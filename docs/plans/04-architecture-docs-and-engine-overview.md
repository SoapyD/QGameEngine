# Plan 04 — Architecture-Docs Update + High-Level Engine Overview

Two parts:
- **Part A** — a plan to bring the architecture docs back in sync with the code.
- **Part B** — a ready-to-read **high-level overview of how the engine works**, so you can
  re-orient quickly without reading source. (This doubles as the content for the doc updates.)

---

# Part A — Architecture-docs update plan

The docs in `docs/architecture/` are well-structured but have **drifted** since the
2026-06-08 eval/cleanup (which changed code but only partly updated docs). The fixes are
mechanical; do them in one pass.

### A.1 Fix broken links (5 min)
- **Root `README.md`** points at `docs/ARCHITECTURE.md`, `docs/SYSTEMS.md`, … — all six live
  under `docs/architecture/`. Repoint them.
- `docs/architecture/README.md` links `ROADMAP.md` / `CPP_CONCEPTS_BY_CHAPTER.md` /
  `FUTURE_TUTORIALS.md` as siblings; `ROADMAP*` and `FUTURE_TUTORIALS` are in `docs/roadmap/`.
  Repoint (CPP_CONCEPTS *is* in `architecture/`, so that one's fine).

### A.2 Sync `COMPONENTS.md` (biggest gap)
The code has **components the doc never mentions**, and **defaults the doc has wrong**:

- **Add** (present in `components.h`, missing from doc): `PrevPosition`, `SpawnPoint`,
  `DamageFlash`, `PendingKnockback`, `CameraDirection`, and `Health.invulnerableTimer`.
  (`HudConfig`/`CombatResources` are documented as ctx objects — keep, but note they're structs
  in `components.h`.)
- **Remove**: the `Gravity` component — it was deleted in the eval cleanup but is still documented.
- **Fix default**: `CharacterPhysics.stepHeight` is **0.7** (doc says 1.5).
- **Note as inert**: `AABBCollider.layer`/`mask` (unused since Jolt; retained harmlessly).

### A.3 Sync `SYSTEMS.md`
- **Add `playerDeathSystem`** — it's in the tick order and code but not documented as a system.
- Confirm the documented order matches `simulation.cpp::stepSimulation` (it now does: weaponSwitch
  → mover → moverSync → joltStep → joltSync → playerCharacter → combat → lifetime → trigger →
  **playerDeath** → demoReset).

### A.4 Sync `SCENE_SETUP.md`
- Drop `Gravity` from the player's component list.
- Reconcile the **lava damage value** (doc says "25 dmg/s"; code applies a per-tick value — state
  the real number and unit).
- Add the player's `SpawnPoint`, `DamageFlash`, `PendingKnockback` to the component table.

### A.5 Sync `ARCHITECTURE.md`
- Move **"Death / respawn"** from *Not Implemented* to *Implemented* (it exists).
- Fix the `extern/` tree: Jolt is **FetchContent** (not a submodule dir); there's no
  `tinyobjloader` (OBJ loading is the in-repo `obj_loader.cpp`). Submodules are glfw/glm/entt
  (per `.gitmodules`); glad/stb are vendored.
- Add `src/engine/app/` (simulation) and `src/harness/` to the structure block.

### A.6 Sync `JOLT_PHYSICS.md` / `TICK_ORDER.md`
- These were updated in the eval and are largely correct. Verify `stepHeight 0.7` references
  agree, and that `TICK_ORDER.md` still lists 11 systems including `playerDeathSystem`.

### A.7 New docs worth adding
- **`ENGINE_OVERVIEW.md`** — promote Part B below into `docs/architecture/` as the
  "start here" narrative (the README currently jumps straight into reference tables).
- **`FACTORIES.md`** (once Plan 02 #1 lands) — document the `spawn*` entity-factory layer; it
  becomes the contract TrenchBroom entity mapping (Plan 03) targets.

### A.8 Process suggestion (prevent re-drift)
Add a one-line rule to `docs/architecture/README.md`: *"When you change `components.h` or the
system tick order, update COMPONENTS.md / SYSTEMS.md / TICK_ORDER.md in the same commit."*
The drift all came from code-only commits.

---

# Part B — High-level overview: how the engine works

A re-orientation guide. Read top to bottom; it mirrors the actual data flow.

## B.1 The one-paragraph version
QEngine is a **data-oriented FPS engine**: all game state lives in an **EnTT registry**
(entities = ids, components = plain data structs), and behaviour lives in **stateless free
functions ("systems")** that query and mutate those components. Rendering uses **OpenGL**
(Phong-lit textured meshes). Physics is delegated to **Jolt** — the ECS holds a mirror
(`Position`) of Jolt's truth, with explicit *sync* systems bridging the two each tick. The loop
runs game logic at a **fixed 60 Hz** while rendering at the display rate, interpolating between
the two most recent physics states for smoothness.

## B.2 The module map (where things live)
```
src/
├── main.cpp                     Windowed entry: window/input/camera + the frame loop
├── harness/headless_main.cpp    No-window test entry: scripted input + assertions (same sim)
└── engine/
    ├── app/simulation.cpp       buildWorld() + stepSimulation() — the shared "what runs" code
    ├── core/                    Window (GLFW), InputManager, ResourceManager, FixedTimestep
    ├── ecs/
    │   ├── components.h         EVERY component (the shared data contract)
    │   ├── scene_setup.cpp      Builds the showcase: geometry + all entities (currently inline)
    │   ├── showcase_level.cpp   Hard-coded room geometry (the 30×30×6 box)
    │   ├── jolt_body_helpers.*  create{Static,Dynamic,Kinematic,Sensor,Level}Body
    │   ├── weapon_definitions.h createWeapon() — stats for all 7 weapons
    │   └── systems/             The 12 active systems (+ archived/ dead reference code)
    ├── physics/                 Jolt wrapper (jolt_world), layers/filters, config; legacy AABB/raycast
    ├── renderer/                Camera, Shader, Texture, Mesh, OBJ loader
    └── level/                   Level/Sector/Surface structs + .qlvl loader (legacy, unused)
```

**Two executables, one library.** `qengine_lib` holds *everything* except the two `main`s, so
the windowed game and the headless test harness run **byte-identical simulation code**. That's
why the harness can prove physics behaviour without a screen.

## B.3 The frame loop (`main.cpp`)
```
each frame:
  accumulate real elapsed time
  poll GLFW input  → camera mouse-look → build wishDir (WASD) → write PlayerInput + CameraDirection
  while (accumulator >= 1/60):            ← fixed-timestep catch-up loop
      snapshot Position → PrevPosition    ← interpolation base
      stepSimulation(dt = 1/60)           ← all game logic, exactly 60 Hz
      accumulator -= 1/60
  alpha = accumulator / (1/60)
  camera follows player (lerp prev→current by alpha)
  renderSystem  (draws meshes, interpolated by alpha)
  debugHudSystem (text overlay)
  swap buffers
```
The accumulator is clamped (0.25 s) to avoid a "spiral of death" on hitches. Input is sampled
once per *frame* but consumed at fixed rate — standard and fine.

## B.4 One fixed tick (`stepSimulation`) — the heart of it
Order matters; this is the sequence and *why*:
```
 1 weaponSwitchSystem   input → which weapon is active (before combat can fire)
 2 moverSystem          advance door/lift state machines → new ECS Position
 3 moverSyncSystem      push those positions into Jolt KINEMATIC bodies (MoveKinematic)
 4 joltWorld.step(dt)   Jolt simulates: gravity, collisions, sweeps kinematic bodies
 5 joltSyncSystem       read Jolt body transforms BACK into ECS Position (+ OnGround heuristic)
 6 playerCharacterSystem player input → CharacterVirtual velocity → ExtendedUpdate
                         (runs AFTER movers moved + were swept, so the player resolves
                          against the lift's CURRENT position → smooth boarding/riding)
 7 combatSystem         fire weapons: hitscan rays / spawn projectiles, apply damage+knockback
 8 lifetimeSystem       tick down timers, destroy expired projectiles/tracers
 9 triggerSystem        ECS AABB overlap (player vs trigger volumes) → run actions
10 playerDeathSystem    if health ≤ 0 → respawn at SpawnPoint, grant invuln
11 demoResetSystem      teleport demo cubes back to start on a timer
```
The "movers + physics step **before** the player controller" ordering (steps 2–6) is the fix
that made lifts carry the player without jitter — see `TICK_ORDER.md`.

## B.5 The ECS ↔ Jolt relationship (the key mental model)
- **Jolt owns physical truth**; the ECS keeps a **mirror** in `Position`.
- Four body kinds, four roles:
  - **Static** — level geometry, shelves (never move).
  - **Dynamic** — cubes (gravity + collision; Jolt → ECS via `joltSyncSystem`).
  - **Kinematic** — doors/lifts (code-driven via `MoveKinematic`; they *push* things).
  - **CharacterVirtual** — the player (direct velocity control + collision response, not a rigid body).
- **Sync direction depends on who's authoritative:** movers are *ECS-authoritative* (push ECS→Jolt,
  step 3); dynamic bodies/player are *Jolt-authoritative* (pull Jolt→ECS, step 5/6).
- **Triggers deliberately bypass Jolt** — `triggerSystem` does its own AABB overlap against ECS
  positions (simpler, and avoids combat's physics sweeps spuriously tripping sensors). The Jolt
  sensor bodies were removed.

## B.6 Rendering (read-only consumer)
`renderSystem` iterates `MeshRenderer` entities, builds model matrices from
`Position`(interpolated)/`Rotation`/`Scale`, sets camera view/projection + up to 8 point lights
+ one directional light, binds the texture, and issues indexed draws. It owns no game state —
it only reads. `debugHudSystem` draws text (FPS/health/ammo) via `stb_easy_font` in an
orthographic pass. Resources (shaders/textures/meshes) are loaded once and cached by name in
`ResourceManager`; components store raw GL handles, not objects.

## B.7 How a level/scene comes to exist (`buildWorld`)
```
setupScene()              build showcase geometry (createShowcaseLevel) + spawn all entities
createLevelBodies()       static Jolt bodies from each surface (fattened to clear convex radius)
OptimizeBroadPhase()
createKinematicBody() × N  for every entity with a Mover
initPlayerCharacter()      CharacterVirtual capsule — AFTER level bodies, so it lands on ground
OptimizeBroadPhase()
```
Today this is **hard-coded**; Plan 03 replaces `setupScene`'s geometry/entities with a
TrenchBroom `.map` load, but the *body-creation* and *tick* code stays the same.

## B.8 What's solid vs what's missing (so you know the edges)
- **Solid:** rendering, lighting, Jolt physics, Quake movement, doors/lifts/triggers, weapons
  (all 7 defined), death/respawn, knockback data, fixed timestep + interpolation, headless
  regression harness.
- **Missing/partial:** pickups, enemies/AI, audio, graphical HUD (crosshair/bars), menus/states,
  authored levels (TrenchBroom). See Plans 02 and 03.
- **Legacy/inert (don't be fooled):** `level/` `.qlvl` loader (unused), `physics/{collision,
  spatial_hash,aabb}` + legacy `raycast` triangle path, and `ecs/systems/archived/*` — all kept
  as tutorial "old-vs-new" reference, none compiled into behaviour.

---

## Suggested execution for this plan
1. **Part A.1–A.6** doc-sync in a single commit (no code risk).
2. Promote **Part B** into `docs/architecture/ENGINE_OVERVIEW.md` and link it from both READMEs.
3. Add the **A.8 same-commit rule** to stop future drift.
4. Defer **FACTORIES.md** until Plan 02 #1 ships.
