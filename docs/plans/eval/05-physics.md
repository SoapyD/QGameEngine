# 05 — Physics System (Jolt) — Deep-Dive Evaluation

**Status:** Draft — evaluation only (no fixes applied yet)
**Scope:** The Jolt-based physics/movement system shipped in Chapters 14–15 (player `CharacterVirtual`, kinematic movers, sync ordering).
**Trigger:** Visible glitches when the player walks onto the lift; general "physics section has a few glitches" before moving on to Phase 5 (TrenchBroom).
**Part of:** the whole-codebase evaluation — see [README.md](README.md).

## Reference target (what "correct" means)

The Phase 4 milestone in [ROADMAP.md:83](../../roadmap/ROADMAP.md#L83) is the acceptance bar:

> Player with Jolt-powered physics — **solid collision, lifts that carry the player, no jitter on resting contact.**

So the evaluation must end able to answer, with evidence: does the player ride the lift smoothly (up *and* down), can they walk on/off it without snagging or popping, and is there zero jitter at rest?

---

## 1. System map (the moving parts to evaluate)

| Stage | System | File | Role in the glitch |
|------|--------|------|--------------------|
| 2 | `playerCharacterSystem` | [player_character_system.cpp](../../../src/engine/ecs/systems/player_character_system.cpp) | Builds player velocity, runs `ExtendedUpdate` |
| 3 | `moverSystem` | [mover_system.cpp](../../../src/engine/ecs/systems/mover_system.cpp) | Animates lift ECS position via state machine |
| 4 | `moverSyncSystem` | [mover_sync_system.cpp](../../../src/engine/ecs/systems/mover_sync_system.cpp) | `MoveKinematic` pushes lift target to Jolt |
| 5 | `JoltWorld::step` | [jolt_world.h:59](../../../src/engine/physics/jolt_world.h#L59) | Sweeps kinematic bodies, integrates dynamics |
| 6 | `joltSyncSystem` | [jolt_sync_system.cpp](../../../src/engine/ecs/systems/jolt_sync_system.cpp) | Reads Jolt → ECS (movers/dynamics, **not** player) |

Tick order reference: [TICK_ORDER.md:48](../../architecture/TICK_ORDER.md#L48).

---

## 2. Primary hypothesis — tick-order desync between player and lift

This is the most likely root cause of the walk-onto-lift glitch and should be checked first.

**The ordering:** the player's `ExtendedUpdate` runs in stage **2**, but the lift only physically moves in stage **5** (`MoveKinematic` queued in stage 4, swept in stage 5). So every tick the player resolves collision against the lift's **previous-tick** position, then the lift moves *after* the player has already finalised.

**Predicted symptoms:** on a rising lift the player is left one tick behind and the lift surface penetrates the capsule, which the *next* `ExtendedUpdate` resolves by ejecting the player → bounce/jitter. On a descending lift the player floats then drops. Walking onto the edge is worst because the seam between floor body and lift body is where the lag shows.

### Checks
- [ ] **Confirm the lag exists.** Log, each tick: lift ECS Y (post stage 3), lift Jolt body Y (post stage 5), player Y, player `GetGroundState()`. Look for the player resolving against a stale lift Y.
- [ ] **Confirm CharacterVirtual is never re-resolved after the kinematic move.** It isn't called again post-`step()` — verify there is no second update path.
- [ ] **Decide the fix shape** (for the follow-up implementation plan, not now): either (a) move `playerCharacterSystem` to run *after* `joltWorld.step()`, or (b) update movers *before* the player (run `moverSystem`+`moverSyncSystem`+a kinematic pre-step before the character update), or (c) adopt ground velocity (Section 3). Capture trade-offs of each.

---

## 3. Secondary hypothesis — no platform (ground) velocity inheritance

`CharacterVirtual` does **not** automatically ride a moving kinematic platform. The desired velocity in [player_character_system.cpp:62-151](../../../src/engine/ecs/systems/player_character_system.cpp#L62-L151) is built from **input + gravity only** — it never adds `character->GetGroundVelocity()`.

**Predicted symptoms:** vertical "carry" relies entirely on `mStickToFloorStepDown` + the kinematic push, which is fragile; the player can be left behind going up or sink going down, and gets zero horizontal carry on moving doors.

### Checks
- [ ] Verify `GetGroundVelocity()` is non-zero while standing on the moving lift (instrument it).
- [ ] Check whether `mStickToFloorStepDown` (= `stepHeight` = 0.7) is doing the carrying by accident, and how it interacts with the lift speed (2.0 u/s → 0.033 u/tick; well inside 0.7, so stick-down currently masks part of the problem — confirm).
- [ ] Determine the intended pattern: blend `GetGroundVelocity()` into `desiredVel` when `OnGround`. Note implications for the air/ground branch and for jump (must not inherit on jump).

---

## 4. Internal-edge snagging (walk-on / general movement)

The level is built from **many separate static box bodies**, one per surface, each fattened by ±0.1 ([JOLT_PHYSICS.md:114-118](../../architecture/JOLT_PHYSICS.md#L114-L118)). Adjacent boxes create internal edges/seams the capsule can catch on. The floor→lift boundary is exactly such a seam.

### Checks
- [ ] Confirm `mEnhancedInternalEdgeRemoval` is **not** set on the character settings ([player_character_system.cpp:24-29](../../../src/engine/ecs/systems/player_character_system.cpp#L24-L29)) — it isn't. Evaluate enabling it.
- [ ] Reproduce: walk slowly across flat floor seams and onto the lift edge; watch for velocity spikes / micro-stops.
- [ ] Check whether `ExtendedUpdate`'s `mWalkStairsStepUp` (= 0.7) causes the player to "pop" up onto the lift lip when it sits slightly proud of the floor.

---

## 5. Ground-state staleness

`GetGroundState()` is read at the **top** of the tick ([player_character_system.cpp:57](../../../src/engine/ecs/systems/player_character_system.cpp#L57)), so it reflects the *previous* `ExtendedUpdate`. The ground/air branch (which decides gravity application and friction) is therefore one tick stale — compounding the Section 2 desync on the lift.

### Checks
- [ ] Log `onGround` vs actual contact each tick while boarding/riding the lift; quantify how often it disagrees.
- [ ] On ground, the code preserves `currentVel.GetY()` ([line 107](../../../src/engine/ecs/systems/player_character_system.cpp#L107)). Check whether residual negative Y from resting contact accumulates and fights `mStickToFloorStepDown` → **resting jitter** (directly violates the milestone).

---

## 6. Gravity & terminal-velocity correctness

- [ ] **Magic-number gravity.** `-20.0f` is hardcoded in [player_character_system.cpp:137](../../../src/engine/ecs/systems/player_character_system.cpp#L137) *and* set in [jolt_world.h:56](../../../src/engine/physics/jolt_world.h#L56). Verify they agree and decide whether the player should read `physicsSystem->GetGravity()` instead.
- [ ] **Terminal velocity never enforced.** `PhysicsConfig.terminalVelocity = 50` ([physics_config.h:7](../../../src/engine/physics/physics_config.h#L7)) is unused; the comment admits it's a "magic number" that isn't wired up. Confirm nothing clamps fall speed for player or dynamic bodies; decide if that matters for the showcase.
- [ ] **Single vs double gravity application.** Confirm air gravity is applied once (velocity build) and that the gravity passed to `ExtendedUpdate` ([line 163](../../../src/engine/ecs/systems/player_character_system.cpp#L163)) only drives stick/stair prediction, not a second integration.

---

## 7. Doc ↔ code divergences (correctness of the architecture docs)

The docs are part of what's being evaluated; these are already-confirmed mismatches to fix alongside any code changes:

- [ ] `stepHeight` documented as **1.5** ([JOLT_PHYSICS.md:156](../../architecture/JOLT_PHYSICS.md#L156)) but is **0.7** ([components.h:90](../../../src/engine/ecs/components.h#L90)).
- [ ] Capsule "halfHeight 0.55 / radius 0.3" in docs — verify against the values derived from `AABBCollider.halfExtents` at runtime ([player_character_system.cpp:17-18](../../../src/engine/ecs/systems/player_character_system.cpp#L17-L18)).
- [ ] Doc claim that `MoveKinematic` "is why the lift carries the player upward" ([JOLT_PHYSICS.md:167](../../architecture/JOLT_PHYSICS.md#L167)) is at best half-true given the Section 2 ordering — correct the explanation once the mechanism is confirmed.

---

## 8. Dead / orphaned code and config smells

- [ ] **Orphaned duplicate:** [src/engine/physics/jolt_sync_system.cpp](../../../src/engine/physics/jolt_sync_system.cpp) and its `.h` are **not** in [CMakeLists.txt](../../../CMakeLists.txt#L62-L89) (the `ecs/systems/` copy is the live one). Confirm dead, then delete to avoid editing the wrong file. *(Tracked in area 01.)*
- [ ] **Empty TU:** [jolt_world.cpp](../../../src/engine/physics/jolt_world.cpp) is just an `#include` (header-only impl). Harmless; note it.
- [ ] **Unused sensor bodies:** Jolt sensor bodies are created but triggers use ECS AABB overlap instead ([JOLT_PHYSICS.md:98](../../architecture/JOLT_PHYSICS.md#L98)). Confirm they're inert and decide keep-or-remove (divergence risk).
- [ ] **`joltSyncSystem` ground heuristic** (`abs(velY) < 0.5`, [jolt_sync_system.cpp:33](../../../src/engine/ecs/systems/jolt_sync_system.cpp#L33)) is described as superseded but still runs for any `JoltBody`+`OnGround` entity. Confirm no entity relies on it incorrectly (player uses `JoltCharacter`, so unaffected — verify).

---

## 9. Forward-looking / scalability (because Phase 5 is next)

- [ ] **Body budget:** `maxBodies = 1024`, `maxBodyPairs = 1024`, `maxContactConstraints = 1024` ([jolt_world.h:43-46](../../../src/engine/physics/jolt_world.h#L43-L46)). TrenchBroom levels (Ch 17–20) generate far more brush bodies — confirm headroom or flag as a Phase 5 blocker.
- [ ] **No render interpolation** between fixed ticks ([TICK_ORDER.md:97](../../architecture/TICK_ORDER.md#L97)). At high FPS the lift/player render in discrete steps, which *looks* like a physics glitch even when the simulation is correct. Decide whether to rule this out first (it may explain part of the visual "glitch").
- [ ] **Single collision sub-step** ([jolt_world.h:63](../../../src/engine/physics/jolt_world.h#L63)). Confirm it's adequate at current speeds; note as a knob if fast movers are added.

---

## 10. Verification methodology

1. **Reproduce deterministically.** Define the exact repro: approach the lift from floor level, board while idle, board while rising, ride up, ride down, step off at top. Record what's visually wrong at each.
2. **Instrument before changing anything.** Add temporary per-tick logging (lift ECS Y, lift Jolt Y, player Y, ground state, `GetGroundVelocity`) to confirm/deny each hypothesis with data rather than guesswork.
3. **Rule out rendering.** Cap or vary FPS to separate render-stepping (Section 9) from true simulation jitter.
4. **Attribute each symptom** to a section above before proposing fixes.
5. **Output:** a findings table (symptom → confirmed cause → section) that feeds a *separate* implementation plan. This document does not change code.

---

## Suggested investigation order

1. Section 2 (ordering) + Section 5 (stale ground state) — instrument together; most likely the core defect.
2. Section 3 (ground velocity) — the probable "carry" fix.
3. Section 4 (internal edges) — the probable "walk-on snag" fix.
4. Section 9 render interpolation — cheap to rule in/out, may explain visual jitter.
5. Sections 6–8 — correctness/cleanup, fold into whatever PR fixes the above.
