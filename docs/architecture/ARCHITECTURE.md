# QEngine — Architecture Reference

Updated through Chapter 15d. See [COMPONENTS.md](COMPONENTS.md), [SYSTEMS.md](SYSTEMS.md), and [TICK_ORDER.md](TICK_ORDER.md) for detailed breakdowns.

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
        ├── ecs/
        │   ├── components.h         #   ALL component definitions (single file)
        │   ├── scene_setup.h/.cpp   #   Entity spawning (setupScene)
        │   ├── jolt_body_helpers.h/.cpp  # Jolt body creation functions
        │   ├── showcase_level.h/.cpp    # Hardcoded showcase room geometry
        │   ├── weapon_definitions.h #   createWeapon() factory (inline)
        │   └── systems/
        │       ├── player_character_system.h/.cpp  # Player movement via CharacterVirtual
        │       ├── mover_system.h/.cpp             # Door/lift state machine
        │       ├── mover_sync_system.h/.cpp        # Push mover positions to Jolt
        │       ├── jolt_sync_system.h/.cpp          # Read Jolt transforms to ECS
        │       ├── combat_system.h/.cpp             # Weapons: hitscan + projectiles
        │       ├── trigger_system.h/.cpp            # Trigger volume overlap detection
        │       ├── lifetime_system.h/.cpp           # Auto-destroy timed entities
        │       ├── demo_reset_system.h/.cpp         # Reset physics demo objects
        │       ├── render_system.h/.cpp             # OpenGL rendering + lighting
        │       ├── debug_hud_system.h/.cpp          # Text HUD overlay
        │       ├── weapon_switch_system.h           # Weapon switching (inline)
        │       ├── system_phase.h                   # Phase documentation enum
        │       └── archived/                        # Dead code from pre-Jolt era
        │           ├── collision_system.h/.cpp
        │           ├── physics_system.h/.cpp
        │           ├── movement_system.h/.cpp
        │           └── player_movement_system.h/.cpp
        ├── physics/
        │   ├── jolt_setup.h         #   Jolt layers, filters, callbacks
        │   ├── jolt_world.h/.cpp    #   JoltWorld wrapper (init, step, shutdown)
        │   ├── physics_config.h     #   PhysicsConfig (fixedDeltaTime, terminalVelocity)
        │   ├── aabb.h               #   AABB struct + intersection
        │   ├── collision.h          #   sweepAABB function
        │   ├── collision_layers.h   #   Legacy collision layer bitmasks
        │   ├── raycast.h/.cpp       #   Ray-AABB intersection
        │   └── spatial_hash.h       #   Spatial hash grid (legacy, used by archived collision_system)
        ├── renderer/
        │   ├── camera.h/.cpp        #   FPS camera (position, orientation, matrices)
        │   ├── shader.h/.cpp        #   Shader compilation + uniform helpers
        │   ├── texture.h/.cpp       #   Texture loading (stb_image)
        │   ├── mesh.h/.cpp          #   VAO/VBO/EBO management
        │   ├── obj_loader.h/.cpp    #   OBJ file parser
        │   └── stb_image_impl.cpp   #   stb_image implementation unit
        ├── level/
        │   ├── level.h              #   Level, Sector, Surface structs
        │   └── level_loader.h/.cpp  #   buildSectorMeshes (LEGACY .qlvl path — see note below)
        └── app/
            └── simulation.h/.cpp    #   Shared buildWorld + stepSimulation (windowed + headless)
```

---

## Design Principles

### ECS Architecture (EnTT)
- **Components** are plain data structs — no methods, no inheritance
- **Systems** are free functions — no state, no member variables
- **Registry** is the single source of truth — all game state lives here
- **Context objects** (`registry.ctx()`) store singletons: `PhysicsConfig`, `JoltWorld`, `HudConfig`, `CombatResources`

### Physics (Jolt)
- **Static bodies** for level geometry and immovable platforms
- **Dynamic bodies** for physics objects (cubes)
- **Kinematic bodies** for movers (doors, lifts) — pushed via `MoveKinematic`
- **CharacterVirtual** for the player — direct velocity control with collision response
- **Sensor bodies** for trigger volumes — overlap detection without blocking
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
| `jolt_body_helpers.cpp` | ~190 | 5 Jolt body creation functions |
| `player_character_system.cpp` | ~165 | Player movement + CharacterVirtual |
| `combat_system.cpp` | ~300 | Weapon firing, hitscan, projectiles |
| `render_system.cpp` | ~150 | OpenGL draw calls + lighting |

---

## What's Implemented (Through Chapter 15d)

- Window creation, OpenGL context, GLFW input
- Shader system (basic, textured, lit, HUD)
- Texture loading (stb_image)
- OBJ mesh loading (tinyobjloader)
- ECS entity/component/system architecture (EnTT)
- FPS camera with mouse look
- Level geometry (sectors, surfaces, BSP-style)
- Phong lighting (directional + point lights)
- Jolt Physics (static, dynamic, kinematic, sensor bodies)
- Player movement (Quake-style acceleration, CharacterVirtual)
- Doors and lifts (kinematic movers with state machine + start delay)
- Trigger volumes (activate movers, teleport, damage, heal)
- Weapons (shotgun hitscan, rocket launcher projectile)
- Player death / respawn (`player_death_system`, in the tick order)
- Debug HUD (FPS, position, health, ammo)
- Demo reset system (periodic physics object respawn)

## What's Not Yet Implemented

- AI / enemies
- Audio
- Networking
- Crosshair / expanded HUD
- TrenchBroom level loading
- BSP traversal
