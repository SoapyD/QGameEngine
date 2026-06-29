# QEngine Tutorial — Claude Memory Reference

## Project Overview
- **What**: A ground-up 3D Quake-style FPS engine tutorial in C++
- **Architecture**: ECS (Entity Component System) — this is the central design principle
- **ECS Library**: EnTT (header-only, C++17)
- **Core ECS Rule**: Components have no behaviour, systems have no state
- **Engine Name**: QEngine

## File Locations
- **Tracking docs**: `D:\Documents\AI\documents\Game Learning\Game Engine Docs\`
  - `ROADMAP.md` — 21-chapter plan, dependency graph, progress tracker
  - `ARCHITECTURE.md` — project structure, component registry, system tick order
  - `CPP_CONCEPTS_BY_CHAPTER.md` — which C++ features are taught when
- **Tutorial chapters**: `D:\Documents\AI\documents\Game Learning\Game Engine Tutorial\`
  - Chapter_00 through Chapter_20 — ALL COMPLETE
- **ECS overview**: `D:\Documents\AI\documents\Game Learning\README.md`
  - Compares id-style, McShaffry event-driven, and ECS approaches

## Progress
- **Phase 1 (Foundation)**: Ch 0-3 — COMPLETE (setup, window, shaders, ECS intro)
- **Phase 2 (3D Rendering)**: Ch 4-7 — COMPLETE (transforms, textures, meshes, lighting)
- **Phase 3 (Game World)**: Ch 8-11 — COMPLETE (levels, collision, physics, triggers)
- **Phase 4 (Gameplay)**: Ch 12-15 — COMPLETE (weapons, items, AI, HUD)
- **Phase 5 (Audio)**: Ch 16 — COMPLETE (miniaudio, 3D positional audio, AudioManager)
- **Phase 6 (Multiplayer)**: Ch 17-19 — COMPLETE (ENet networking, snapshots, delta compression, prediction, lag compensation)
- **Phase 7 (Polish)**: Ch 20 — COMPLETE (particles, screen shake, view bob, interpolation, final render order)

**ALL 21 CHAPTERS COMPLETE.**

## Tutorial Series 2 — Nice-to-Haves (Chapters 21-27)
- **Location**: `D:\Documents\AI\documents\Game Learning\Game_Engine_Tutorial_2\`
- **Phase A (Game States & Menus)**: Ch 21-22 — COMPLETE (GameState stack, pending operations, MainMenu, Pause, Loading, Settings, GameOver states)
- **Phase B (Save/Load)**: Ch 23 — COMPLETE (nlohmann/json, to_json/from_json ADL, SaveSystem class, entity serialisation)
- **Phase C (Skybox)**: Ch 24 — COMPLETE (cubemap textures, skybox shader, depth trick, view matrix translation stripping)
- **Phase D (Extended Content)**: Ch 25-26 — COMPLETE (view models with separate FOV render pass, keyframe animation, procedural bob/recoil/sway; boss fights with multi-phase AI, attack patterns, arena lockdown)
- **Phase E (Dev Console)**: Ch 27 — COMPLETE (console with command registration, GLFW char callbacks, debug wireframe rendering with GL_LINES)

**ALL 7 NICE-TO-HAVE CHAPTERS COMPLETE.**

## Tutorial Series 2b — Engine Completeness (Chapters 28-34)
- **Location**: `D:\Documents\AI\documents\Game Learning\Game_Engine_Tutorial_2\`
- **Phase F (Rendering Upgrades)**: Ch 28-29 — COMPLETE (FBOs, post-processing bloom/vignette/damage flash, shadow mapping with PCF, shadow acne fixes, cascaded shadow maps concept)
- **Phase G (Text & Visual Effects)**: Ch 30-31 — COMPLETE (FreeType font rendering, bitmap font atlas, glyph metrics, text batching; decals with projected quads, texture atlas, fade/lifetime)
- **Phase H (Performance)**: Ch 32 — COMPLETE (frustum culling, plane extraction from VP matrix, AABB-frustum test, p-vertex optimization)
- **Phase I (Animation & World)**: Ch 33-34 — COMPLETE (skeletal animation with bone hierarchy, skinning shader, animation blending, quaternion slerp; level transitions with persistent player state, registry clearing, spawn points)

**ALL 14 NICE-TO-HAVE CHAPTERS COMPLETE (21-34).**

## Tutorial Series 2c — Advanced Features (Chapters 35-39)
- **Location**: `D:\Documents\AI\documents\Game Learning\Game_Engine_Tutorial_2\`
- **Phase J (Surface Detail)**: Ch 35 — COMPLETE (tangent space, TBN matrix, normal map textures, parallax mapping)
- **Phase K (Asset Pipeline)**: Ch 36 — COMPLETE (OBJ parser, MTL materials, glTF/tinygltf overview, AssetManager caching)
- **Phase L (AI Navigation)**: Ch 37 — COMPLETE (A* algorithm, NavGrid, NavMesh, funnel algorithm, PathFollower component)
- **Phase M (Rendering Performance)**: Ch 38 — COMPLETE (glDrawElementsInstanced, instance buffers, glVertexAttribDivisor, frustum-culled instancing)
- **Phase N (Environmental Effects)**: Ch 39 — COMPLETE (planar reflections, refraction FBOs, dudv distortion, Fresnel effect, underwater post-processing, swimming mechanics)

**ALL 19 NICE-TO-HAVE CHAPTERS COMPLETE (21-39).**

## Tutorial Series 2d — Animation & Rendering Polish (Chapters 40-44)
- **Location**: `D:\Documents\AI\documents\Game Learning\Game_Engine_Tutorial_2\`
- **Phase O (Animation Polish)**: Ch 40-42 — COMPLETE (animation events/notifies with frame-triggered callbacks and event dispatch; ragdoll physics with bone-to-rigidbody mapping, joint constraints, death transitions; animation layers with bone masks, override/additive blending, partial body animation)
- **Phase P (Advanced Rendering & Animation)**: Ch 43-44 — COMPLETE (inverse kinematics with two-bone IK solver, foot placement, CCD algorithm, look-at; PBR materials with Cook-Torrance BRDF, metallic-roughness workflow, IBL, HDR tone mapping)

**ALL 24 NICE-TO-HAVE CHAPTERS COMPLETE (21-44).**

## Tutorial Series 2e — Advanced Particles (Chapters 45-46)
- **Location**: `D:\Documents\AI\documents\Game Learning\Game_Engine_Tutorial_2\`
- **Phase Q (Advanced Particles)**: Ch 45-46 — COMPLETE (particle-world collision with bounce/friction, drag/wind/turbulence forces, flipbook texture animation, trails/ribbons, soft particles; data-driven JSON effect definitions, sub-emitters, colour/size curves, effect library with blood/fire/smoke/sparks/explosions/rocket trails, hot-reload)

**ALL 26 NICE-TO-HAVE CHAPTERS COMPLETE (21-46).**

## Additional Roadmaps
- `ROADMAP_NICE_TO_HAVES.md` — Chapters 21-46 (all written)
- `ROADMAP_TRENCHBROOM.md` — 5-phase TrenchBroom integration (.map parser, FGD, brush collision)
- `ROADMAP_TOP_DOWN_SHOOTER.md` — 7-phase genre adaptation (camera, input, 2D collision, weapons, levels, AI, polish)
- `ROADMAP_MULTIPLAYER_INFRASTRUCTURE.md` — 5-part multiplayer (co-op campaign, lobbies, QMaster server, NAT traversal, game modes)

## Tech Stack
| Purpose | Library |
|---|---|
| ECS | EnTT |
| Windowing/Input | GLFW |
| OpenGL Loading | GLAD |
| Math | GLM |
| Image Loading | stb_image |
| Audio (later) | miniaudio |
| Networking (later) | ENet |
| Build System | CMake |

## What IS ECS in This Project
- All game entities (player, enemies, doors, lifts, lights, triggers, projectiles) are EnTT entities
- Components: Position, Velocity, Health, MeshRenderer, AABBCollider, Gravity, OnGround, CharacterPhysics, PlayerInput, DirectionalLight, PointLight, Mover, TriggerVolume, TagPlayer, etc.
- Systems: free functions that take `entt::registry&` and query with `registry.view<>()`

## What is NOT ECS
- Window, Camera — engine infrastructure (Camera could become a component later)
- Shader, Texture, Mesh — shared GPU resources, referenced by component IDs
- Level (sectors, surfaces, portals) — static world geometry, queried by systems but not entities
- SpatialHash — acceleration structure rebuilt each frame

## Key Design Decisions
- Fixed timestep for physics (60 ticks/sec)
- Quake-style movement (separate ground/air acceleration, enables bunny hopping)
- Sector/portal level format (simplified BSP)
- Phong lighting with lightmap concepts
- Collision layers via bitmasks
- State machines for interactive objects (doors, lifts)
