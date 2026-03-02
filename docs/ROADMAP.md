# Quake-Style ECS Game Engine Tutorial — Roadmap

## Project Name: **QEngine**

A ground-up 3D FPS engine in C++ using ECS architecture, inspired by Quake.

---

## Tech Stack

| Purpose | Library | Why |
|---|---|---|
| ECS | EnTT | Header-only, modern C++, industry-respected |
| Windowing/Input | GLFW | Cross-platform, lightweight, standard |
| OpenGL Loading | GLAD | Generates only what you need |
| Math | GLM | Mirrors GLSL syntax, header-only |
| Physics | Jolt Physics | Modern C++17, multi-threaded, used in AAA games |
| Image Loading | stb_image | Single header, no dependencies |
| Model Loading | Assimp (later) or tinyobjloader | Start simple, upgrade later |
| Audio | miniaudio | Single header, cross-platform |
| Networking | ENet | Reliable UDP, proven in games |
| Build System | CMake | Industry standard |

---

## Chapter Outline

### Phase 1: Foundation (Chapters 0-3)
Getting a window open, triangles on screen, and the ECS wired up.

| Ch | Title | Key Concepts |
|----|-------|-------------|
| 0 | Dev Environment Setup | CMake, vcpkg/manual deps, project structure |
| 1 | Window & OpenGL Context | GLFW, GLAD, OpenGL core profile, game loop |
| 2 | Shader System | GLSL, vertex/fragment shaders, shader class |
| 3 | ECS Foundation | EnTT, entities, components, systems, queries |

**Milestone:** Coloured triangle rendered via ECS.

---

### Phase 2: 3D Rendering (Chapters 4-7)
Going from triangles to a textured 3D world you can walk around in.

| Ch | Title | Key Concepts |
|----|-------|-------------|
| 4 | 3D Transforms & Camera | Model/View/Projection, GLM, FPS camera |
| 5 | Textures & Materials | stb_image, UV mapping, texture units |
| 6 | Mesh & Model Loading | Vertex data, index buffers, OBJ loading |
| 7 | Lighting | Phong/Blinn-Phong, normals, point/directional lights |

**Milestone:** Textured, lit 3D scene with FPS camera movement.

---

### Phase 3: Game World (Chapters 8-11)
Building out the level and interacting with it.

| Ch | Title | Key Concepts |
|----|-------|-------------|
| 8 | Level Geometry & BSP | BSP concepts, level loading, PVS (simplified) |
| 9 | Collision Detection | AABB, raycasting, swept collision, spatial hashing |
| 10 | Physics & Movement | Gravity, friction, Quake-style air control, stair stepping |
| 11 | Doors, Lifts & Triggers | Entity interactions, trigger volumes, state machines |

**Milestone:** Walkable level with collision, physics, and interactive elements.

---

### Phase 4: Player & Gameplay (Chapters 12-15)
Giving the player a real body, integrating proper physics, and turning the engine into a playable game.

| Ch | Title | Key Concepts |
|----|-------|-------------|
| 12 | Weapons & Projectiles | Hitscan, projectile entities, damage system |
| 13 | Player Body & Debug HUD | Physical player body, reversed camera sync, input→velocity pipeline, health clamping, stair-stepping, stb_easy_font debug overlay |
| 14a | Jolt Physics — CMake, Boilerplate & World Wrapper | CMake FetchContent, Jolt setup/boilerplate (layers, filters, callbacks), JoltWorld wrapper |
| 14b | Jolt Physics — Bodies, Sync & Wiring | JoltBody component, static bodies from level surfaces, dynamic bodies, jolt sync system, main.cpp wiring, CMakeLists updates |
| 15a | Jolt Character Controller — CharacterVirtual | CharacterVirtual vs Character, JoltCharacter component, player_character_system with Quake-style acceleration |
| 15b | Jolt Kinematic Movers & Sensor Bodies | Kinematic bodies for lifts/doors, MoveKinematic vs SetPosition, sensor bodies for triggers |
| 15c | Jolt Integration — Wiring & Architecture Review | main.cpp tick order, CMakeLists updates, old vs new architecture comparison |

**Milestone:** Player with Jolt-powered physics — solid collision, lifts that carry the player, no jitter on resting contact. Debug HUD showing health and FPS.

---

### Phase 5: Test Objects & Gameplay (Chapters 16-20)
Building gameplay content and test scenarios to exercise the physics system.

| Ch | Title | Key Concepts |
|----|-------|-------------|
| 16 | Gameplay Polish | Crosshair, expanded HUD (ammo/weapon), death/respawn, damage feedback, knockback |
| 17 | Platforming Arena | Floating platforms, variable jump height, coyote time, low-gravity tuning |
| 18 | Physics Puzzles & Props | Pushable objects, stacking, pressure plates, physics-driven doors |
| 19 | Combat Arena | Enemy placement, health pickups, ammo crates, arena design |
| 20 | Integration Test Level | Complete playable level exercising all features end-to-end |

**Milestone:** Fully playable hardcoded level with platforming, combat, physics puzzles, and all engine features working together.

---

### Phase 6: TrenchBroom Integration (Chapters 21-25)
Replacing the hardcoded level with a proper level editor workflow.

| Ch | Title | Key Concepts |
|----|-------|-------------|
| 21 | .map Parser & Brush Rendering | TrenchBroom .map format, plane→polygon→triangle conversion, brush-to-mesh, texture mapping |
| 22 | Entity Mapping & FGD | FGD entity definitions, classname→ECS factory functions, point vs brush entities |
| 23 | Brush Collision | Collision from brush planes, Jolt static bodies from brushes, spatial optimization |
| 24 | TrenchBroom Config & Workflow | GameConfig.cfg, texture browser integration, hot reload, test level construction |
| 25 | Final Level & Integration | Complete playable TrenchBroom level exercising all features: rooms, doors, lifts, lava, platforms, weapons, pickups, lighting |

**Milestone:** Fully playable level authored in TrenchBroom with all engine features working end-to-end.

---

## Dependency Graph

```
Ch 0 (Setup)
  └── Ch 1 (Window)
       └── Ch 2 (Shaders)
            ├── Ch 3 (ECS)
            │    └── ALL subsequent chapters depend on this
            └── Ch 4 (3D Transforms)
                 ├── Ch 5 (Textures)
                 │    └── Ch 6 (Models)
                 │         └── Ch 7 (Lighting)
                 │              └── Ch 8 (Levels/BSP)
                 │                   └── Ch 9 (Collision)
                 │                        └── Ch 10 (Physics)
                 │                             ├── Ch 11 (Triggers)
                 │                             └── Ch 12 (Weapons)
                 │                                  └── Ch 13 (Player Body)
                 │                                       └── Ch 14a (Jolt Setup)
                 │                                            └── Ch 14b (Jolt Bodies)
                 │                                                 └── Ch 15a (Character Controller)
                 │                                                      └── Ch 15b (Kinematic & Sensors)
                 │                                                           └── Ch 15c (Integration)
                 │                                                                └── Ch 16 (Gameplay Polish)
                 │                                                                     └── Ch 17 (Platforming)
                 │                                                                          └── Ch 18 (Physics Puzzles)
                 │                                                                               └── Ch 19 (Combat Arena)
                 │                                                                                    └── Ch 20 (Test Level)
                 │                                                                                         └── Ch 21 (.map Parser)
                 │                                                                                              └── Ch 22 (Entity Mapping)
                 │                                                                                                   └── Ch 23 (Brush Collision)
                 │                                                                                                        └── Ch 24 (TB Config)
                 │                                                                                                             └── Ch 25 (Final Level)
```

---

## Progress Tracker

| Chapter | Status | Notes |
|---------|--------|-------|
| 0 | Complete | Dev environment, CMake, dependencies |
| 1 | Complete | GLFW window, OpenGL context, game loop, delta time |
| 2 | Complete | GLSL shaders, Shader class, first triangle |
| 3 | Complete | EnTT, components, systems, triangle as entity |
| 4 | Complete | Model/View/Projection, FPS camera, WASD + mouse look |
| 5 | Complete | stb_image, Texture class, UV coords, filtering, tiling |
| 6 | Complete | Vertex struct, Mesh class, OBJ loader, index buffers, procedural meshes |
| 7 | Complete | Phong lighting, directional/point lights, light components, lightmap concepts |
| 8 | Complete | Sectors, surfaces, portals, level loading, simplified BSP, backface culling |
| 9 | Complete | AABB, raycasting, swept collision, spatial hashing, Minkowski difference |
| 10 | Complete | Gravity, friction, fixed timestep, jumping, Quake air control, stair stepping |
| 11 | Complete | State machines, trigger volumes, doors, lifts, teleporters, damage zones |
| 12 | Complete | Hitscan/projectile weapons, splash damage, rocket jumping, weapon switching |
| 13 | Complete | Physical player body, reversed camera sync, input→velocity, health clamping, stb_easy_font debug HUD |
| 14a | Written | Jolt CMake FetchContent, boilerplate (layers, filters), JoltWorld wrapper |
| 14b | Written | JoltBody component, static/dynamic bodies, jolt sync system, main.cpp wiring |
| 15a | Written | CharacterVirtual player controller, Quake-style acceleration |
| 15b | Written | Kinematic movers (lifts/doors), sensor bodies (triggers) |
| 15c | Written | main.cpp integration, tick order, CMakeLists, architecture review |
| 16 | Planned | Crosshair, expanded HUD, death/respawn, damage feedback |
| 17 | Planned | Floating platforms, variable jump, coyote time, platforming test arena |
| 18 | Planned | Pushable objects, pressure plates, physics-driven doors |
| 19 | Planned | Enemy placement, health/ammo pickups, combat arena |
| 20 | Planned | Complete integration test level |
| 21 | Planned | TrenchBroom .map parser, plane→polygon conversion, brush-to-mesh rendering |
| 22 | Planned | FGD entity definitions, classname→ECS factory mapping |
| 23 | Planned | Brush collision with Jolt static bodies |
| 24 | Planned | TrenchBroom GameConfig.cfg, texture browser, hot reload workflow |
| 25 | Planned | Complete playable TrenchBroom level with all features integrated |
