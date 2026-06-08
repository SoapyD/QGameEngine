# QEngine — Code Cleanup Roadmap

Refactoring checkpoints every 5 chapters. These "X.5" chapters clean up accumulated code debt before moving on.

Each cleanup chapter explains **why** the refactor is needed, **what** changes, and provides the complete refactored code.

---

## Cleanup Schedule

| Cleanup | After Ch | Title | What Gets Cleaned Up |
|---------|----------|-------|---------------------|
| 5a | 5 | Foundation Cleanup | Extract vertex/mesh setup from main.cpp, ResourceManager for textures/shaders, remove mouse globals |
| 10a | 10 | Game Loop & Physics Cleanup | Extract fixed timestep into GameLoop class, InputManager, centralise physics constants, formalise system scheduling |
| 15a | 15 | HUD & Effects Cleanup | Component-based HUD state, extract HUD renderer, consolidate text rendering, remove global UI state |
| 20a | 20 | Polish Systems Cleanup | Screen shake/view bob/recoil as components, data-driven particle emitters, effect parameter structs, interpolation utilities |
| 25a | 25 | Animation & Assets Cleanup | Load weapon animations from data files, animation registry, asset cache formalisation, parameterise procedural effects |
| 30a | 30 | Rendering Pipeline Cleanup | Render pass abstraction, shader cache, font resource management, draw call batching audit |
| 35a | 35 | Material System Cleanup | Unified Material component, texture slot management, tangent computation in mesh loader, shader variant management |
| 40a | 40 | Animation Pipeline Cleanup | Event handler registry, animation component audit, bone mask presets, animation graph simplification |
| 45a | 45 | Particle System Cleanup | Force fields as entities, unified particle pool, trail system formalisation, per-effect configuration structs |
| 50a | 50 | Tools & Pipeline Cleanup | ConfigManager (Lua-based config for all tweakables), editor state management, script hot-reload, asset dependency graph, editor/runtime separation |
| 55a | 55 | Production Rendering Cleanup | LOD integration with instancing, tiled/clustered light culling, unified render pipeline update, final architecture review |

---

## Cleanup 5a: Foundation Cleanup (after Ch 5)

### Problems
- `main.cpp` has inline vertex data, VAO/VBO setup, and manual OpenGL calls
- Mouse state is global variables (`lastMouseX`, `mouseXOffset`, etc.)
- Textures and shaders are created ad-hoc with no management
- Entity creation is inline in main()

### Refactoring Targets

**1. Extract mesh setup from main.cpp**
- Vertex data and VAO/VBO setup → `Mesh` class already exists (Ch 6 builds on this)
- For now: move vertex arrays and GL setup into helper functions or a `MeshFactory`
- main.cpp should just call `createTriangleMesh()` and `createQuadMesh()`

**2. InputManager class**
- Move mouse callback and state into `engine/core/input_manager.h`
- Encapsulate `lastMouseX/Y`, `mouseXOffset/Y`, `firstMouse`
- Provide `getMouseDelta()` and `isKeyPressed()` methods
- Register GLFW callbacks internally

**3. ResourceManager (basic)**
- `engine/core/resource_manager.h` — holds loaded Shaders and Textures
- `getShader(name)`, `loadShader(name, vert, frag)`
- `getTexture(name)`, `loadTexture(name, path)`
- Prevents duplicate loading, centralises cleanup

**4. Scene setup function**
- Move entity creation into `setupScene(registry, resources)` function
- main.cpp game loop becomes clean: init → setup → loop → cleanup

### After Cleanup, main.cpp Should Look Like
```cpp
int main() {
    Window window(1280, 720, "QEngine");
    InputManager input(window);
    ResourceManager resources;

    resources.loadShader("basic", "assets/shaders/basic.vert", "assets/shaders/basic.frag");
    resources.loadShader("textured", "assets/shaders/textured.vert", "assets/shaders/textured.frag");
    resources.loadTexture("wall", "assets/textures/wall.png");

    Camera camera(glm::vec3(0.0f, 0.0f, 3.0f));
    entt::registry registry;
    setupScene(registry, resources);

    while (!window.shouldClose()) {
        // ... clean game loop
    }
}
```

---

## Cleanup 10a: Game Loop & Physics Cleanup (after Ch 10)

### Problems
- Fixed timestep accumulator is a large block of boilerplate in main loop
- System execution order is implicit (just function call order)
- Physics parameters (gravity, friction, air control) are magic numbers scattered across systems
- Collision layer bitmasks are file-scope constants

### Refactoring Targets

**1. GameLoop / FixedTimestep class**
- Extract fixed timestep logic into reusable class
- `FixedTimestep timestep(1.0f / 60.0f)` with `while (timestep.step())` pattern

**2. SystemScheduler (lightweight)**
- Not a full ECS scheduler — just a clear phase structure
- `scheduler.addSystem(Phase::Physics, physicsSystem)`
- Phases: Input, Physics, GameLogic, LateUpdate, Render

**3. PhysicsConfig struct**
- Centralise: gravity, friction, air control factor, max speed, jump velocity
- Store as registry context: `registry.ctx().emplace<PhysicsConfig>()`
- Systems read from config instead of hardcoded values

**4. CollisionLayers namespace**
- Move bitmask definitions into `engine/physics/collision_layers.h`
- Named constants: `CollisionLayers::Player`, `CollisionLayers::World`, etc.

---

## Cleanup 15a: HUD & Effects Cleanup (after Ch 15)

### Problems
- HUD state (damage flash timer, messages) is global mutable state
- HUD rendering is scattered helper functions called from main
- Multiple text rendering approaches coexist
- No separation between HUD data and HUD rendering

### Refactoring Targets

**1. HUDState component**
- Move all HUD state into an ECS component on the player entity
- `DamageFlash`, `HUDMessages`, `CrosshairStyle` as components

**2. HUDRenderer system**
- Single `hudRenderSystem(registry, shader, font)` that reads components
- Replaces scattered `drawHealthBar()`, `drawCrosshair()` calls

**3. Consolidate text rendering**
- Single `TextRenderer` utility (prepares for Ch 30's proper font system)
- All text goes through one interface

---

## Cleanup 20a: Polish Systems Cleanup (after Ch 20)

### Problems
- Screen shake, view bob, weapon recoil are global state structs
- Particle emitter functions hardcode all parameters
- Effect parameters (sizes, colours, lifetimes) are magic numbers
- Interpolation helpers are scattered loose functions

### Refactoring Targets

**1. Effect components**
- `ScreenShake`, `ViewBob`, `WeaponRecoil` as proper ECS components
- Systems read/write these components, camera applies them

**2. ParticleEmitterDef struct**
- Data struct for emitter properties (count, speed range, colour, lifetime, size)
- `emitParticles(pool, def, position, direction)` — one generic function
- Predefined defs: `MUZZLE_FLASH_DEF`, `EXPLOSION_DEF`, `SPARK_DEF`

**3. MathUtils header**
- `engine/core/math_utils.h` with lerp, smoothstep, easeIn, easeOut, spring
- Used by particles, animation, camera effects

---

## Cleanup 25a: Animation & Assets Cleanup (after Ch 25)

### Problems
- Weapon animations defined as global `WeaponAnimation` objects
- Animation parameters (bob, sway, recoil) are hardcoded constants
- Asset loading is ad-hoc with no formal cache

### Refactoring Targets

**1. Animation data loading**
- Load `WeaponAnimation` from JSON/binary files
- `AnimationLibrary` class: `getAnimation("shotgun_fire")`

**2. AssetCache formalisation**
- Extend ResourceManager with reference counting
- Meshes, textures, shaders, animations all cached by name

**3. Configurable effect parameters**
- `WeaponEffectConfig` struct (bobAmount, swaySpeed, recoilStiffness)
- Loaded per-weapon from data files

---

## Cleanup 30a: Rendering Pipeline Cleanup (after Ch 30)

### Problems
- Render passes are implicit in main loop ordering
- Shaders compiled individually with no caching
- Font instances scattered as globals
- No batching audit

### Refactoring Targets

**1. RenderPipeline class**
- Formalise render order: Skybox → World → Transparent → Particles → HUD
- Each pass is a function, pipeline manages state transitions

**2. ShaderCache**
- Compile once, retrieve by name
- Hot-reload capability for development

**3. Font resource management**
- Fonts loaded via ResourceManager like textures
- `resources.getFont("hud")`, `resources.getFont("console")`

---

## Cleanup 35a: Material System Cleanup (after Ch 35)

### Problems
- Material component has grown with diffuse, specular, normal map, flags
- Tangent computation happens at different points
- Shader needs to handle with/without normal maps

### Refactoring Targets

**1. Unified Material struct**
- Clean struct: albedo, normalMap, specularMap, roughness, metallic
- `hasMaps` bitfield instead of individual booleans

**2. Tangent computation in mesh loader**
- Automatically compute tangents during OBJ/glTF loading
- Never computed in main.cpp

**3. Shader variants**
- Preprocessor defines or uber-shader for with/without normal mapping
- Material binds appropriate shader variant

---

## Cleanup 40a: Animation Pipeline Cleanup (after Ch 40)

### Problems
- Animation event dispatch uses string-based matching
- Animation components accumulating (AnimState, AnimLayer, AnimEvents, IKTarget)
- Bone masks defined inline

### Refactoring Targets

**1. Event handler registry**
- `AnimEventDispatcher::registerHandler("footstep", handleFootstep)`
- Replaces string switch/if chains

**2. Animation component audit**
- Review component sizes, consider splitting hot/cold data
- Document which systems touch which components

**3. Bone mask presets**
- Load from data: `"upper_body"`, `"lower_body"`, `"full_body"`
- Referenced by name in animation layer setup

---

## Cleanup 45a: Particle System Cleanup (after Ch 45)

### Problems
- Force fields passed as loose array parameter
- Main and trail pools are separate with duplicated logic
- Turbulence constants hardcoded
- Per-effect configuration scattered

### Refactoring Targets

**1. Force fields as entities**
- `ForceField` component with type (wind, turbulence, vortex), shape, strength
- Particle system queries registry for active fields

**2. Unified particle pool**
- Single pool with type tags, or pool-per-effect-type
- Common update path with optional features (collision, trails)

**3. Per-effect configuration**
- All particle parameters in `ParticleEffectDef` structs
- Prepares directly for Ch 46 (data-driven effects)

---

## Cleanup 50a: Tools & Pipeline Cleanup (after Ch 50)

### Problems
- Tweakable game values (camera speed, physics constants, HUD layout) are still hardcoded in C++
- Editor state (selection, gizmo mode, undo history) is ad-hoc
- Lua scripts require engine restart to pick up changes
- Asset loading has no dependency tracking or build manifests

### Refactoring Targets

**1. ConfigManager**
- All tweakable values loaded from Lua config files at startup
- `config.get<float>("camera.speed")`, `config.get<int>("physics.substeps")`
- Hot-reload in debug builds — change config.lua, see results immediately
- Migrate PhysicsConfig, HUD layout values, particle parameters, weapon configs to Lua

**2. Editor state management**
- Unified `EditorState` context (selected entity, active tool, gizmo mode)
- Undo/redo stack formalisation
- Editor vs runtime mode separation (editor systems don't tick gameplay)

**3. Script hot-reload**
- File watcher on script directory
- Reload Lua scripts without restarting the engine
- Error handling for broken scripts (don't crash, show error in console)

**4. Asset dependency graph**
- Track which assets reference which other assets
- Rebuild only what changed when an asset is modified
- Build manifest for release packaging

---

## Cleanup 55a: Production Rendering Cleanup (after Ch 55)

### Problems
- LOD selection is per-entity with no batching consideration
- Deferred renderer's light loop is brute-force (every light tests every pixel)
- Render pipeline has grown with new passes but state management is ad-hoc
- No unified profiling hooks in the render pipeline

### Refactoring Targets

**1. LOD-aware instanced rendering**
- Group entities by (mesh, LOD level) for batched draw calls
- LOD selection integrated into frustum culling pass
- Billboard impostor generation for distant objects

**2. Tiled/clustered light culling**
- Divide screen into tiles, assign lights to tiles
- Only evaluate lights that affect each tile
- Scales from dozens to hundreds of lights

**3. Render pipeline update**
- All new passes (G-buffer, SSAO, AA) integrated into RenderPipeline from 30a
- Pipeline configuration object (enable/disable passes, quality presets)
- GPU timestamp queries for per-pass profiling

**4. Quality presets**
- Low/Medium/High/Ultra presets that configure LOD bias, SSAO samples, shadow resolution, AA mode
- Exposed through settings menu from Ch 22
- ConfigManager integration from 50a

---

## Progress Tracker

| Cleanup | Status |
|---------|--------|
| 5a: Foundation Cleanup | Pending |
| 10a: Game Loop & Physics Cleanup | Pending |
| 15a: HUD & Effects Cleanup | Pending |
| 20a: Polish Systems Cleanup | Pending |
| 25a: Animation & Assets Cleanup | Pending |
| 30a: Rendering Pipeline Cleanup | Pending |
| 35a: Material System Cleanup | Pending |
| 40a: Animation Pipeline Cleanup | Pending |
| 45a: Particle System Cleanup | Pending |
| 50a: Tools & Pipeline Cleanup | Pending |
| 55a: Production Rendering Cleanup | Pending |
