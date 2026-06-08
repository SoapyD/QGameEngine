# Fix Plan — Headless Simulation & Debug Harness

**Status:** ✅ COMPLETE — implemented & verified 2026-06-08. Phase A (hidden-window headless) shipped with 6 green scenarios; Phase B (true no-GL / GPU-less CI) deferred as a future enhancement. Archived.
**Type:** Implementation plan (graduated from the evaluation bundle — see [eval/README.md](eval/README.md)).
**Priority:** Do this **before** `physics-fixes.md`. It's what turns "you drive while I read logs" into "I reproduce, measure, and regression-test the physics myself."
**Why:** The engine today has no tests, no headless mode, no input scripting — `main.cpp` always opens a window and reads live GLFW input. Every bug we found ([lift glitch](eval/05-physics.md), [teleporter](eval/07-gameplay-systems.md#L1), [projectiles through walls](eval/07-gameplay-systems.md)) is only reproducible by a human at a keyboard looking at a screen. This plan removes that dependency.

---

## Goal

A windowless build that runs the **exact same fixed-tick simulation** as the game, driven by **scripted input**, recording per-tick state, so that:

1. Physics bugs reproduce deterministically with no display, GPU interaction, or human input.
2. Each bug becomes an automated **assertion** (regression test) that fails today and passes once fixed.
3. I can run it from here, read the recorded numbers, and confirm/deny the [05 §10](eval/05-physics.md) hypotheses directly.

**Non-goal:** rendering correctness. The harness never draws — visual/shader bugs stay in the windowed build's domain ([04](eval/04-renderer.md)).

---

## Core problem to solve: GL coupling

The simulation is *mostly* GL-free (Jolt + ECS systems need no OpenGL), but two couplings block a clean headless run:

- **Resource loading** creates GL objects: `ResourceManager::getMesh/getTexture/getShader` call `glGen*`. `scene_setup` needs mesh VAOs for `MeshRenderer` and `CombatResources`.
- **Level building creates GL meshes**: `buildSectorMeshes` ([level_loader.cpp:189](../../../src/engine/level/level_loader.cpp#L189)) makes a `Mesh` (VAO) per sector during level load — but the **collision** geometry (`createLevelBodies`) only needs `surface.vertices`, not the GL mesh.

Two strategies, sequenced:

### Phase A — hidden-window headless (fast, local)
Open a GLFW window with `glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE)`. A real GL context exists, so resources load unchanged, but nothing is presented and the render systems are simply not called. Minimal refactor; works on your GPU machine immediately. (Limitation: still needs a GL driver, so not pure-CI.)

### Phase B — true no-GL headless (clean, CI-ready)
Decouple collision geometry from GL meshes so the simulation builds with **zero** GL:
- Make `buildSectorMeshes` optional / lazy (skip it in headless; `createLevelBodies` already works from raw vertices).
- Give `ResourceManager` a headless mode that hands out stub ids (0) instead of calling `glGen*`, or split "asset data" from "GPU upload."
- `MeshRenderer`/`CombatResources` carry id 0 in headless — harmless because render systems never run.

**Recommendation:** ship Phase A first (unblocks me this week), then Phase B when CI matters (before Phase 5).

---

## Architecture changes

### 1. Factor the engine into a library target
Today [CMakeLists.txt:62](../../../CMakeLists.txt#L62) builds one executable from all sources. Restructure:

```
qengine_lib   (STATIC)  — all engine sources EXCEPT main.cpp
QEngine       (exe)     — main.cpp            + qengine_lib   (windowed game)
QEngineHeadless (exe)   — headless_main.cpp   + qengine_lib   (harness/tests)
```

This is the enabling refactor — both entry points share identical system code, so there's no "tests drift from the game" risk.

### 2. Extract two reusable functions from `main.cpp`
The windowed loop and the harness must run **the same** setup and tick, or the harness proves nothing:

- `buildWorld(registry, resources, JoltWorld&) -> Level` — everything in `main.cpp` lines 73-107 (scene + bodies + player + broadphase).
- `stepSimulation(registry, JoltWorld&, const Level&, float dt)` — the body of the `while (fixedTimestep.step())` loop ([main.cpp:184-199](../../../src/main.cpp#L184-L199)).

`main.cpp` then *calls* these. **Bonus:** the [08 §8.1](eval/08-integration.md) tick-order fix (the lift-glitch reorder) then lives in **one** function that both builds share — fix once, verified by the harness.

### 3. Scripted input
Replace the per-frame GLFW→`PlayerInput` glue ([main.cpp:146-178](../../../src/main.cpp#L146-L178)) with a scriptable source the harness drives:

```
struct InputCommand {
    int   ticks;        // how many fixed ticks to hold this
    glm::vec3 wishDir;  // world-space desired direction (already flattened)
    bool  jump, fire;
    int   weaponSwitch;
};
// A scenario is a std::vector<InputCommand> replayed tick-by-tick.
```

The harness writes `PlayerInput` each tick from the active command — no window, no GLFW.

### 4. Determinism
- Use Jolt's **single-threaded** job system in headless (`JobSystemSingleThreaded`, already compiled) so runs are byte-reproducible. Add a flag to `JoltWorld::init`.
- Seed the combat `std::mt19937` ([combat_system.cpp:11](../../../src/engine/ecs/systems/combat_system.cpp#L11)) from a fixed value in headless (today it's `random_device` → non-reproducible spread).

### 5. Recording & assertions
- A `Recorder` that snapshots chosen state each tick (player `Position`/`OnGround`/`GetGroundVelocity`, a named mover's ECS+Jolt Y, health, projectile count) into rows.
- Dump as CSV to stdout (so I can read it) **and** feed an in-process assertion list.
- Test runner returns non-zero exit code on any failed assertion (CI-friendly). Keep it dependency-free (tiny custom `CHECK`), or pull `doctest` via FetchContent (matches the Jolt pattern) — recommend the tiny custom runner first.

---

## Scenarios to ship (each is a regression test)

| Scenario | Drives | Asserts (fails today → passes after fix) | Targets |
|----------|--------|------------------------------------------|---------|
| `rest_no_jitter` | Stand on floor, no input, 300 ticks | Player Y variance < ε; vertical velocity not oscillating | [05 §5](eval/05-physics.md) resting jitter |
| `ride_lift_up` | Walk onto lift trigger, wait `startDelay`, ride to top | Player foot Y tracks lift top within ε every tick; no Y bounce; ends at top | **lift glitch** [05 §2/3](eval/05-physics.md) |
| `board_moving_lift` | Step onto lift while it's already rising | Player is carried, not penetrated/ejected | [05 §2](eval/05-physics.md) |
| `walk_floor_seams` | Strafe across flat floor-body seams | No velocity spikes / micro-stops | [05 §4](eval/05-physics.md) internal edges |
| `teleporter` | Walk into the teleport trigger | Player position jumps to destination **and stays** next tick | [07 §7.1](eval/07-gameplay-systems.md) (fails now) |
| `rocket_vs_wall` | Fire rocket at a wall | Projectile detonates at wall distance, doesn't pass through | [07 §7.2](eval/07-gameplay-systems.md) (fails now) |
| `fall_terminal` | Drop player from height | Fall speed clamps to terminal velocity | [05 §6](eval/05-physics.md) (currently unenforced) |

The first run is itself diagnostic: `ride_lift_up`'s CSV dump (lift ECS Y vs lift Jolt Y vs player Y vs ground state) **is** the [05 §10](eval/05-physics.md) instrumentation pass — it confirms the tick-order desync with data before we touch the fix.

---

## Implementation steps

1. **CMake refactor** → `qengine_lib` static lib + `QEngine` exe (build must stay green; pure restructure, no behaviour change). Verify the windowed game still runs.
2. **Extract** `buildWorld` + `stepSimulation` from `main.cpp`; rewire `main.cpp` to call them. Verify game unchanged.
3. **Phase A headless**: add `headless_main.cpp` + `QEngineHeadless` target; hidden window; build world; run `stepSimulation` on a scripted scenario; CSV dump.
4. **Scripted input + Recorder + tiny test runner.**
5. **Determinism**: single-threaded Jolt + seeded RNG in headless.
6. **Author the 7 scenarios**; commit them red where the bug exists (lift, teleporter, rocket) as executable proof of the bugs.
7. **(Later) Phase B**: decouple `buildSectorMeshes`/resources from GL for pure no-GPU CI.

## Risks / notes
- Step 1-2 are a refactor of working code — do them in isolation, confirm the windowed game is byte-identical in behaviour before adding the harness.
- Hidden GLFW window still needs a GL driver; true headless (Phase B) is required for GPU-less CI.
- Reproducibility depends on the determinism work (step 5) — without it, scenario assertions need tolerances, not exact equality.

## How it slots into the backlog
Insert as the step **before** `physics-fixes.md` in [eval/README.md](eval/README.md). The physics fix then proceeds: run `ride_lift_up` (red) → apply the [05](eval/05-physics.md) fixes in `stepSimulation`/`player_character_system` → run again (green). Same loop retires the teleporter and projectile bugs.
