# QEngine — Engine Overview (start here)

A re-orientation guide to how the engine works. Read top to bottom; it mirrors the actual data
flow. For reference detail, drop into [ARCHITECTURE.md](ARCHITECTURE.md),
[SYSTEMS.md](SYSTEMS.md), [COMPONENTS.md](COMPONENTS.md), and [TICK_ORDER.md](TICK_ORDER.md).

---

## 1. The one-paragraph version
QEngine is a **data-oriented FPS engine**: all game state lives in an **EnTT registry**
(entities = ids, components = plain data structs), and behaviour lives in **stateless free
functions ("systems")** that query and mutate those components. Rendering uses **OpenGL**
(Phong-lit textured meshes). Physics is delegated to **Jolt** — the ECS holds a mirror
(`Position`) of Jolt's truth, with explicit *sync* systems bridging the two each tick. The loop
runs game logic at a **fixed 60 Hz** while rendering at the display rate, interpolating between
the two most recent physics states for smoothness.

## 2. The module map (where things live)
```
src/
├── main.cpp                     Windowed entry: window/input/camera + the frame loop
├── harness/headless_main.cpp    No-window test entry: scripted input + assertions (same sim)
└── engine/
    ├── app/
    │   ├── simulation.cpp        buildWorld() + stepSimulation() — the shared "what runs" code
    │   └── scene_setup.cpp       setupScene(): builds the showcase from a descriptor list
    ├── core/                     Window (GLFW), InputManager, ResourceManager, FixedTimestep
    ├── ecs/
    │   ├── components.h          Barrel over components/ (core, physics, combat,
    │   │                           gameplay, rendering, tags) — the shared data contract
    │   ├── weapon_definitions.h  createWeapon() — stats for all 7 weapons
    │   └── systems/              The 12 fixed-tick systems + audio/render/hud frame systems
    │                               (+ archived/ dead reference code)
    ├── ai/                       Enemy pathfinding: NavGrid (build_nav_grid) + A* (find_path)
    ├── audio/                    miniaudio + stb_vorbis engine, SoundQueue, queue_sound()
    ├── physics/                  Jolt wrapper (jolt_world), bodies/ (create*Body), layers,
    │                               config; legacy AABB/raycast (inert)
    ├── renderer/                 Camera, Shader, Texture, Mesh, obj_loader
    └── level/                    Level/Sector/Surface, factories + classname dispatch,
                                    showcase_descriptor; .qlvl loader (legacy, unused)
```

**Two executables, one library.** `qengine_lib` holds *everything* except the two `main`s, so
the windowed game and the headless test harness run **byte-identical simulation code**. That's
why the harness can prove physics behaviour without a screen.

## 3. The frame loop (`main.cpp`)
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
  audioSystem   (drain SoundQueue, play this frame's sounds)
  renderSystem  (draws meshes, interpolated by alpha)
  debugHudSystem (bars, ammo, crosshair, damage flash, pickup toast)
  swap buffers
```
The accumulator is clamped (0.25 s) to avoid a "spiral of death" on hitches. Input is sampled
once per *frame* but consumed at fixed rate — standard and fine.

## 4. One fixed tick (`stepSimulation`) — the heart of it
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
10 pickupSystem         ECS AABB overlap (player vs item pickups) → grant + consume
11 playerDeathSystem    if health ≤ 0 → respawn at SpawnPoint, grant invuln
12 demoResetSystem      teleport demo cubes back to start on a timer
```
The "movers + physics step **before** the player controller" ordering (steps 2–6) is the fix
that made lifts carry the player without jitter — see [TICK_ORDER.md](TICK_ORDER.md).

## 5. The ECS ↔ Jolt relationship (the key mental model)
- **Jolt owns physical truth**; the ECS keeps a **mirror** in `Position`.
- Four body kinds, four roles:
  - **Static** — level geometry, shelves (never move).
  - **Dynamic** — cubes (gravity + collision; Jolt → ECS via `joltSyncSystem`).
  - **Kinematic** — doors/lifts (code-driven via `MoveKinematic`; they *push* things).
  - **CharacterVirtual** — the player (direct velocity control + collision response, not a rigid body).
- **Sync direction depends on who's authoritative:** movers are *ECS-authoritative* (push ECS→Jolt,
  step 3); dynamic bodies/player are *Jolt-authoritative* (pull Jolt→ECS, step 5/6).
- **Triggers and pickups deliberately bypass Jolt** — they do their own AABB overlap against ECS
  positions (simpler, and avoids combat's physics sweeps spuriously tripping sensors). No Jolt
  sensor bodies are created.

## 6. Rendering & audio (read-only consumers)
`renderSystem` iterates `MeshRenderer` entities, builds model matrices from
`Position`(interpolated)/`Rotation`/`Scale`, sets camera view/projection + up to 8 point lights
+ one directional light, binds the texture, and issues indexed draws. It owns no game state — it
only reads. `debugHudSystem` draws the 2D overlay (FPS text, health/armour bars, ammo, crosshair,
damage flash, pickup toast) via `stb_easy_font` + `draw_*` primitives in an orthographic pass.
`audioSystem` drains the `SoundQueue` that simulation systems fill via `queueSound()` and plays
each event (miniaudio). Resources (shaders/textures/meshes) are loaded once and cached by name in
`ResourceManager`; components store raw GL handles, not objects. After the world pass,
`renderWeaponViewModel` draws the player's current weapon in **view space** (procedural gun mesh
per `WeaponType`, flat-albedo colour via the lit shader, depth cleared so it overlays the world)
with idle bob, fire recoil, and a switch animation — windowed build only, not part of the sim.

## 7. How a level/scene comes to exist (`buildWorld`)
```
setupScene()              build showcase geometry + spawn all entities from a descriptor list
                            (classname → factory dispatch + two-pass targetname linking)
createLevelBodies()       static Jolt bodies from each surface (fattened to clear convex radius)
OptimizeBroadPhase()
createKinematicBody() × N  for every entity with a Mover
initEnemyCharacters()      CharacterVirtual (+ kinematic inner body) per enemy — collided locomotion
initPlayerCharacter()      CharacterVirtual capsule — AFTER level bodies, so it lands on ground
OptimizeBroadPhase()
buildNavGrid()             walkability grid from the (loaded or showcase) level geometry
```
The geometry is still **hard-coded** (the descriptor list lives in C++), but it's already
`.map`-loader-shaped: the [TrenchBroom plan](../plans/2026-07-03_trenchbroom_engine-loader.md)
swaps the descriptor source for parsed `.map` entities while the *body-creation* and *tick* code
stays the same. See [FACTORIES.md](FACTORIES.md) for the factory layer.

## 8. What's solid vs what's missing (so you know the edges)
- **Solid:** rendering, lighting, Jolt physics, Quake movement, doors/lifts/triggers, item
  pickups (+ armour/ammo), graphical HUD, audio (miniaudio + SFX/music), death/respawn, knockback,
  data-driven entity spawning, fixed timestep + interpolation, headless regression harness, and
  the full 7-weapon loadout (fixed 7-slot inventory, keys 1-7 = weapon type; start with 2, collect
  the rest; each weapon draws from its own ammo pool), and a first-person weapon viewmodel
  (distinct procedural gun shape + colour per weapon, with bob/recoil/switch animation).
- **Solid (enemies):** `monster_grunt` (melee) + `monster_ranged` (keeps distance, fires dodgeable
  bolts) — a `CharacterVirtual` (with a kinematic inner body that blocks the player), shootable with
  hit-flash + hit/death sounds, and `aiSystem` behaviour (LoS sensing/aggro → **A\* pathfinding**
  around walls/props via a `NavGrid`, driven with **collided locomotion** (`ExtendedUpdate`) so it
  can't clip a wall on a corner-cut → melee **or** standoff+ranged attack). Projectiles are
  faction-tagged, so there's no friendly-fire.
- **Missing:** menus/game-states, navmesh (deferred — grid A\* is adequate at current scale),
  enemies traversing lifts/doors.
  See the active [plans](../plans/README.md).
- **Legacy/inert (don't be fooled):** `level/` `.qlvl` loader (unused), `physics/{collision,
  spatial_hash,aabb}` + legacy `raycast` triangle path, and `ecs/systems/archived/*` — all kept
  as tutorial "old-vs-new" reference, none compiled into behaviour.
