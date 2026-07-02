# QEngine: Building a Quake-Style FPS from Scratch

A complete, hands-on tutorial series that takes you from an empty C++ project to a fully-featured first-person shooter engine. Across 46 chapters (plus 9 refactoring checkpoints), you'll build **QEngine** — a Quake-inspired game engine using modern C++, OpenGL, and an Entity Component System (EnTT).

## What You'll Be Able to Build

By the end of this series, you'll have a working engine capable of producing a game like this:

```
┌─────────────────────────────────────────────────────────────────┐
│  HEALTH: 85                                    AMMO: 24/120    │
│  ┌─────┐                                                       │
│  │ MAP │   You're in a dimly lit corridor. Torches flicker on  │
│  └─────┘   the walls (point lights, Chapter 7). The stone      │
│            textures show every crack and groove thanks to       │
│            normal mapping (Ch 35) and PBR materials (Ch 44).   │
│                                                                 │
│   An enemy patrol rounds the corner — pathfinding brought      │
│   them here (Ch 37). They spot you and open fire. Your         │
│   screen shakes (Ch 20), damage flash tints the view red       │
│   (Ch 28), and the HUD health counter drops (Ch 15).          │
│                                                                 │
│   You fire the shotgun. The view model kicks back with         │
│   spring physics (Ch 25), a muzzle flash particle burst        │
│   lights up the hallway (Ch 46), shell casings tumble out      │
│   with ragdoll physics (Ch 41), and bullet decals stamp        │
│   the far wall (Ch 31). The enemy dies — their skeleton        │
│   crumples into a ragdoll that drapes over a railing.          │
│                                                                 │
│   You grab a health pickup (Ch 13), ride a lift (Ch 11),      │
│   and step through a portal to the next level (Ch 34).        │
│   A loading screen fades in, the new map streams in, and       │
│   you're in a boss arena with a multi-phase encounter          │
│   (Ch 26). Water pools reflect the skybox (Ch 39, Ch 24),     │
│   shadows shift in real-time (Ch 29), and hundreds of          │
│   instanced torches light the arena (Ch 38).                   │
│                                                                 │
│   You pause (Ch 22), tweak your mouse sensitivity in the       │
│   settings menu, quicksave (Ch 23), and dive back in.          │
│                          ┌───────┐                              │
│                          │SHOTGUN│  <- view model (Ch 25)      │
│                          └───────┘                              │
└─────────────────────────────────────────────────────────────────┘
```

Every system in that scene is something you build yourself, from the ground up, with full source code and explanations of **why** each design decision was made.

## What You'll Learn

- **Engine architecture** — how to structure a real game engine using ECS, not inheritance hierarchies
- **Graphics programming** — OpenGL rendering, shaders, lighting, shadows, post-processing, PBR, normal mapping
- **Physics** — collision detection, movement, ragdolls, fixed timestep simulation
- **Animation** — skeletal animation, IK, animation layers, events, blend trees
- **Gameplay systems** — weapons, AI, triggers, pickups, boss fights, save/load
- **Networking** — client-server architecture, state synchronisation, client-side prediction
- **Audio** — 3D positional sound, ambient loops, audio manager
- **Particles** — physics-driven particles, trails, force fields, data-driven effects
- **UI** — HUD, menus, font rendering, developer console
- **Tools** — ImGui debug UI, level editor, Lua scripting, asset pipeline
- **Professional practices** — regular refactoring, resource management, data-driven design, profiling

## Course Structure

The series is split into **main chapters** that build features and **cleanup chapters** (marked with "a" suffix) that refactor accumulated code debt every 5 chapters. This mirrors real development — ship features, then pay down tech debt before it compounds.

```
Ch 0-5   → Foundation        → 5a cleanup
Ch 6-10  → World & Physics   → 10a cleanup
Ch 11-15 → Gameplay & HUD    → 15a cleanup
Ch 16-20 → Audio & Polish    → 20a cleanup
Ch 21-25 → States & Anims    → 25a cleanup
Ch 26-30 → Rendering         → 30a cleanup
Ch 31-35 → Visual Fidelity   → 35a cleanup
Ch 36-40 → Advanced Systems  → 40a cleanup
Ch 41-45 → Animation & PBR   → 45a cleanup
Ch 46    → Data-Driven Particles
Ch 47-50 → Tools & Scripting → 50a cleanup
Ch 51-55 → Production Render → 55a cleanup
```

---

## Part 1: Foundation (Chapters 0-5)

| # | Chapter | Synopsis |
|---|---------|----------|
| 0 | [Dev Environment Setup](Game%20Engine%20Tutorial/Chapter_00_Dev_Environment_Setup.md) | Install a C++ compiler, CMake, and dependencies. Set up the project structure for QEngine. Covers how compilation and linking work. |
| 1 | [Window & OpenGL Context](Game%20Engine%20Tutorial/Chapter_01_Window_And_OpenGL.md) | Create a window with GLFW, initialise OpenGL with GLAD, build the game loop, handle keyboard input, and measure delta time. |
| 2 | [Shader System](Game%20Engine%20Tutorial/Chapter_02_Shader_System.md) | Learn how the GPU rendering pipeline works. Write vertex and fragment shaders in GLSL, compile and link them, and render your first triangle. Build a reusable Shader class. |
| 3 | [ECS Foundation](Game%20Engine%20Tutorial/Chapter_03_ECS_Foundation.md) | Introduce EnTT as the backbone of the engine. Define components as plain data structs, create entities, write systems, and refactor the triangle to use ECS. |
| 4 | [3D Transforms & Camera](Game%20Engine%20Tutorial/Chapter_04_3D_Transforms_And_Camera.md) | Model-View-Projection matrices, 3D positioning and rotation, and building an FPS camera with mouse look and WASD movement. |
| 5 | [Textures & Materials](Game%20Engine%20Tutorial/Chapter_05_Textures_And_Materials.md) | Load images with stb_image, map them onto geometry with UV coordinates, texture filtering and wrapping, and build a Texture class. |
| **5a** | [Foundation Cleanup](Game%20Engine%20Tutorial/Chapter_05a_Foundation_Cleanup.md) | Extract mesh setup into MeshFactory, build ResourceManager for caching textures/shaders, create InputManager to encapsulate GLFW input, and move entity creation into setupScene(). |

## Part 2: World & Physics (Chapters 6-10)

| # | Chapter | Synopsis |
|---|---------|----------|
| 6 | [Mesh & Model Loading](Game%20Engine%20Tutorial/Chapter_06_Mesh_And_Model_Loading.md) | Load 3D models from OBJ files. Vertex data, index buffers, and a Mesh class for managing GPU geometry. |
| 7 | [Lighting](Game%20Engine%20Tutorial/Chapter_07_Lighting.md) | The Phong lighting model — ambient, diffuse, and specular. Directional lights, point lights, writing lit shaders, and Quake-style lightmaps. |
| 8 | [Level Geometry & BSP](Game%20Engine%20Tutorial/Chapter_08_Level_Geometry_And_BSP.md) | How Quake represents its world with brushes, planes, and BSP trees. Build level geometry and understand spatial partitioning. |
| 9 | [Collision Detection](Game%20Engine%20Tutorial/Chapter_09_Collision_Detection.md) | AABB collision, ray casting, swept collision, spatial hashing for efficient broadphase, and integrating it all into the ECS. |
| 10 | [Physics & Movement](Game%20Engine%20Tutorial/Chapter_10_Physics_And_Movement.md) | Gravity, friction, terminal velocity, fixed timestep physics, jumping, ground detection, Quake-style air control, bunny hopping, and stair stepping. |
| **10a** | [Game Loop & Physics Cleanup](Game%20Engine%20Tutorial/Chapter_10a_Game_Loop_And_Physics_Cleanup.md) | Extract FixedTimestep class, centralise physics constants into PhysicsConfig (stored as registry context), formalise SystemPhase enum, and introduce CollisionLayers. |

## Part 3: Gameplay (Chapters 11-15)

| # | Chapter | Synopsis |
|---|---------|----------|
| 11 | [Doors, Lifts & Triggers](Game%20Engine%20Tutorial/Chapter_11_Doors_Lifts_And_Triggers.md) | State machines for interactive objects, trigger volumes, doors that open/close, lifts that carry the player, and timed event sequences. |
| 12 | [Weapons & Projectiles](Game%20Engine%20Tutorial/Chapter_12_Weapons_And_Projectiles.md) | Hitscan weapons (shotguns, railguns), projectile weapons (rockets, grenades), weapon switching, cooldowns, and damage dealing through ECS. |
| 13 | [Items & Pickups](Game%20Engine%20Tutorial/Chapter_13_Items_And_Pickups.md) | Health packs, ammo crates, armour, and powerups. Pickup components, collision-based collection, and respawn timers. |
| 14 | [Enemy AI](Game%20Engine%20Tutorial/Chapter_14_Enemy_AI.md) | AI state machines (idle, patrol, chase, attack, retreat), line-of-sight checks, hearing detection, and integrating AI with the combat system. |
| 15 | [HUD & UI](Game%20Engine%20Tutorial/Chapter_15_HUD_And_UI.md) | Rendering health bars, ammo counters, crosshairs, and damage indicators. Orthographic rendering for 2D overlays on top of the 3D world. |
| **15a** | [HUD & Effects Cleanup](Game%20Engine%20Tutorial/Chapter_15a_HUD_And_Effects_Cleanup.md) | Move HUD state into ECS components (DamageFlash, HUDMessages, CrosshairStyle), extract HUDRenderer and TextRenderer utility classes, add hudUpdateSystem(). |

## Part 4: Audio, Networking & Polish (Chapters 16-20)

| # | Chapter | Synopsis |
|---|---------|----------|
| 16 | [Audio System](Game%20Engine%20Tutorial/Chapter_16_Audio_System.md) | 3D positional audio with miniaudio. Sound effects, looping ambient sounds, an AudioManager, and audio components in the ECS. |
| 17 | [Networking Foundation](Game%20Engine%20Tutorial/Chapter_17_Networking_Foundation.md) | Client-server architecture over UDP with ENet. Packet serialisation, the server/client tick split, connection handling, and basic message passing. |
| 18 | [State Synchronisation](Game%20Engine%20Tutorial/Chapter_18_State_Synchronisation.md) | Server snapshots, delta compression, client-side interpolation between snapshots, network entity management, and handling packet loss. |
| 19 | [Client-Side Prediction](Game%20Engine%20Tutorial/Chapter_19_Client_Side_Prediction.md) | Input buffering and replay, server reconciliation for correcting mispredictions, lag compensation for shooting, and the complete netcode pipeline. |
| 20 | [Particles, Effects & Polish](Game%20Engine%20Tutorial/Chapter_20_Particles_Effects_And_Polish.md) | Object-pooled particle system, billboard rendering, screen shake, view bobbing, weapon recoil, muzzle flashes, explosions, and wall sparks. |
| **20a** | [Polish Systems Cleanup](Game%20Engine%20Tutorial/Chapter_20a_Polish_Systems_Cleanup.md) | Convert ScreenShake/ViewBob/WeaponRecoil to ECS components, introduce ParticleEmitterDef for data-driven emitters, and create MathUtils namespace (lerp, smoothstep, spring, etc.). |

## Part 5: Game States & Animation (Chapters 21-25)

| # | Chapter | Synopsis |
|---|---------|----------|
| 21 | [Game State Machine](Game_Engine_Tutorial_2/Chapter_21_Game_State_Machine.md) | Design a state stack for managing game modes (menu, playing, paused, game over). Control which ECS systems tick per state, with enter/exit hooks. |
| 22 | [Main Menu & Pause Screen](Game_Engine_Tutorial_2/Chapter_22_Main_Menu_And_Pause_Screen.md) | Build a main menu with keyboard navigation, a pause overlay, a settings screen for volume/sensitivity/resolution, and loading transitions. |
| 23 | [Save & Load System](Game_Engine_Tutorial_2/Chapter_23_Save_And_Load_System.md) | Serialise ECS entities and components to JSON. Handle resource references, stable entity IDs across save/load, save slots with metadata. |
| 24 | [Skybox](Game_Engine_Tutorial_2/Chapter_24_Skybox.md) | Cubemap textures, a unit cube rendered with stripped translation, the depth trick for always-behind rendering, and a reusable Skybox class. |
| 25 | [Weapon Animations & View Models](Game_Engine_Tutorial_2/Chapter_25_Weapon_Animations_And_View_Models.md) | Separate-FOV weapon rendering, keyframe animation for fire/reload/switch, procedural view bob, recoil spring physics, and idle sway. |
| **25a** | [Animation & Assets Cleanup](Game_Engine_Tutorial_2/Chapter_25a_Animation_And_Assets_Cleanup.md) | Load WeaponAnimations from JSON via AnimationLibrary, extend ResourceManager into a full AssetCache, and introduce WeaponEffectConfig for per-weapon tuning. |

## Part 6: Advanced Rendering (Chapters 26-30)

| # | Chapter | Synopsis |
|---|---------|----------|
| 26 | [Boss Fights & Arenas](Game_Engine_Tutorial_2/Chapter_26_Boss_Fights_And_Arenas.md) | Multi-phase boss encounters built entirely from existing ECS systems. Arena design, phase transitions, attack patterns, and health gates. |
| 27 | [Developer Console & Debug Tools](Game_Engine_Tutorial_2/Chapter_27_Developer_Console_And_Debug_Tools.md) | In-game command console (~ key), command parsing, debug wireframe rendering (AABBs, triggers, nav graphs), FPS counter, and cheat commands. |
| 28 | [Framebuffers & Post-Processing](Game_Engine_Tutorial_2/Chapter_28_Framebuffers_And_Post_Processing.md) | Render-to-texture with FBOs, a full-screen quad pipeline, and six post-processing effects: invert, greyscale, vignette, bloom, damage flash, and colour grading. |
| 29 | [Shadow Mapping](Game_Engine_Tutorial_2/Chapter_29_Shadow_Mapping.md) | Render the scene from the light's perspective into a depth FBO, sample that depth texture to determine shadows, PCF filtering, shadow acne fixes, and cascaded shadow maps. |
| 30 | [Font Rendering](Game_Engine_Tutorial_2/Chapter_30_Font_Rendering.md) | Bitmap font atlases and FreeType rasterisation. Glyph metrics, texture atlas packing, a batched TextRenderer, text alignment, and word wrapping. |
| **30a** | [Rendering Pipeline Cleanup](Game_Engine_Tutorial_2/Chapter_30a_Rendering_Pipeline_Cleanup.md) | Formalise render passes into a RenderPipeline class, build a ShaderCache (compile-once, retrieve-by-name), and add font resource management to ResourceManager. |

## Part 7: Visual Fidelity (Chapters 31-35)

| # | Chapter | Synopsis |
|---|---------|----------|
| 31 | [Decals](Game_Engine_Tutorial_2/Chapter_31_Decals.md) | Project textures onto world geometry for bullet holes, blood splats, scorch marks, and footprints. Decal meshes, projection, and fade-out. |
| 32 | [Frustum Culling](Game_Engine_Tutorial_2/Chapter_32_Frustum_Culling.md) | Extract the view frustum from the VP matrix, test AABBs against frustum planes, and skip rendering for off-screen objects. |
| 33 | [Skeletal Animation](Game_Engine_Tutorial_2/Chapter_33_Skeletal_Animation.md) | Bone hierarchies, skinning weights, animation clips with keyframes, the animation system, and GPU skinning in the vertex shader. |
| 34 | [Level Transitions](Game_Engine_Tutorial_2/Chapter_34_Level_Transitions.md) | Portal triggers, loading states, map streaming, player state persistence across levels, and fade transitions. |
| 35 | [Normal Mapping](Game_Engine_Tutorial_2/Chapter_35_Normal_Mapping.md) | Tangent-space normals, TBN matrix construction, computing tangent vectors from geometry, a normal-mapped Phong shader, and Material component updates. |
| **35a** | [Material System Cleanup](Game_Engine_Tutorial_2/Chapter_35a_Material_System_Cleanup.md) | Unified Material struct with MapFlags bitfield, centralised tangent computation in MeshLoader, and shader variants via preprocessor defines. |

## Part 8: Advanced Systems (Chapters 36-40)

| # | Chapter | Synopsis |
|---|---------|----------|
| 36 | [Model Loading](Game_Engine_Tutorial_2/Chapter_36_Model_Loading.md) | Load complex 3D models from OBJ and glTF formats. Scene graphs, multiple meshes per model, and material assignment. |
| 37 | [Pathfinding](Game_Engine_Tutorial_2/Chapter_37_Pathfinding.md) | Navigation meshes, A* search, path smoothing, and integrating pathfinding with the AI system for intelligent enemy movement. |
| 38 | [Instanced Rendering](Game_Engine_Tutorial_2/Chapter_38_Instanced_Rendering.md) | Draw hundreds of identical objects in a single draw call. Instance buffers, per-instance transforms, and when instancing beats batching. |
| 39 | [Water & Liquid Rendering](Game_Engine_Tutorial_2/Chapter_39_Water_And_Liquid_Rendering.md) | Planar reflections, refraction with distortion, animated water surfaces, Fresnel effect, and underwater post-processing. |
| 40 | [Animation Events & Notifies](Game_Engine_Tutorial_2/Chapter_40_Animation_Events_And_Notifies.md) | Embed events in animation clips (footsteps, muzzle flashes, damage windows). Detect crossed events during playback, route them through a dispatch system. |
| **40a** | [Animation Pipeline Cleanup](Game_Engine_Tutorial_2/Chapter_40a_Animation_Pipeline_Cleanup.md) | Replace hardcoded event dispatch with AnimEventDispatcher (handler registry), split Animator into hot/cold data, and introduce BoneMaskLibrary for named mask presets. |

## Part 9: Animation & PBR (Chapters 41-45)

| # | Chapter | Synopsis |
|---|---------|----------|
| 41 | [Ragdoll Physics](Game_Engine_Tutorial_2/Chapter_41_Ragdoll_Physics.md) | Map bone hierarchies to physics rigid bodies with joint constraints. Seamless transition from animated to physics-driven, iterative constraint solving, and settling detection. |
| 42 | [Animation Layers & Partial Body](Game_Engine_Tutorial_2/Chapter_42_Animation_Layers_And_Partial_Body.md) | Bone masks for selecting which bones a layer controls, override and additive blending, layer stacks, and practical examples (walk+shoot, reload+strafe). |
| 43 | [Inverse Kinematics](Game_Engine_Tutorial_2/Chapter_43_Inverse_Kinematics.md) | Two-bone IK for arms/legs, foot placement on uneven terrain, hand IK for weapon grips, head tracking, and CCD for longer chains. |
| 44 | [PBR Materials](Game_Engine_Tutorial_2/Chapter_44_PBR_Materials.md) | Physically-based rendering with metallic-roughness workflow. GGX normal distribution, Smith-Schlick geometry, Fresnel, HDR, tone mapping, and image-based lighting. |
| 45 | [Advanced Particle Physics & Rendering](Game_Engine_Tutorial_2/Chapter_45_Advanced_Particle_Physics_And_Rendering.md) | Particle-world collision, force fields (wind, turbulence, attractors), rotation, flipbook animation, trails/ribbons, and soft particles. |
| **45a** | [Particle System Cleanup](Game_Engine_Tutorial_2/Chapter_45a_Particle_System_Cleanup.md) | Convert ForceFields to ECS entities, unify particle pools with feature flags, and extend ParticleEffectDef to cover all Chapter 45 properties. |

## Part 10: Data-Driven Finale (Chapter 46)

| # | Chapter | Synopsis |
|---|---------|----------|
| 46 | [Data-Driven Particle Effects](Game_Engine_Tutorial_2/Chapter_46_Data_Driven_Particle_Effects.md) | Define particle effects entirely in JSON — emitter shapes, colour/size curves, sub-emitters, and a ParticleEffectManager. The culmination of the particle system. |

## Part 11: Tools & Scripting (Chapters 47-50)

| # | Chapter | Synopsis |
|---|---------|----------|
| 47 | [ImGui Debug UI](Game_Engine_Tutorial_2/Chapter_47_ImGui_Debug_UI.md) | Integrate Dear ImGui for runtime entity inspection, system profiling, and registry stats. Build debug windows that let you tweak the game while it runs. |
| 48 | [Level Editor](Game_Engine_Tutorial_2/Chapter_48_Level_Editor.md) | An in-engine editor with free-fly camera, entity placement via mouse ray casting, transform gizmos, property panels, level save/load, and undo/redo. |
| 49 | [Lua Scripting](Game_Engine_Tutorial_2/Chapter_49_Lua_Scripting.md) | Embed Lua via sol2 for gameplay scripting and configuration. Bind ECS to Lua, LuaScript components with callbacks, and Lua config files for all tweakable values. |
| 50 | [Asset Pipeline & Preprocessing](Game_Engine_Tutorial_2/Chapter_50_Asset_Pipeline.md) | Offline asset compiler that converts raw assets to optimised binary formats. Mesh preprocessing, texture compression, asset manifests, and incremental rebuilds. |
| **50a** | [Tools & Pipeline Cleanup](Game_Engine_Tutorial_2/Chapter_50a_Tools_And_Pipeline_Cleanup.md) | ConfigManager for all tweakable values via Lua, editor state management, script hot-reload with file watching, and asset dependency graph. |

## Part 12: Production Rendering (Chapters 51-55)

| # | Chapter | Synopsis |
|---|---------|----------|
| 51 | [Level of Detail](Game_Engine_Tutorial_2/Chapter_51_Level_Of_Detail.md) | Discrete LOD with distance-based mesh swapping, hysteresis to prevent flickering, cross-fade dithering, billboard impostors, and LOD-aware frustum culling. |
| 52 | [Deferred Rendering](Game_Engine_Tutorial_2/Chapter_52_Deferred_Rendering.md) | G-buffer with multiple render targets, geometry and lighting passes, light volumes with stencil tricks, forward pass for transparency, and PBR integration. |
| 53 | [SSAO](Game_Engine_Tutorial_2/Chapter_53_SSAO.md) | Screen-space ambient occlusion using G-buffer depth and normals. Hemisphere sampling, noise texture, blur pass, and integration with the deferred lighting ambient term. |
| 54 | [Anti-Aliasing](Game_Engine_Tutorial_2/Chapter_54_Anti_Aliasing.md) | Three AA techniques: MSAA (hardware), FXAA (post-process edge smoothing), and TAA (temporal with jitter, motion vectors, and history blending). |
| 55 | [Profiling & Optimisation](Game_Engine_Tutorial_2/Chapter_55_Profiling_And_Optimisation.md) | CPU and GPU profiling, ImGui overlay with frame graphs, draw call analysis, memory budgets, bottleneck identification, and a review of every optimisation technique in the series. |
| **55a** | [Production Rendering Cleanup](Game_Engine_Tutorial_2/Chapter_55a_Production_Rendering_Cleanup.md) | LOD-aware instanced rendering, tiled light culling, render pipeline configuration with quality presets (Low/Medium/High/Ultra), and the final architecture review. |

---

## Prerequisites

- Basic C++ knowledge (variables, functions, classes, pointers)
- No prior graphics or game engine experience required — every concept is explained from first principles
- Each chapter builds on the previous one; work through them in order

## Tech Stack

| Technology | Purpose |
|-----------|---------|
| **C++17** | Core language |
| **OpenGL 3.3+** | Graphics rendering |
| **GLFW** | Window and input |
| **GLAD** | OpenGL function loading |
| **EnTT** | Entity Component System |
| **glm** | Math (vectors, matrices) |
| **stb_image** | Texture loading |
| **miniaudio** | Audio playback |
| **ENet** | Networking (UDP) |
| **FreeType** | Font rasterisation |
| **nlohmann/json** | Data file loading |
| **Dear ImGui** | Debug UI and editor |
| **sol2** | Lua scripting bindings |
| **Lua 5.4** | Scripting and configuration |
| **CMake** | Build system |
