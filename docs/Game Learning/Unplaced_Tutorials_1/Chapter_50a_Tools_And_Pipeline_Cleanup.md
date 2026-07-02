# Chapter 50a: Tools & Pipeline Cleanup

> **Prerequisites:** Chapter 50 (Asset Pipeline & Preprocessing) completed. You should have a working asset compiler that produces `.qmesh` and `.qtex` binary files, a `ManifestBuilder` that generates `manifest.json`, a `ResourceManager` that loads cooked assets with raw fallback, and `AssetHandle` integer IDs for fast lookup. Chapter 49's `ConfigManager` and `ScriptManager` are integrated, with `config.lua` defining game settings and `LuaScript` components driving per-entity behaviour. Chapter 48's `EditorState` has a free-fly camera, gizmos, a property panel, and an `UndoStack` with concrete command classes.

---

## Time for Another Cleanup

You know the pattern by now. Chapters 5a, 10a, 15a, 20a, 25a, 30a, 35a, 40a, and 45a each followed the same rhythm: the features work, the code does not scale. Chapters 47 through 50 added four major systems -- ImGui debug UI, a level editor, Lua scripting, and an asset pipeline. Each of them works. And each of them introduced structural problems that will compound as we build on top of them.

Let us take inventory. Open your `config_manager.h`, `editor_state.h`, `script_system.cpp`, and `manifest_builder.h`. You will find something like this:

**Problem 1: Tweakable values are still half-hardcoded.**

```cpp
// Current state — Chapter 49 introduced config.lua loading, but usage is inconsistent

// camera_system.cpp — reads from config
float moveSpeed = config.get<float>("camera_speed", 10.0f);

// particle_effect_defs.h — still hardcoded C++ constants
inline const ParticleEffectDef EXPLOSION_DEF = [] {
    ParticleEffectDef def;
    def.drag = 2.0f;              // should be in config
    def.turbulenceFreq = 1.5f;    // should be in config
    def.burstCount = 30;          // should be in config
    // ...
}();

// hud_system.cpp — reads some values from config, hardcodes others
float crosshairSize = config.getNested<float>("hud", "crosshair_size", 16.0f);
float healthBarY = 20.0f;  // hardcoded — not in config at all
float ammoCountX = 750.0f; // hardcoded — not in config at all
```

Chapter 49 introduced the `ConfigManager`, but it is a simple wrapper that loads once at startup. There is no hot-reload -- change `config.lua` and you must restart the engine. Some systems read from config, others still use hardcoded constants. The particle effect defs from Chapter 45a are entirely C++ structs with baked values. We need a proper system that loads all tweakable values from Lua and refreshes them at runtime.

**Problem 2: Editor state is scattered across EditorState members.**

```cpp
// editor_state.h — state scattered across loose members
class EditorState : public GameState {
    // ...
    EditorCamera      m_camera;
    SelectionManager  m_selection;
    GizmoRenderer     m_gizmo;
    PropertyPanel     m_propertyPanel;
    EntityPalette     m_palette;
    UndoStack         m_undoStack;
    LevelSerializer   m_serializer;

    GizmoMode m_gizmoMode = GizmoMode::Translate;
    bool  m_snapEnabled = true;
    float m_snapSize    = 1.0f;
    std::string m_currentLevelPath;
    bool        m_unsavedChanges = false;
};
```

Every piece of editor state lives as a private member of `EditorState`. Want to query the current selection from the property panel? The property panel needs a pointer to the selection manager. Want to check if there are unsaved changes from the menu bar? Thread `m_unsavedChanges` through draw calls. Want to show the undo history in a debug window? Pass the undo stack reference around. This is the same "scattered context" problem we fixed for physics in Chapter 10a and particles in Chapter 45a.

**Problem 3: Lua scripts require engine restart to pick up changes.**

```cpp
// script_system.cpp — no automatic reload
void ScriptSystem::update(float dt)
{
    auto view = m_registry->view<LuaScript>();
    for (auto [entity, script] : view.each()) {
        if (!script.initialised) {
            initScript(entity, script);
            // ...
        }
        // on_update...
    }
}
// The only reload mechanism is a manual console command:
// > reload_scripts
// This sets initialised = false on all scripts. No file watching.
```

Chapter 49 noted that hot-reload was possible using `std::filesystem::last_write_time`, but left it as a manual console command. During development, editing a Lua script and having to type `reload_scripts` every time breaks flow. We need a file watcher that detects changes automatically.

**Problem 4: Asset loading has no dependency graph at runtime.**

```cpp
// Current state — manifest has dependency data, but nothing uses it at runtime

// asset_manifest.h — getDependencies exists but is never called automatically
std::vector<std::string> getDependencies(const std::string& name) const;

// ResourceManager loads assets individually — no dependency awareness
void loadTexture(const std::string& name);  // does not load dependencies
void loadMesh(const std::string& name);     // does not load dependencies

// Level loading — manually lists every asset to load
void loadLevel(const std::string& levelName)
{
    loadTexture("brick_wall");      // how do we know this is needed?
    loadTexture("stone_floor");     // hardcoded list
    loadMesh("enemy_grunt");        // no connection to level data
    // ...
}
```

Chapter 50 built a manifest with dependency arrays, but the runtime never uses them. Loading a level means manually listing every asset it references. Add a new texture to a level, forget to add the load call, and you get a missing-texture crash. The dependency graph should drive loading automatically.

Here is our plan:

| Problem | Solution |
|---|---|
| Config values half-hardcoded, no hot-reload | `ConfigManager` formalised with dot-path access, file watcher, hot-reload in debug builds |
| Editor state scattered across `EditorState` members | Unified `EditorContext` stored as registry context, accessible from any editor subsystem |
| Lua scripts require manual reload | `FileWatcher` class, automatic script hot-reload with error recovery |
| Asset manifest dependencies unused at runtime | `AssetDependencyGraph` that drives automatic loading and incremental rebuild |

---

## Step 1: ConfigManager — Formalised Lua Config System

### The Problem in Detail

Chapter 49 introduced a `ConfigManager` that loads `config.lua` and provides `get<T>()`, `getNested<T>()`, and `getDeep<T>()` for typed access. This works, but it has three issues:

1. **No dot-path access.** Reading a deeply nested value requires knowing the nesting structure and calling the right method: `getNested` for two levels, `getDeep` for three. What about four? We need a general-purpose `get` that takes a dot-separated path: `config.get<float>("weapons.shotgun.damage")`.

2. **No hot-reload.** Change `config.lua` and restart. In a tight iteration loop (tweaking camera speed, adjusting HUD layout), this is slow.

3. **Incomplete migration.** Many values are still hardcoded. The particle effect defs, weapon configs, and HUD layout values were never moved to Lua.

### Dot-Path Config Access

We replace the three separate methods with a single `get<T>()` that traverses Lua tables using a dot-separated path:

```cpp
// engine/scripting/config_manager.h — updated

#pragma once

#include <sol/sol.hpp>
#include <string>
#include <vector>
#include <functional>
#include <filesystem>
#include <glm/glm.hpp>
#include <iostream>

class ConfigManager {
public:
    // ─── Load / reload ─────────────────────────────────────────
    bool load(const std::string& filepath);
    bool reload();  // re-execute the same file

    // ─── Dot-path access ───────────────────────────────────────
    // Replaces get<T>, getNested<T>, and getDeep<T>.
    //
    // Examples:
    //   config.get<float>("camera.speed")
    //   config.get<int>("weapons.shotgun.pellet_count")
    //   config.get<bool>("debug.show_fps")
    //   config.get<float>("physics.gravity")

    template<typename T>
    T get(const std::string& path, T defaultValue = T{}) const
    {
        sol::object obj = resolve(path);
        if (obj.valid() && obj.is<T>()) {
            return obj.as<T>();
        }
        return defaultValue;
    }

    // Get a colour from a Lua array table: {r, g, b, a}
    glm::vec4 getColor(const std::string& path,
                       glm::vec4 defaultValue = {1, 1, 1, 1}) const;

    // Get an array of vec3 values from a Lua table
    std::vector<glm::vec3> getVec3Array(const std::string& path) const;

    // ─── Hot-reload support ────────────────────────────────────
    // Call once per frame in debug builds. Checks file modification
    // time and reloads if the file has changed.
    void pollForChanges();

    // Register a callback to be notified when config reloads.
    // Systems use this to refresh their cached values.
    using ReloadCallback = std::function<void(const ConfigManager&)>;
    void onReload(ReloadCallback callback);

    // ─── Raw access (for console integration) ──────────────────
    sol::state& getLuaState() { return m_lua; }
    const sol::state& getLuaState() const { return m_lua; }

    const std::string& getFilePath() const { return m_filepath; }

private:
    // Resolve a dot-separated path to a Lua object.
    // "weapons.shotgun.damage" → m_lua["weapons"]["shotgun"]["damage"]
    sol::object resolve(const std::string& path) const;

    sol::state  m_lua;
    std::string m_filepath;

    // Hot-reload
    std::filesystem::file_time_type m_lastWriteTime;
    std::vector<ReloadCallback>     m_reloadCallbacks;
};
```

### Implementation

```cpp
// engine/scripting/config_manager.cpp — updated

#include "engine/scripting/config_manager.h"

#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

bool ConfigManager::load(const std::string& filepath)
{
    m_filepath = filepath;

    m_lua.open_libraries(sol::lib::base, sol::lib::math,
                         sol::lib::string, sol::lib::table);

    try {
        m_lua.script_file(filepath);
        std::cout << "[Config] Loaded: " << filepath << std::endl;
    }
    catch (const sol::error& e) {
        std::cerr << "[Config] Error loading " << filepath << ": "
                  << e.what() << std::endl;
        return false;
    }

    // Record modification time for hot-reload
    if (fs::exists(filepath)) {
        m_lastWriteTime = fs::last_write_time(filepath);
    }

    return true;
}

bool ConfigManager::reload()
{
    if (m_filepath.empty()) return false;

    // Re-create the Lua state to clear old values
    m_lua = sol::state{};
    m_lua.open_libraries(sol::lib::base, sol::lib::math,
                         sol::lib::string, sol::lib::table);

    try {
        m_lua.script_file(m_filepath);
        std::cout << "[Config] Reloaded: " << m_filepath << std::endl;
    }
    catch (const sol::error& e) {
        std::cerr << "[Config] Error reloading " << m_filepath << ": "
                  << e.what() << std::endl;
        return false;
    }

    // Update modification time
    if (fs::exists(m_filepath)) {
        m_lastWriteTime = fs::last_write_time(m_filepath);
    }

    // Notify all registered systems
    for (auto& callback : m_reloadCallbacks) {
        callback(*this);
    }

    return true;
}

sol::object ConfigManager::resolve(const std::string& path) const
{
    // Split path on '.' and traverse Lua tables
    sol::object current = m_lua.globals();

    size_t start = 0;
    size_t dot = path.find('.');

    while (dot != std::string::npos) {
        std::string segment = path.substr(start, dot - start);

        if (!current.valid() || current.get_type() != sol::type::table) {
            return sol::nil;
        }

        current = current.as<sol::table>()[segment];
        start = dot + 1;
        dot = path.find('.', start);
    }

    // Final segment
    std::string last = path.substr(start);
    if (!current.valid() || current.get_type() != sol::type::table) {
        return sol::nil;
    }

    return current.as<sol::table>()[last];
}

glm::vec4 ConfigManager::getColor(const std::string& path,
                                   glm::vec4 defaultValue) const
{
    sol::object obj = resolve(path);
    if (!obj.valid() || obj.get_type() != sol::type::table) {
        return defaultValue;
    }

    sol::table colorTbl = obj.as<sol::table>();
    return glm::vec4(
        colorTbl.get_or(1, defaultValue.r),
        colorTbl.get_or(2, defaultValue.g),
        colorTbl.get_or(3, defaultValue.b),
        colorTbl.get_or(4, defaultValue.a)
    );
}

std::vector<glm::vec3> ConfigManager::getVec3Array(const std::string& path) const
{
    std::vector<glm::vec3> result;
    sol::object obj = resolve(path);
    if (!obj.valid() || obj.get_type() != sol::type::table) return result;

    sol::table arr = obj.as<sol::table>();
    for (size_t i = 1; i <= arr.size(); ++i) {
        sol::optional<sol::table> vec = arr[i];
        if (vec) {
            result.push_back(glm::vec3(
                vec->get_or(1, 0.0f),
                vec->get_or(2, 0.0f),
                vec->get_or(3, 0.0f)
            ));
        }
    }
    return result;
}

void ConfigManager::pollForChanges()
{
    if (m_filepath.empty()) return;
    if (!fs::exists(m_filepath)) return;

    auto currentTime = fs::last_write_time(m_filepath);
    if (currentTime != m_lastWriteTime) {
        std::cout << "[Config] File changed, reloading..." << std::endl;
        reload();
    }
}

void ConfigManager::onReload(ReloadCallback callback)
{
    m_reloadCallbacks.push_back(std::move(callback));
}
```

### Expanded config.lua

We extend the config file to include every tweakable value that was previously hardcoded -- particle parameters, weapon configs, HUD layout, and editor defaults:

```lua
-- assets/scripts/config.lua — expanded for Chapter 50a
-- All tweakable engine values in one place.
-- Edit and save — the engine hot-reloads in debug builds.

-- ─── Camera ──────────────────────────────────────────────────────
camera = {
    speed             = 10.0,
    sprint_mult       = 2.0,
    mouse_sensitivity = 0.1,
    fov               = 90.0,
    near_plane        = 0.1,
    far_plane         = 500.0,
}

-- ─── Physics ─────────────────────────────────────────────────────
physics = {
    gravity        = -9.81,
    substeps       = 4,
    jump_force     = 8.0,
    friction       = 6.0,
    air_control    = 0.3,
    max_fall_speed = -50.0,
}

-- ─── Player ──────────────────────────────────────────────────────
player = {
    max_health    = 100,
    walk_speed    = 7.0,
    sprint_speed  = 12.0,
    crouch_speed  = 3.5,
    step_height   = 0.5,
}

-- ─── Weapons ─────────────────────────────────────────────────────
weapons = {
    shotgun = {
        damage          = 80,
        pellet_count    = 8,
        spread_angle    = 6.0,
        range           = 30.0,
        damage_falloff  = 0.5,
        fire_rate       = 0.8,
        recoil_pitch    = 3.0,
        recoil_yaw      = 1.5,
        recoil_recovery = 8.0,
    },
    nailgun = {
        damage          = 15,
        fire_rate       = 0.1,
        speed           = 40.0,
        range           = 100.0,
        recoil_pitch    = 0.5,
        recoil_yaw      = 0.3,
        recoil_recovery = 12.0,
    },
    rocket_launcher = {
        damage           = 120,
        splash_radius    = 5.0,
        splash_falloff   = 0.4,
        fire_rate        = 0.8,
        projectile_speed = 25.0,
        recoil_pitch     = 5.0,
        recoil_yaw       = 0.0,
        recoil_recovery  = 4.0,
    },
}

-- ─── HUD ─────────────────────────────────────────────────────────
hud = {
    crosshair_size    = 16.0,
    crosshair_gap     = 4.0,
    crosshair_color   = { 0.0, 1.0, 0.0, 0.8 },
    health_bar_x      = 20.0,
    health_bar_y      = 20.0,
    health_bar_width  = 200.0,
    health_bar_height = 20.0,
    ammo_x            = 750.0,
    ammo_y            = 20.0,
    ammo_font_size    = 24.0,
    message_duration  = 3.0,
    message_x         = 400.0,
    message_y         = 550.0,
}

-- ─── Particles ───────────────────────────────────────────────────
particles = {
    explosion = {
        burst_count      = 30,
        velocity_min     = { -3.0, 1.0, -3.0 },
        velocity_max     = {  3.0, 6.0,  3.0 },
        color_start      = { 1.0, 0.8, 0.2, 1.0 },
        color_end        = { 0.3, 0.1, 0.0, 0.0 },
        size_start       = 0.2,
        size_end         = 0.8,
        lifetime_min     = 0.4,
        lifetime_max     = 1.0,
        drag             = 2.0,
        gravity_scale    = 0.3,
        turbulence_freq  = 1.5,
        turbulence_amp   = 3.0,
    },
    muzzle_flash = {
        burst_count      = 5,
        velocity_min     = { -0.5, -0.5, 5.0 },
        velocity_max     = {  0.5,  0.5, 8.0 },
        color_start      = { 1.0, 0.9, 0.5, 1.0 },
        color_end        = { 1.0, 0.4, 0.0, 0.0 },
        size_start       = 0.05,
        size_end         = 0.15,
        lifetime_min     = 0.03,
        lifetime_max     = 0.08,
        drag             = 1.0,
        gravity_scale    = 0.0,
    },
    smoke = {
        burst_count      = 8,
        velocity_min     = { -0.3, 0.5, -0.3 },
        velocity_max     = {  0.3, 2.0,  0.3 },
        color_start      = { 0.5, 0.5, 0.5, 0.4 },
        color_end        = { 0.3, 0.3, 0.3, 0.0 },
        size_start       = 0.1,
        size_end         = 0.6,
        lifetime_min     = 1.0,
        lifetime_max     = 2.5,
        drag             = 3.0,
        gravity_scale    = -0.1,
        turbulence_freq  = 1.0,
        turbulence_amp   = 2.0,
    },
}

-- ─── Screen Effects ──────────────────────────────────────────────
screen_effects = {
    screen_shake_decay   = 5.0,
    screen_shake_max     = 0.5,
    view_bob_frequency   = 8.0,
    view_bob_amplitude   = 0.03,
}

-- ─── Audio ───────────────────────────────────────────────────────
audio = {
    master_volume     = 0.8,
    music_volume      = 0.5,
    sfx_volume        = 1.0,
    footstep_interval = 0.4,
}

-- ─── Editor ──────────────────────────────────────────────────────
editor = {
    grid_size         = 1.0,
    snap_enabled      = true,
    camera_speed      = 15.0,
    camera_fast_mult  = 3.0,
    gizmo_size        = 1.5,
    selection_color   = { 1.0, 0.8, 0.0, 1.0 },
}

-- ─── Debug ───────────────────────────────────────────────────────
debug = {
    show_fps       = true,
    show_colliders = false,
    show_navmesh   = false,
    god_mode       = false,
    noclip         = false,
}
```

### Loading Config Into Subsystems with Reload Callbacks

The key pattern: each subsystem loads its values from config at startup and registers a callback to refresh them on hot-reload.

```cpp
// Before (Chapter 49) — manual, no hot-reload
auto& config = registry.ctx().get<ConfigManager>();
float cameraSpeed = config.get<float>("camera_speed", 10.0f);

// After (Chapter 50a) — dot-path, hot-reload aware
auto& config = registry.ctx().get<ConfigManager>();
float cameraSpeed = config.get<float>("camera.speed", 10.0f);

// Register for reload notifications
config.onReload([&registry](const ConfigManager& cfg) {
    // Refresh PhysicsConfig
    auto& pc = registry.ctx().get<PhysicsConfig>();
    pc.gravity      = cfg.get<float>("physics.gravity", -9.81f);
    pc.substeps     = cfg.get<int>("physics.substeps", 4);
    pc.jumpForce    = cfg.get<float>("physics.jump_force", 8.0f);
    pc.friction     = cfg.get<float>("physics.friction", 6.0f);
    pc.airControl   = cfg.get<float>("physics.air_control", 0.3f);
    pc.maxFallSpeed = cfg.get<float>("physics.max_fall_speed", -50.0f);
    std::cout << "[Physics] Config reloaded" << std::endl;
});
```

### Loading PhysicsConfig — Before and After

```cpp
// Before (Chapter 49):
PhysicsConfig loadPhysicsConfig(const ConfigManager& config)
{
    PhysicsConfig pc;
    pc.gravity      = config.getNested<float>("physics", "gravity", -9.81f);
    pc.jumpForce    = config.getNested<float>("physics", "jump_force", 8.0f);
    pc.friction     = config.getNested<float>("physics", "friction", 6.0f);
    pc.airControl   = config.getNested<float>("physics", "air_control", 0.3f);
    pc.maxFallSpeed = config.getNested<float>("physics", "max_fall_speed", -50.0f);
    return pc;
}

// After (Chapter 50a):
PhysicsConfig loadPhysicsConfig(const ConfigManager& config)
{
    PhysicsConfig pc;
    pc.gravity      = config.get<float>("physics.gravity", -9.81f);
    pc.substeps     = config.get<int>("physics.substeps", 4);
    pc.jumpForce    = config.get<float>("physics.jump_force", 8.0f);
    pc.friction     = config.get<float>("physics.friction", 6.0f);
    pc.airControl   = config.get<float>("physics.air_control", 0.3f);
    pc.maxFallSpeed = config.get<float>("physics.max_fall_speed", -50.0f);
    return pc;
}
```

The API is cleaner -- one method instead of three -- and the `substeps` value that was hardcoded in the physics system is now configurable.

### Loading Particle Defs from Config

The particle effect defs from Chapter 45a were C++ constants. Now they load from Lua. We keep the C++ `ParticleEffectDef` struct unchanged -- it is a good data structure -- but populate it from config instead of hardcoded values:

```cpp
// engine/particles/particle_effect_loader.h — NEW
#pragma once

#include "engine/particles/particle_effect_def.h"
#include "engine/scripting/config_manager.h"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace ParticleEffectLoader {

// Load a particle effect def from a config path.
// Example: loadFromConfig(config, "particles.explosion")
inline ParticleEffectDef loadFromConfig(const ConfigManager& config,
                                         const std::string& basePath)
{
    ParticleEffectDef def;

    // Extract the effect name from the path (last segment)
    size_t lastDot = basePath.rfind('.');
    def.name = (lastDot != std::string::npos)
             ? basePath.substr(lastDot + 1)
             : basePath;

    auto path = [&](const std::string& key) {
        return basePath + "." + key;
    };

    def.burstCount    = config.get<int>(path("burst_count"), def.burstCount);
    def.sizeStart     = config.get<float>(path("size_start"), def.sizeStart);
    def.sizeEnd       = config.get<float>(path("size_end"), def.sizeEnd);
    def.lifetimeMin   = config.get<float>(path("lifetime_min"), def.lifetimeMin);
    def.lifetimeMax   = config.get<float>(path("lifetime_max"), def.lifetimeMax);
    def.drag          = config.get<float>(path("drag"), def.drag);
    def.gravityScale  = config.get<float>(path("gravity_scale"), def.gravityScale);
    def.turbulenceFreq = config.get<float>(path("turbulence_freq"), def.turbulenceFreq);
    def.turbulenceAmp  = config.get<float>(path("turbulence_amp"), def.turbulenceAmp);

    // Velocity range — stored as {x, y, z} arrays in Lua
    glm::vec4 velMin = config.getColor(path("velocity_min"),
                          glm::vec4(def.velocityMin, 0.0f));
    def.velocityMin = glm::vec3(velMin);

    glm::vec4 velMax = config.getColor(path("velocity_max"),
                          glm::vec4(def.velocityMax, 0.0f));
    def.velocityMax = glm::vec3(velMax);

    // Colours
    def.colorStart = config.getColor(path("color_start"), def.colorStart);
    def.colorEnd   = config.getColor(path("color_end"), def.colorEnd);

    return def;
}

} // namespace ParticleEffectLoader
```

Usage at startup:

```cpp
// In setupScene() or Game::init()
auto& config = registry.ctx().get<ConfigManager>();

ParticleEffectDef explosionDef =
    ParticleEffectLoader::loadFromConfig(config, "particles.explosion");
ParticleEffectDef muzzleDef =
    ParticleEffectLoader::loadFromConfig(config, "particles.muzzle_flash");
ParticleEffectDef smokeDef =
    ParticleEffectLoader::loadFromConfig(config, "particles.smoke");
```

Now a designer can open `config.lua`, change `particles.explosion.burst_count` from 30 to 50, save, and the engine reloads the value immediately. No recompile, no restart.

---

## Step 2: Editor State Management

### The Problem in Detail

Chapter 48's `EditorState` works, but its state is scattered across private members. The property panel needs the selection to know what to display. The menu bar needs `m_unsavedChanges` to show a dirty indicator. The toolbar needs `m_gizmoMode` to highlight the active tool. Every ImGui panel that needs editor state must receive it as a parameter or hold a pointer.

This is exactly what registry context objects are for. We used them for `PhysicsConfig` (Chapter 10a), `ParticleSystemContext` (Chapter 45a), and `ConfigManager` (Chapter 49). The editor state should follow the same pattern.

### EditorContext

```cpp
// engine/editor/editor_context.h — NEW

#pragma once

#include "engine/editor/undo_stack.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <string>

// ─── GizmoMode ──────────────────────────────────────────────────
// Moved from EditorState private enum to shared header.

enum class GizmoMode {
    Translate,
    Rotate,
    Scale
};

// ─── EditorMode ─────────────────────────────────────────────────
// Distinguishes editor time from runtime. Systems can check this
// to decide whether to tick.

enum class EditorMode {
    Editing,     // editor active, gameplay systems paused
    Playing,     // gameplay running (PIE — Play In Editor)
    Paused       // PIE paused at current frame
};

// ─── EditorContext ──────────────────────────────────────────────
// Unified editor state. Stored as a registry context object.
// Any editor subsystem (property panel, toolbar, menu bar, gizmo
// renderer) can access it via registry.ctx().get<EditorContext>().

struct EditorContext {

    // ─── Selection ──────────────────────────────────────────
    entt::entity selectedEntity = entt::null;
    bool hasSelection() const { return selectedEntity != entt::null; }

    void select(entt::entity e) { selectedEntity = e; }
    void clearSelection() { selectedEntity = entt::null; }

    // ─── Gizmo ──────────────────────────────────────────────
    GizmoMode gizmoMode = GizmoMode::Translate;

    // ─── Grid / Snap ────────────────────────────────────────
    bool  snapEnabled = true;
    float snapSize    = 1.0f;

    // ─── File state ─────────────────────────────────────────
    std::string currentLevelPath;
    bool        unsavedChanges = false;

    void markDirty() { unsavedChanges = true; }
    void markClean() { unsavedChanges = false; }

    // ─── Mode ───────────────────────────────────────────────
    EditorMode mode = EditorMode::Editing;
    bool isEditing() const { return mode == EditorMode::Editing; }
    bool isPlaying() const { return mode == EditorMode::Playing; }

    // ─── Undo / Redo ────────────────────────────────────────
    UndoStack undoStack;
};
```

### Registering the Context

```cpp
// In Game::init() or wherever the editor is set up
registry.ctx().emplace<EditorContext>();

// Load editor defaults from config
auto& config = registry.ctx().get<ConfigManager>();
auto& edCtx  = registry.ctx().get<EditorContext>();
edCtx.snapSize    = config.get<float>("editor.grid_size", 1.0f);
edCtx.snapEnabled = config.get<bool>("editor.snap_enabled", true);
```

### Simplified EditorState

With `EditorContext` in the registry, `EditorState` becomes much thinner:

```cpp
// engine/editor/editor_state.h — simplified

#pragma once

#include "engine/core/game_state.h"
#include "engine/editor/editor_camera.h"
#include "engine/editor/editor_context.h"
#include "engine/editor/gizmo_renderer.h"
#include "engine/editor/property_panel.h"
#include "engine/editor/entity_palette.h"
#include "engine/editor/level_serializer.h"

#include <entt/entt.hpp>

class RenderPipeline;
class InputManager;
class Console;
class Window;

class EditorState : public GameState {
public:
    EditorState(entt::registry& registry,
                RenderPipeline& renderer,
                InputManager& input,
                Console& console,
                Window& window);

    void enter() override;
    void exit() override;
    void update(float dt) override;
    void render() override;

    std::string getName() const override { return "EditorState"; }
    bool isTransparent() const override { return false; }

private:
    void handleInput(float dt);
    void handleMousePick();
    void handleGizmoInteraction(float dt);
    void drawMenuBar();
    void drawToolbar();

    entt::registry& m_registry;
    RenderPipeline& m_renderer;
    InputManager&   m_input;
    Console&        m_console;
    Window&         m_window;

    EditorCamera      m_camera;
    GizmoRenderer     m_gizmo;
    PropertyPanel     m_propertyPanel;
    EntityPalette     m_palette;
    LevelSerializer   m_serializer;

    // SelectionManager, UndoStack, GizmoMode, snap settings,
    // level path, unsaved flag — all moved to EditorContext.
};
```

Compare to the Chapter 48 version. Six members have moved into `EditorContext`. The `GizmoMode` enum is no longer a private nested type -- it is a shared enum that any editor subsystem can reference.

### Using EditorContext from Subsystems

The property panel no longer needs a selection manager pointer:

```cpp
// Before (Chapter 48):
void PropertyPanel::draw(entt::entity selected)
{
    if (selected == entt::null) return;
    // ...
}

// After (Chapter 50a):
void PropertyPanel::draw(entt::registry& registry)
{
    auto& ctx = registry.ctx().get<EditorContext>();
    if (!ctx.hasSelection()) return;

    entt::entity selected = ctx.selectedEntity;
    // ...
}
```

The menu bar can check for unsaved changes without a parameter:

```cpp
// Before (Chapter 48):
void EditorState::drawMenuBar()
{
    // m_unsavedChanges accessed directly as member
    if (m_unsavedChanges) {
        ImGui::Text("(unsaved)");
    }
}

// After (Chapter 50a):
void EditorState::drawMenuBar()
{
    auto& ctx = m_registry.ctx().get<EditorContext>();
    if (ctx.unsavedChanges) {
        ImGui::Text("(unsaved)");
    }
}
```

### Undo/Redo Through EditorContext

Commands now access the undo stack through the context:

```cpp
// Before (Chapter 48):
m_undoStack.execute(std::make_unique<MoveEntityCommand>(
    m_registry, selected, oldPos, newPos));

// After (Chapter 50a):
auto& ctx = m_registry.ctx().get<EditorContext>();
ctx.undoStack.execute(std::make_unique<MoveEntityCommand>(
    m_registry, selected, oldPos, newPos));
ctx.markDirty();
```

### Editor vs Runtime Mode Separation

The `EditorMode` enum formalises what Chapter 48 handled implicitly. Systems can now check the mode:

```cpp
// In the game loop — systems check editor mode
void physicsSystem(entt::registry& registry, float dt)
{
    // Skip physics when editor is in editing mode
    auto* edCtx = registry.ctx().find<EditorContext>();
    if (edCtx && edCtx->isEditing()) return;

    // Normal physics update...
    auto& pc = registry.ctx().get<PhysicsConfig>();
    // ...
}
```

The `find` call returns `nullptr` if `EditorContext` is not registered (e.g. in a standalone game build with no editor). This means the editor mode check has zero cost in release builds where the context is never emplaced.

---

## Step 3: Script Hot-Reload

### The Problem in Detail

Chapter 49's `reload_scripts` console command works but requires manual action. The developer edits `door.lua`, saves, alt-tabs to the engine, opens the console, types `reload_scripts`, presses enter. This breaks flow. We need the engine to detect file changes and reload automatically.

### FileWatcher

File watching is a cross-platform problem. Windows has `ReadDirectoryChangesW`, Linux has `inotify`, macOS has `FSEvents`. For a tutorial engine, we use the portable (if slightly less efficient) approach: `std::filesystem::last_write_time` polled once per second.

```cpp
// engine/core/file_watcher.h — NEW

#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>
#include <iostream>

namespace fs = std::filesystem;

class FileWatcher {
public:
    using ChangeCallback = std::function<void(const std::string& path)>;

    // Watch a single file. Callback fires when the file's
    // modification time changes.
    void watchFile(const std::string& path, ChangeCallback callback)
    {
        if (!fs::exists(path)) {
            std::cerr << "[FileWatcher] File not found: " << path << std::endl;
            return;
        }

        WatchEntry entry;
        entry.path          = path;
        entry.lastWriteTime = fs::last_write_time(path);
        entry.callback      = std::move(callback);
        m_entries.push_back(std::move(entry));
    }

    // Watch all files in a directory matching an extension.
    // Example: watchDirectory("assets/scripts", ".lua", callback)
    void watchDirectory(const std::string& directory,
                        const std::string& extension,
                        ChangeCallback callback)
    {
        if (!fs::exists(directory) || !fs::is_directory(directory)) {
            std::cerr << "[FileWatcher] Directory not found: "
                      << directory << std::endl;
            return;
        }

        for (auto& entry : fs::recursive_directory_iterator(directory)) {
            if (entry.is_regular_file() &&
                entry.path().extension() == extension) {
                watchFile(entry.path().string(), callback);
            }
        }

        // Store the directory watch so new files can be detected
        DirWatch dw;
        dw.directory = directory;
        dw.extension = extension;
        dw.callback  = std::move(callback);
        m_dirWatches.push_back(std::move(dw));
    }

    // Call once per frame (or once per second — see pollInterval).
    // Checks modification times and fires callbacks for changed files.
    void poll()
    {
        m_timeSinceLastPoll += m_frameDt;
        if (m_timeSinceLastPoll < m_pollInterval) return;
        m_timeSinceLastPoll = 0.0f;

        // Check existing watches
        for (auto& entry : m_entries) {
            if (!fs::exists(entry.path)) continue;

            auto currentTime = fs::last_write_time(entry.path);
            if (currentTime != entry.lastWriteTime) {
                entry.lastWriteTime = currentTime;
                entry.callback(entry.path);
            }
        }

        // Check for new files in watched directories
        for (auto& dw : m_dirWatches) {
            if (!fs::exists(dw.directory)) continue;

            for (auto& fEntry : fs::recursive_directory_iterator(dw.directory)) {
                if (!fEntry.is_regular_file()) continue;
                if (fEntry.path().extension() != dw.extension) continue;

                std::string path = fEntry.path().string();
                bool alreadyWatched = false;
                for (const auto& e : m_entries) {
                    if (e.path == path) { alreadyWatched = true; break; }
                }
                if (!alreadyWatched) {
                    watchFile(path, dw.callback);
                }
            }
        }
    }

    void setFrameDt(float dt) { m_frameDt = dt; }
    void setPollInterval(float seconds) { m_pollInterval = seconds; }

private:
    struct WatchEntry {
        std::string                  path;
        fs::file_time_type           lastWriteTime;
        ChangeCallback               callback;
    };

    struct DirWatch {
        std::string    directory;
        std::string    extension;
        ChangeCallback callback;
    };

    std::vector<WatchEntry> m_entries;
    std::vector<DirWatch>   m_dirWatches;
    float m_frameDt          = 0.016f;
    float m_timeSinceLastPoll = 0.0f;
    float m_pollInterval      = 1.0f;   // check once per second
};
```

### Integrating Script Hot-Reload

The `ScriptSystem` now owns a `FileWatcher` and automatically reloads scripts when their files change:

```cpp
// engine/scripting/script_system.h — updated

#pragma once

#include "engine/core/file_watcher.h"

#include <entt/entt.hpp>
#include <string>
#include <unordered_set>

class ScriptManager;

class ScriptSystem {
public:
    void init(entt::registry& registry, ScriptManager& scriptMgr);
    void update(float dt);
    void shutdown();

    // Enable hot-reload for scripts in a directory
    void enableHotReload(const std::string& scriptDirectory);

private:
    entt::registry* m_registry  = nullptr;
    ScriptManager*  m_scriptMgr = nullptr;
    FileWatcher     m_fileWatcher;

    // Files that changed since last update — batched to avoid
    // reloading the same file multiple times per frame
    std::unordered_set<std::string> m_pendingReloads;

    void initScript(entt::entity entity, LuaScript& script);
    void reloadScript(entt::entity entity, LuaScript& script);
    void onScriptRemoved(entt::registry& reg, entt::entity entity);
};
```

```cpp
// engine/scripting/script_system.cpp — updated

#include "engine/scripting/script_system.h"
#include "engine/scripting/script_manager.h"
#include "engine/ecs/components.h"

#include <iostream>
#include <algorithm>

void ScriptSystem::init(entt::registry& registry, ScriptManager& scriptMgr)
{
    m_registry  = &registry;
    m_scriptMgr = &scriptMgr;

    registry.on_destroy<LuaScript>()
        .connect<&ScriptSystem::onScriptRemoved>(*this);
}

void ScriptSystem::enableHotReload(const std::string& scriptDirectory)
{
    m_fileWatcher.watchDirectory(scriptDirectory, ".lua",
        [this](const std::string& changedPath) {
            std::cout << "[Script] File changed: " << changedPath << std::endl;
            m_pendingReloads.insert(changedPath);
        }
    );
}

void ScriptSystem::update(float dt)
{
    // ─── Poll file watcher ──────────────────────────────────
    m_fileWatcher.setFrameDt(dt);

#ifndef NDEBUG
    m_fileWatcher.poll();
#endif

    // ─── Process pending reloads ────────────────────────────
    if (!m_pendingReloads.empty()) {
        auto view = m_registry->view<LuaScript>();
        for (auto [entity, script] : view.each()) {
            // Normalise paths for comparison
            std::string normalised = script.scriptPath;
            std::replace(normalised.begin(), normalised.end(), '\\', '/');

            for (const auto& changed : m_pendingReloads) {
                std::string changedNorm = changed;
                std::replace(changedNorm.begin(), changedNorm.end(), '\\', '/');

                if (normalised == changedNorm ||
                    changedNorm.find(normalised) != std::string::npos ||
                    normalised.find(changedNorm) != std::string::npos)
                {
                    reloadScript(entity, script);
                    break;
                }
            }
        }
        m_pendingReloads.clear();
    }

    // ─── Update scripts ─────────────────────────────────────
    m_scriptMgr->getLuaState()["dt"] = dt;

    auto view = m_registry->view<LuaScript>();
    for (auto [entity, script] : view.each()) {
        if (!script.initialised) {
            initScript(entity, script);
            if (!script.initialised) continue;
        }

        script.environment["self"] = entity;

        sol::function onUpdate = script.environment["on_update"];
        if (onUpdate.valid()) {
            auto result = onUpdate(dt);
            if (!result.valid()) {
                sol::error err = result;
                std::cerr << "[Script] on_update error in "
                          << script.scriptPath << ": "
                          << err.what() << std::endl;
            }
        }
    }
}

void ScriptSystem::reloadScript(entt::entity entity, LuaScript& script)
{
    // Call on_destroy for the old script instance
    if (script.initialised) {
        sol::function onDestroy = script.environment["on_destroy"];
        if (onDestroy.valid()) {
            auto result = onDestroy();
            if (!result.valid()) {
                sol::error err = result;
                std::cerr << "[Script] on_destroy error during reload: "
                          << err.what() << std::endl;
            }
        }
    }

    // Re-initialise. If the new script has errors, the old state
    // is gone but the entity keeps its script component. The error
    // is logged and on_update will be a no-op until the file is
    // fixed and saved again (triggering another reload).
    script.initialised = false;
    initScript(entity, script);

    if (script.initialised) {
        std::cout << "[Script] Successfully reloaded: "
                  << script.scriptPath << std::endl;
    } else {
        std::cerr << "[Script] Reload failed (syntax error?): "
                  << script.scriptPath << std::endl;
        std::cerr << "[Script] Fix the file and save again to retry."
                  << std::endl;
    }
}

void ScriptSystem::initScript(entt::entity entity, LuaScript& script)
{
    auto& lua = m_scriptMgr->getLuaState();

    script.environment = sol::environment(lua, sol::create, lua.globals());
    script.environment["self"] = entity;

    try {
        lua.script_file(script.scriptPath, script.environment);
    }
    catch (const sol::error& e) {
        std::cerr << "[Script] Error loading " << script.scriptPath
                  << ": " << e.what() << std::endl;
        return;
    }

    sol::function onCreate = script.environment["on_create"];
    if (onCreate.valid()) {
        auto result = onCreate();
        if (!result.valid()) {
            sol::error err = result;
            std::cerr << "[Script] on_create error in "
                      << script.scriptPath << ": "
                      << err.what() << std::endl;
        }
    }

    script.initialised = true;
}

void ScriptSystem::shutdown()
{
    auto view = m_registry->view<LuaScript>();
    for (auto [entity, script] : view.each()) {
        if (script.initialised) {
            sol::function onDestroy = script.environment["on_destroy"];
            if (onDestroy.valid()) {
                onDestroy();
            }
        }
    }
}

void ScriptSystem::onScriptRemoved(entt::registry& reg, entt::entity entity)
{
    if (reg.all_of<LuaScript>(entity)) {
        auto& script = reg.get<LuaScript>(entity);
        if (script.initialised) {
            sol::function onDestroy = script.environment["on_destroy"];
            if (onDestroy.valid()) {
                onDestroy();
            }
        }
    }
}
```

### Error Recovery

The reload system is designed to be resilient. If a developer saves a Lua file with a syntax error:

1. The file watcher detects the change.
2. `reloadScript` calls `on_destroy` on the old instance.
3. `initScript` tries to execute the broken file, catches the `sol::error`, and logs it.
4. `script.initialised` remains `false`.
5. On subsequent frames, `on_update` is skipped for this entity.
6. The developer fixes the syntax error and saves again.
7. The file watcher detects the second change.
8. `reloadScript` tries again and succeeds.
9. The entity resumes normal behaviour.

The engine never crashes. The worst case is a single entity stops updating until its script is fixed.

### Integration

```cpp
// In Game::init()
auto& scriptSystem = registry.ctx().emplace<ScriptSystem>();
scriptSystem.init(registry, scriptMgr);
scriptSystem.enableHotReload("assets/scripts");

// In Game::update(float dt)
auto& scriptSystem = registry.ctx().get<ScriptSystem>();
scriptSystem.update(dt);
```

---

## Step 4: Asset Dependency Graph

### The Problem in Detail

Chapter 50 built a manifest with dependency arrays. The manifest records that `level_01` depends on `brick_wall`, `stone_floor`, `enemy_grunt`, and `shotgun`. But the runtime never uses this information. Loading a level means manually calling `loadTexture` and `loadMesh` for every asset it references.

We need two things:

1. **Runtime dependency loading:** when `ResourceManager` loads a level, it automatically loads all transitive dependencies.
2. **Build-time dependency tracking:** when a texture changes, the asset compiler knows which levels and meshes reference it and can rebuild them.

### AssetDependencyGraph

The manifest from Chapter 50 already stores dependency lists. We build a proper graph structure on top of it that supports both forward queries ("what does this asset depend on?") and reverse queries ("what depends on this asset?").

```cpp
// engine/assets/asset_dependency_graph.h — NEW

#pragma once

#include "engine/assets/asset_manifest.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <iostream>

class AssetDependencyGraph {
public:
    // ─── Build from manifest ────────────────────────────────
    // Constructs the forward and reverse adjacency lists from
    // the manifest's dependency data.

    void buildFromManifest(const AssetManifest& manifest)
    {
        m_forward.clear();
        m_reverse.clear();

        // The manifest stores entries with dependencies.
        // We iterate all assets and build both directions.
        for (size_t i = 0; i < manifest.assetCount(); ++i) {
            // We need to iterate by name — use the manifest's
            // internal structure through the public API.
            // For each asset, getDependencies returns its children.
        }

        // Since AssetManifest does not expose an iteration API,
        // we build the graph during manifest loading instead.
        // See buildFromEntries below.
    }

    void buildFromEntries(
        const std::vector<std::pair<std::string, std::vector<std::string>>>& entries)
    {
        m_forward.clear();
        m_reverse.clear();

        for (const auto& [name, deps] : entries) {
            m_forward[name] = deps;
            for (const auto& dep : deps) {
                m_reverse[dep].push_back(name);
            }
        }
    }

    // ─── Forward query: what does this asset depend on? ─────
    // Returns all transitive dependencies (breadth-first).

    std::vector<std::string> getAllDependencies(
        const std::string& assetName) const
    {
        std::vector<std::string> result;
        std::unordered_set<std::string> visited;
        std::queue<std::string> queue;

        auto it = m_forward.find(assetName);
        if (it == m_forward.end()) return result;

        for (const auto& dep : it->second) {
            if (visited.insert(dep).second) {
                queue.push(dep);
            }
        }

        while (!queue.empty()) {
            std::string current = queue.front();
            queue.pop();
            result.push_back(current);

            auto fwd = m_forward.find(current);
            if (fwd != m_forward.end()) {
                for (const auto& dep : fwd->second) {
                    if (visited.insert(dep).second) {
                        queue.push(dep);
                    }
                }
            }
        }

        return result;
    }

    // ─── Reverse query: what depends on this asset? ─────────
    // Returns all assets that transitively depend on the given one.
    // Used by the asset compiler to determine what to rebuild when
    // a source file changes.

    std::vector<std::string> getDependents(
        const std::string& assetName) const
    {
        std::vector<std::string> result;
        std::unordered_set<std::string> visited;
        std::queue<std::string> queue;

        auto it = m_reverse.find(assetName);
        if (it == m_reverse.end()) return result;

        for (const auto& dep : it->second) {
            if (visited.insert(dep).second) {
                queue.push(dep);
            }
        }

        while (!queue.empty()) {
            std::string current = queue.front();
            queue.pop();
            result.push_back(current);

            auto rev = m_reverse.find(current);
            if (rev != m_reverse.end()) {
                for (const auto& dep : rev->second) {
                    if (visited.insert(dep).second) {
                        queue.push(dep);
                    }
                }
            }
        }

        return result;
    }

    // ─── Rebuild list: given a set of changed source files, ──
    // determine the minimal set of assets to rebuild.

    std::unordered_set<std::string> getAssetsToRebuild(
        const std::vector<std::string>& changedAssets) const
    {
        std::unordered_set<std::string> toRebuild;

        for (const auto& changed : changedAssets) {
            toRebuild.insert(changed);

            auto dependents = getDependents(changed);
            for (const auto& dep : dependents) {
                toRebuild.insert(dep);
            }
        }

        return toRebuild;
    }

    // ─── Topological sort: load order ────────────────────────
    // Returns assets in dependency order (leaves first).
    // Assets with no dependencies come first, assets that depend
    // on others come after their dependencies.

    std::vector<std::string> getLoadOrder(
        const std::vector<std::string>& assets) const
    {
        // Build in-degree map for the subset
        std::unordered_map<std::string, int> inDegree;
        std::unordered_set<std::string> assetSet(assets.begin(), assets.end());

        for (const auto& name : assets) {
            if (inDegree.find(name) == inDegree.end()) {
                inDegree[name] = 0;
            }
            auto it = m_forward.find(name);
            if (it != m_forward.end()) {
                for (const auto& dep : it->second) {
                    if (assetSet.count(dep)) {
                        inDegree[name]++;
                    }
                }
            }
        }

        // Kahn's algorithm
        std::queue<std::string> ready;
        for (const auto& [name, degree] : inDegree) {
            if (degree == 0) ready.push(name);
        }

        std::vector<std::string> result;
        while (!ready.empty()) {
            std::string current = ready.front();
            ready.pop();
            result.push_back(current);

            // Reduce in-degree of dependents
            auto it = m_reverse.find(current);
            if (it != m_reverse.end()) {
                for (const auto& dependent : it->second) {
                    if (assetSet.count(dependent)) {
                        inDegree[dependent]--;
                        if (inDegree[dependent] == 0) {
                            ready.push(dependent);
                        }
                    }
                }
            }
        }

        return result;
    }

private:
    // Forward: asset → [assets it depends on]
    std::unordered_map<std::string, std::vector<std::string>> m_forward;

    // Reverse: asset → [assets that depend on it]
    std::unordered_map<std::string, std::vector<std::string>> m_reverse;
};
```

### Dependency-Aware Loading in ResourceManager

The `ResourceManager` now uses the dependency graph to load all required assets automatically:

```cpp
// engine/resource_manager.h — updated loadLevel method

// Before (Chapter 50) — manual asset listing
void ResourceManager::loadLevel(const std::string& levelName)
{
    // Hardcoded list — fragile, must be maintained manually
    loadTexture("brick_wall");
    loadTexture("stone_floor");
    loadMesh("enemy_grunt");
    // ... more manual loads ...

    // Load the level file itself
    loadLevelData(levelName);
}

// After (Chapter 50a) — dependency-driven loading
void ResourceManager::loadLevel(const std::string& levelName)
{
    // Get all transitive dependencies from the graph
    auto deps = m_depGraph.getAllDependencies(levelName);

    // Sort into load order (textures before meshes that use them)
    auto loadOrder = m_depGraph.getLoadOrder(deps);

    // Load each dependency by type
    for (const auto& assetName : loadOrder) {
        std::string type = m_manifest.getType(assetName);

        if (type == "texture") {
            loadTexture(assetName);
        } else if (type == "mesh") {
            loadMesh(assetName);
        } else if (type == "effect") {
            loadEffect(assetName);
        } else if (type == "animation") {
            loadAnimation(assetName);
        }
        // Other types pass through or are handled by specific systems
    }

    // Load the level file itself
    loadLevelData(levelName);

    std::cout << "[ResourceManager] Loaded level '" << levelName
              << "' with " << loadOrder.size() << " dependencies"
              << std::endl;
}
```

### Incremental Rebuild in the Asset Compiler

The asset compiler from Chapter 50 already did incremental rebuilds based on file hashes. With the dependency graph, it can now cascade rebuilds. If `enemy_grunt_diffuse.png` changes, the compiler knows it must also reprocess `enemy_grunt.qmesh` (which references the texture) and any level that references the enemy.

```cpp
// tools/asset_compiler/main.cpp — updated rebuild logic

// Before (Chapter 50) — only checks individual file hashes
if (!forceRebuild && !manifest.needsRebuild(assetName, fileHash)) {
    continue;  // skip unchanged assets
}

// After (Chapter 50a) — cascade through dependency graph
AssetDependencyGraph depGraph;
depGraph.buildFromEntries(manifest.getDependencyPairs());

// Find all changed source files
std::vector<std::string> changedAssets;
for (const auto& asset : allAssets) {
    std::string hash = computeFileHash(asset.sourcePath);
    if (manifest.needsRebuild(asset.name, hash)) {
        changedAssets.push_back(asset.name);
    }
}

// Expand to include everything that depends on changed assets
auto toRebuild = depGraph.getAssetsToRebuild(changedAssets);

std::cout << "[AssetCompiler] " << changedAssets.size()
          << " files changed, " << toRebuild.size()
          << " assets to rebuild (including dependents)"
          << std::endl;

// Process only assets in the rebuild set
for (const auto& asset : allAssets) {
    if (!forceRebuild && toRebuild.find(asset.name) == toRebuild.end()) {
        continue;  // not affected by any changes
    }
    // ... compile asset ...
}
```

### Build Manifest for Release

For release packaging, the dependency graph produces a complete list of every asset referenced by the game's levels. Unreferenced assets (test textures, work-in-progress models) are excluded from the build:

```cpp
// tools/asset_compiler/release_builder.h — NEW

#pragma once

#include "engine/assets/asset_dependency_graph.h"

#include <string>
#include <vector>
#include <unordered_set>
#include <iostream>

namespace ReleaseBuilder {

// Given a list of level names, compute the complete set of assets
// needed for a release build. Copies only these assets to the
// output directory.
inline std::vector<std::string> computeReleaseAssets(
    const AssetDependencyGraph& depGraph,
    const std::vector<std::string>& levelNames)
{
    std::unordered_set<std::string> needed;

    for (const auto& level : levelNames) {
        needed.insert(level);

        auto deps = depGraph.getAllDependencies(level);
        for (const auto& dep : deps) {
            needed.insert(dep);
        }
    }

    std::vector<std::string> result(needed.begin(), needed.end());
    auto loadOrder = depGraph.getLoadOrder(result);

    std::cout << "[Release] " << levelNames.size() << " levels require "
              << loadOrder.size() << " total assets" << std::endl;

    return loadOrder;
}

} // namespace ReleaseBuilder
```

---

## Step 5: Updated Initialisation

Here is how everything comes together at startup. Compare this to the scattered initialisation from Chapters 47-50:

```cpp
// In Game::init() — Chapter 50a consolidated initialisation

// ─── 1. Config ──────────────────────────────────────────────
auto& config = registry.ctx().emplace<ConfigManager>();
config.load("assets/scripts/config.lua");

// ─── 2. Physics (loaded from config) ───────────────────────
PhysicsConfig pc = loadPhysicsConfig(config);
registry.ctx().emplace<PhysicsConfig>(pc);

// Register for hot-reload
config.onReload([&registry](const ConfigManager& cfg) {
    auto& pc = registry.ctx().get<PhysicsConfig>();
    pc.gravity      = cfg.get<float>("physics.gravity", -9.81f);
    pc.substeps     = cfg.get<int>("physics.substeps", 4);
    pc.jumpForce    = cfg.get<float>("physics.jump_force", 8.0f);
    pc.friction     = cfg.get<float>("physics.friction", 6.0f);
    pc.airControl   = cfg.get<float>("physics.air_control", 0.3f);
    pc.maxFallSpeed = cfg.get<float>("physics.max_fall_speed", -50.0f);
});

// ─── 3. Editor context ─────────────────────────────────────
auto& edCtx = registry.ctx().emplace<EditorContext>();
edCtx.snapSize    = config.get<float>("editor.grid_size", 1.0f);
edCtx.snapEnabled = config.get<bool>("editor.snap_enabled", true);

// ─── 4. Scripting ───────────────────────────────────────────
auto& scriptMgr = registry.ctx().emplace<ScriptManager>();
scriptMgr.init(registry, console);

auto& scriptSystem = registry.ctx().emplace<ScriptSystem>();
scriptSystem.init(registry, scriptMgr);
scriptSystem.enableHotReload("assets/scripts");

// ─── 5. Resource management with dependency graph ───────────
auto& resourceMgr = registry.ctx().emplace<ResourceManager>();
resourceMgr.init(true);  // prefer cooked assets

// Build dependency graph from manifest
if (resourceMgr.hasManifest()) {
    auto& depGraph = registry.ctx().emplace<AssetDependencyGraph>();
    depGraph.buildFromEntries(resourceMgr.getDependencyPairs());
}

// ─── 6. Particle defs (loaded from config) ──────────────────
auto explosionDef =
    ParticleEffectLoader::loadFromConfig(config, "particles.explosion");
auto muzzleDef =
    ParticleEffectLoader::loadFromConfig(config, "particles.muzzle_flash");
auto smokeDef =
    ParticleEffectLoader::loadFromConfig(config, "particles.smoke");
// Store these in whatever container the particle system uses
```

And in the game loop:

```cpp
// In Game::update(float dt)

// Config hot-reload (debug builds only)
#ifndef NDEBUG
    auto& config = registry.ctx().get<ConfigManager>();
    config.pollForChanges();
#endif

// Script system (includes file watcher polling)
auto& scriptSystem = registry.ctx().get<ScriptSystem>();
scriptSystem.update(dt);

// Physics (skipped in editor editing mode)
physicsSystem(registry, dt);

// ... rest of update ...
```

### File Summary

Here is every file touched or created in this chapter:

| File | Change |
|---|---|
| `engine/scripting/config_manager.h` | **Rewritten.** Dot-path `get<T>()` replaces three separate methods. Added `reload()`, `pollForChanges()`, `onReload()` callback registration. |
| `engine/scripting/config_manager.cpp` | **Rewritten.** Implements `resolve()` for dot-path traversal, `reload()` with callback notification, `pollForChanges()` with `last_write_time`. |
| `engine/editor/editor_context.h` | **New.** `EditorContext` struct with selection, gizmo mode, snap settings, undo stack, mode enum. |
| `engine/editor/editor_state.h` | **Simplified.** Removed `SelectionManager`, `UndoStack`, `GizmoMode`, snap settings, level path, unsaved flag. These moved to `EditorContext`. |
| `engine/core/file_watcher.h` | **New.** `FileWatcher` class with file and directory watching, polling-based change detection. |
| `engine/scripting/script_system.h` | **Updated.** Added `FileWatcher` member, `enableHotReload()`, `reloadScript()`, pending reload set. |
| `engine/scripting/script_system.cpp` | **Updated.** Automatic hot-reload with error recovery. Pending reload batching. |
| `engine/assets/asset_dependency_graph.h` | **New.** Forward and reverse dependency queries, `getAssetsToRebuild()`, topological sort for load order. |
| `engine/particles/particle_effect_loader.h` | **New.** `ParticleEffectLoader::loadFromConfig()` — loads `ParticleEffectDef` from Lua config paths. |
| `tools/asset_compiler/release_builder.h` | **New.** `computeReleaseAssets()` — uses dependency graph to determine minimal release asset set. |
| `assets/scripts/config.lua` | **Expanded.** Added particle parameters, weapon recoil values, HUD layout positions, editor defaults, screen effect values. |

---

## C++ Concept: File Watchers and Platform Abstraction

The `FileWatcher` in this chapter uses `std::filesystem::last_write_time` polled once per second. This is portable and simple but has a cost: on a project with 500 scripts, checking 500 file timestamps every second is measurable (a few hundred microseconds). For a tutorial engine, this is fine. But it is worth understanding what production engines do differently.

### Platform-Native File Watching

Every major operating system provides a kernel-level notification API for file system changes:

| Platform | API | How It Works |
|---|---|---|
| Windows | `ReadDirectoryChangesW` | Registers a directory with the OS. The kernel buffers change events (create, modify, delete, rename) and delivers them to the application asynchronously. |
| Linux | `inotify` | Creates a watch descriptor for a file or directory. The kernel pushes events to a file descriptor that the application reads. |
| macOS | `FSEvents` | Registers for a stream of file system events. The OS coalesces changes and delivers them in batches. |

These APIs are event-driven rather than polling-driven. The application does no work until a file actually changes. There is no per-frame cost when nothing has changed.

### The Abstraction Problem

If you want to support all three platforms, you need three different implementations behind a common interface. This is a classic abstraction boundary:

```cpp
// The interface all platforms implement
class PlatformFileWatcher {
public:
    virtual ~PlatformFileWatcher() = default;
    virtual void watchDirectory(const std::string& path) = 0;
    virtual std::vector<FileChangeEvent> pollEvents() = 0;
};

// Platform-specific implementations
#ifdef _WIN32
class Win32FileWatcher : public PlatformFileWatcher { /* ... */ };
#elif __linux__
class InotifyFileWatcher : public PlatformFileWatcher { /* ... */ };
#elif __APPLE__
class FSEventsFileWatcher : public PlatformFileWatcher { /* ... */ };
#endif
```

This is the same pattern used for windowing (GLFW abstracts Win32/X11/Cocoa), audio (OpenAL/FMOD abstract DirectSound/CoreAudio/ALSA), and input (GLFW abstracts XInput/evdev/IOKit). Platform-specific code hides behind a stable interface so the rest of the engine never needs to know which OS it is running on.

For QEngine, `std::filesystem::last_write_time` is the right choice. It is standard C++17, works everywhere, and the polling cost is negligible for a project with fewer than a thousand watched files. If you later need native file watching -- perhaps because your asset pipeline watches 10,000 files -- the `FileWatcher` interface is already defined. Swap the implementation, keep the interface.

### A Note on std::filesystem Portability

`std::filesystem` is part of the C++17 standard, but its behaviour varies across compilers and platforms. The `last_write_time` function returns a `file_time_type` whose clock type is implementation-defined. Comparing two `file_time_type` values from the same program run is safe (which is all we do). Converting them to wall-clock time (`system_clock`) requires C++20's `clock_cast` or platform-specific code. We avoid that by only comparing timestamps, never displaying them.

---

## What's Next

The tools and pipeline layer is now structured for productive iteration. The `ConfigManager` provides a single file for every tweakable value in the engine, with hot-reload in debug builds so changes take effect immediately. `EditorContext` gives every editor subsystem access to shared state through the registry, following the same pattern we use for physics, particles, and audio. The `FileWatcher` detects script changes and reloads them automatically, with graceful error recovery. And the `AssetDependencyGraph` ensures that loading a level automatically pulls in everything it needs, while the asset compiler rebuilds only what has actually changed.

In **Chapter 51: Level of Detail**, we will:

- Implement LOD mesh selection based on distance from camera
- Precompute LOD meshes in the asset compiler using mesh simplification
- Build a LOD system that swaps mesh detail levels seamlessly at runtime
- Integrate LOD transitions with the frustum culling from Chapter 32
- Explore continuous LOD techniques for terrain rendering
