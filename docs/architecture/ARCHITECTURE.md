# QEngine — Architecture Reference

Updated through Chapter 20 (pickups, graphical HUD, audio). See [COMPONENTS.md](COMPONENTS.md), [SYSTEMS.md](SYSTEMS.md), and [TICK_ORDER.md](TICK_ORDER.md) for detailed breakdowns.

---

## Project Structure

```
QEngine/
├── CMakeLists.txt
├── extern/                          # Third-party libraries (in-repo)
│   ├── entt/                        #   ECS framework
│   ├── glfw/                        #   Window + input
│   ├── glad/                        #   OpenGL loader
│   ├── glm/                         #   Math library
│   └── stb/                         #   Image loading, easy font
│                                    #   (Jolt Physics is fetched via CMake FetchContent,
│                                    #    OBJ loading is the in-repo renderer/obj_loader.cpp)
├── assets/
│   ├── shaders/                     #   GLSL vertex/fragment shaders
│   │   ├── basic.vert/.frag         #     Unlit, solid colour
│   │   ├── textured.vert/.frag      #     Textured, unlit
│   │   ├── lit.vert/.frag           #     Textured + Phong lighting
│   │   └── hud.vert/.frag           #     2D orthographic HUD
│   ├── textures/                    #   Grid textures (grey, orange, blue, green, red)
│   └── models/
│       └── cube.obj                 #   Unit cube mesh
└── src/
    ├── main.cpp                     # Windowed entry point, game loop, input collection
    ├── harness/
    │   └── headless_main.cpp        #   Headless entry point (QEngineHeadless target)
    └── engine/
        ├── core/
        │   ├── window.h/.cpp        #   GLFW window wrapper (create, poll, swap)
        │   ├── input_manager.h/.cpp #   Keyboard/mouse state tracking
        │   ├── resource_manager.h/.cpp  # Shader/texture/mesh loading + caching
        │   └── fixed_timestep.h     #   Fixed timestep accumulator
        ├── ecs/                     #   File organisation: see CODING_STANDARD.md
        │   ├── components.h         #   Barrel — re-includes components/*
        │   ├── components/          #   Component defs by group (core, physics,
        │   │                        #     combat, gameplay, rendering, tags)
        │   ├── types/               #   Incidental types (entity_hit, mesh_assets, system_phase)
        │   ├── weapon_definitions.h #   createWeapon() factory (inline)
        │   └── systems/             #   One free function per file; grouped by domain folder
        │       ├── player/          #     character, init, input, death, camera_follow
        │       ├── mover/           #     mover_system
        │       ├── sync/            #     mover_sync, jolt_sync (ECS↔Jolt)
        │       ├── combat/          #     combat_system + fire_*/splash/... + weapon_switch
        │       ├── debug_hud/       #     debug_hud_system + draw_*
        │       ├── render/          #     render_system
        │       ├── trigger/         #     trigger_system
        │       ├── lifetime/        #     lifetime_system
        │       ├── demo/            #     demo_reset_system
        │       └── archived/        #     Dead pre-Jolt systems (not compiled — see README)
        ├── physics/
        │   ├── jolt_setup.h         #   Jolt layers, filters, callbacks
        │   ├── jolt_world.h/.cpp    #   JoltWorld wrapper (init, step, shutdown)
        │   ├── physics_config.h     #   PhysicsConfig (gravity, fixedDeltaTime, ...)
        │   ├── collision_layers.h   #   Jolt object-layer bitmasks (live)
        │   ├── raycast.h            #   Ray-AABB / ray-triangle declarations
        │   ├── raycast/             #   ray_intersect_aabb.cpp, ray_intersect_triangle.cpp
        │   ├── jolt_bodies.h        #   Body-creation declarations
        │   ├── bodies/              #   create_{static,dynamic,kinematic,sensor,level}_body.cpp
        │   ├── types/               #   aabb.h, ray.h (value types)
        │   └── (collision.*, spatial_hash.*  — LEGACY, not compiled)
        ├── renderer/                #   Single classes, kept whole (one .h/.cpp each)
        │   ├── camera.h/.cpp        #   FPS camera (position, orientation, matrices)
        │   ├── shader.h/.cpp        #   Shader compilation + uniform helpers
        │   ├── texture.h/.cpp       #   Texture loading (stb_image)
        │   ├── mesh.h/.cpp          #   VAO/VBO/EBO management
        │   ├── obj_loader.h/.cpp    #   OBJ file parser
        │   └── stb_image_impl.cpp   #   stb_image implementation unit
        ├── level/
        │   ├── level.h              #   Level, Sector, Surface structs
        │   ├── factories.h/.cpp     #   spawnPlayer / spawnMover / spawnTrigger / ... (level entities)
        │   ├── build_sector_meshes.h/.cpp  # Sector render meshes (live)
        │   ├── showcase_level.h/.cpp       # Hardcoded showcase room geometry
        │   └── level_loader.h/.cpp  #   LEGACY .qlvl parser (not compiled)
        └── app/
            ├── scene_setup.h/.cpp   #   Entity spawning (setupScene) — world bootstrap
            └── simulation.h/.cpp    #   Shared buildWorld + stepSimulation (windowed + headless)
```

---

## Design Principles

### ECS Architecture (EnTT)
- **Components** are plain data structs — no methods, no inheritance
- **Systems** are free functions — no state, no member variables
- **Registry** is the single source of truth — all game state lives here
- **Context objects** (`registry.ctx()`) store singletons: `PhysicsConfig`, `JoltWorld`, `HudConfig`, `CombatResources`, `CameraDirection`, `SoundQueue`

### Physics (Jolt)
- **Static bodies** for level geometry and immovable platforms
- **Dynamic bodies** for physics objects (cubes)
- **Kinematic bodies** for movers (doors, lifts) — pushed via `MoveKinematic`
- **CharacterVirtual** for the player — direct velocity control with collision response
- **Triggers and pickups** use ECS AABB overlap, *not* Jolt sensor bodies — `buildWorld` deliberately creates none (a `createSensorBody` helper exists but is unused)
- See [JOLT_PHYSICS.md](JOLT_PHYSICS.md) for full details

### Fixed Timestep
- Physics and game logic run at 60 ticks/second (`fixedDeltaTime = 1/60`)
- Rendering runs at display frame rate
- Accumulator pattern prevents spiral of death (clamped to 0.25s max)
- See [TICK_ORDER.md](TICK_ORDER.md) for the complete loop breakdown

### Resource Management
- `ResourceManager` loads shaders, textures, and meshes by name
- Resources are cached — loading the same name returns the existing handle
- Components store OpenGL handles (unsigned int IDs), not resource objects
- `resources.clear()` frees everything at shutdown

---

## Key Files

| File | Lines | Purpose |
|------|-------|---------|
| `main.cpp` | ~230 | Game loop, input, camera, system orchestration |
| `components.h` | ~260 | Every component and tag definition |
| `scene_setup.cpp` | ~370 | Entity spawning (player, lights, demos, triggers) |
| `physics/bodies/*` | ~190 | 5 Jolt body-creation functions (static/dynamic/kinematic/sensor/level) |
| `player_character_system.cpp` | ~165 | Player movement + CharacterVirtual |
| `combat_system.cpp` | ~300 | Weapon firing, hitscan, projectiles |
| `render_system.cpp` | ~150 | OpenGL draw calls + lighting |

---

## What's Implemented (Through Chapter 20)

- Window creation, OpenGL context, GLFW input
- Shader system (basic, textured, lit, HUD)
- Texture loading (stb_image)
- OBJ mesh loading (in-repo `renderer/obj_loader.cpp`)
- ECS entity/component/system architecture (EnTT)
- Data-driven entity spawning (`classname`→factory dispatch + two-pass `targetname` linking)
- FPS camera with mouse look + fixed-timestep interpolation
- Level geometry (sectors, surfaces, BSP-style)
- Phong lighting (directional + point lights)
- Jolt Physics (static, dynamic, kinematic bodies + CharacterVirtual)
- Player movement (Quake-style acceleration, CharacterVirtual)
- Doors and lifts (kinematic movers with state machine + start delay)
- Trigger volumes (activate movers, teleport, damage, heal)
- Item pickups (health, ammo, armour, weapons) + armour bar
- Weapons — all 7 wired to a fixed 7-slot inventory (keys 1-7 = weapon type); shotgun + rocket launcher to start, the rest collected; each draws from its own ammo pool (shells/nails/rockets/cells)
- Player death / respawn (`player_death_system`, in the tick order)
- Graphical HUD (FPS, health/armour bars, ammo, crosshair, damage flash, pickup toast, weapon bar showing owned/available slots 1-7)
- First-person weapon viewmodel (procedural per-weapon gun shapes + colours; idle bob, fire recoil, switch drop/raise). Weapon pickups render as the same coloured gun models.
- Audio (miniaudio + stb_vorbis: SFX + looping music via `SoundQueue`/`audioSystem`)
- Demo reset system (periodic physics object respawn)
- Enemies — `monster_grunt` archetype (kinematic body, blocks the player, shootable, white hit-flash + hit/death sounds, dies via `enemyDeathSystem`) with `aiSystem` behaviour: line-of-sight sensing/aggro, **A\* pathfinding** (routes around walls/props via a `NavGrid`), and melee attack.

## What's Not Yet Implemented

- Networking
- Menus / game states (boots straight into gameplay)
- TrenchBroom level loading (levels are hard-coded)
- BSP traversal
