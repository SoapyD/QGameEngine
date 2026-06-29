# Chapter 49: Lua Scripting

## What You'll Learn
- Why scripting languages matter for game engines and when to use them instead of C++
- Embedding Lua into a C++ application using the sol2 binding library
- Using Lua as a configuration language to replace hardcoded engine values
- Loading game settings (camera speed, gravity, mouse sensitivity, weapon damage) from `.lua` files
- Binding ECS entities and components to Lua so scripts can read and modify game state
- A LuaScript component and ScriptSystem that tick per-entity Lua behaviours every frame
- Writing gameplay scripts: interactive doors, enemy patrol routes, custom pickup effects
- Executing Lua from the developer console for live game manipulation
- C++ concepts: embedding vs extending, host-driven vs script-driven architectures

---

## Where We Left Off

Chapter 48 gave us a level editor. We can place entities, set properties, save and load maps. Debug tools from Chapter 27 let us inspect and modify the running game. The game state machine from Chapter 21 manages transitions between menu, gameplay, pause, and editor modes.

But every piece of gameplay logic lives in C++. Want to change how a door opens? Recompile. Want to tweak enemy patrol timing? Recompile. Want a designer to prototype a new pickup effect? They need a C++ development environment, knowledge of the build system, and ten minutes every time they change a number.

This is the same problem we solved for particle effects in Chapter 46 (data-driven definitions) and for levels in Chapter 48 (editor-driven placement). The pattern is consistent: move authoring out of compiled code and into something a human can edit quickly. For configuration values and gameplay behaviour, that something is a scripting language.

```
CURRENT WORKFLOW (all C++)
────────────────────────────────────────────────────────────
  Designer: "The shotgun does too much damage at range."
  Programmer: *opens weapon_system.cpp... finds damage falloff...*
  Programmer: *changes 0.8f to 0.5f... saves... recompiles...*
  Programmer: "Try it now." (2 minutes later)
  Designer: "Better, but the spread needs to be wider too."
  Programmer: *sighs... finds spread constant... recompiles...*

TARGET WORKFLOW (Lua config + scripts)
────────────────────────────────────────────────────────────
  Designer: *opens config.lua... changes shotgun_damage_falloff = 0.5...*
  Designer: *saves... game hot-reloads config... tests immediately*
  Designer: *adjusts spread_angle = 12.0... saves... tests again*
  Programmer: (working on the renderer)
```

---

## Why Lua?

There are many embeddable scripting languages. Lua has won in gamedev for practical reasons:

1. **Tiny footprint.** The entire Lua interpreter is around 300 KB compiled. It adds almost nothing to your binary.
2. **Fast.** Lua is one of the fastest interpreted languages. LuaJIT (a drop-in replacement) approaches native speed for numerical code.
3. **Clean C API.** Lua was designed from day one to be embedded in C and C++ programs. The API is stack-based and straightforward.
4. **Battle-tested.** World of Warcraft, Roblox, Garry's Mod, CryEngine, Corona SDK, Redis, Nginx, Neovim. Lua is everywhere.
5. **Started as a configuration language.** This matters. Lua was created at PUC-Rio in 1993 specifically as a data-description language for configuring engineering applications. The scripting capabilities came later. This heritage means Lua is naturally excellent at the thing we need first: replacing hardcoded numbers with editable config files.

The last point deserves emphasis. Before we write a single gameplay script, we are going to use Lua for what it was originally built for: configuration.

---

## Adding sol2 to the Project

sol2 is a header-only C++ library that wraps Lua's C API with modern C++ types. It handles type conversion, error checking, and binding automatically. Without it, every function binding requires manual stack manipulation. With it, exposing a C++ function to Lua is a single line.

### Getting the Libraries

Download Lua 5.4 source from https://www.lua.org/download.html and sol2 from https://github.com/ThePhD/sol2. Place them in your extern directory:

```
extern/
  lua/
    lua.h
    lualib.h
    lauxlib.h
    lua.c, lapi.c, ... (all .c source files)
  sol/
    sol.hpp
    forward.hpp
    config.hpp
    ... (all sol2 headers)
```

### CMake Setup

```cmake
# In CMakeLists.txt

# Build Lua as a static library from source
file(GLOB LUA_SOURCES extern/lua/*.c)
# Remove lua.c and luac.c — those are the standalone interpreter/compiler mains
list(FILTER LUA_SOURCES EXCLUDE REGEX "lua\\.c$")
list(FILTER LUA_SOURCES EXCLUDE REGEX "luac\\.c$")

add_library(lua STATIC ${LUA_SOURCES})
target_include_directories(lua PUBLIC extern/lua)

# sol2 is header-only, just needs to find Lua
add_library(sol2 INTERFACE)
target_include_directories(sol2 INTERFACE extern/sol)
target_link_libraries(sol2 INTERFACE lua)

# Link to QEngine
target_link_libraries(QEngine PRIVATE ... sol2)
```

### Verifying the Setup

A quick test to make sure everything compiles:

```cpp
// Temporary test — put this in main.cpp and remove after verifying
#include <sol/sol.hpp>
#include <iostream>

void testLua()
{
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string);

    lua.script("print('Hello from Lua!')");

    lua["engine_name"] = "QEngine";
    lua.script("print('Running inside: ' .. engine_name)");

    // Call a Lua function from C++
    lua.script(R"(
        function add(a, b)
            return a + b
        end
    )");

    sol::function add = lua["add"];
    int result = add(3, 7);
    std::cout << "3 + 7 = " << result << std::endl;  // 3 + 7 = 10
}
```

If that compiles and prints three lines, sol2 and Lua are working. Remove the test code and let us build the real system.

---

## Lua as Configuration

Before we do anything clever with gameplay scripting, we are going to solve a simpler problem: the engine is full of hardcoded numbers.

Look at the codebase. Camera speed is a constant somewhere in the camera system. Mouse sensitivity is a magic number in input handling. Gravity is `-9.81f` written directly in physics update functions. Jump force, weapon damage values, HUD margins, audio volumes — all of these are scattered across `.cpp` files. Changing any of them requires finding the right file, editing the right line, and recompiling.

```
HARDCODED VALUES SCATTERED ACROSS THE ENGINE
────────────────────────────────────────────────────────────
  camera_system.cpp        → float moveSpeed = 10.0f;
  input_manager.cpp        → float sensitivity = 0.1f;
  physics_system.cpp       → glm::vec3 gravity(0, -9.81f, 0);
  player_system.cpp        → float jumpForce = 8.0f;
  weapon_system.cpp        → float shotgunDamage = 80.0f;
  hud_system.cpp           → float crosshairSize = 16.0f;
  audio_manager.cpp        → float masterVolume = 0.8f;
```

All of these should live in one place, editable without recompilation.

### The Config File

Create `assets/scripts/config.lua`:

```lua
-- assets/scripts/config.lua
-- Engine configuration file
-- Edit these values and restart (or hot-reload) to apply changes.
-- No recompilation needed.

-- Camera
camera_speed          = 10.0
camera_sprint_mult    = 2.0
mouse_sensitivity     = 0.1
fov                   = 90.0

-- Physics
physics = {
    gravity       = -9.81,
    jump_force    = 8.0,
    friction      = 6.0,
    air_control   = 0.3,
    max_fall_speed = -50.0,
}

-- Player
player = {
    max_health    = 100,
    walk_speed    = 7.0,
    sprint_speed  = 12.0,
    crouch_speed  = 3.5,
    step_height   = 0.5,
}

-- Weapons
weapons = {
    shotgun = {
        damage          = 80,
        pellet_count    = 8,
        spread_angle    = 6.0,
        range           = 30.0,
        damage_falloff  = 0.5,
        fire_rate       = 0.8,
    },
    nailgun = {
        damage          = 15,
        fire_rate       = 0.1,
        speed           = 40.0,
        range           = 100.0,
    },
    rocket_launcher = {
        damage          = 120,
        splash_radius   = 5.0,
        splash_falloff  = 0.4,
        fire_rate       = 0.8,
        projectile_speed = 25.0,
    },
}

-- HUD
hud = {
    crosshair_size    = 16.0,
    crosshair_gap     = 4.0,
    crosshair_color   = { 0.0, 1.0, 0.0, 0.8 },
    health_bar_width  = 200.0,
    health_bar_height = 20.0,
    ammo_font_size    = 24.0,
    message_duration  = 3.0,
}

-- Audio
audio = {
    master_volume  = 0.8,
    music_volume   = 0.5,
    sfx_volume     = 1.0,
    footstep_interval = 0.4,
}

-- Debug
debug = {
    show_fps       = true,
    show_colliders = false,
    show_navmesh   = false,
    god_mode       = false,
    noclip         = false,
}
```

Notice: this is valid Lua. Tables (the `{ }` syntax) are Lua's single data structure — they serve as arrays, dictionaries, and objects. A Lua config file is just a script that assigns values to global variables. When the engine executes this file, those variables become available to read from C++.

### The ConfigManager

We need a class that loads this config file, stores the Lua state, and provides typed access to values. This is a thin wrapper — the Lua state itself is the config store.

### src/engine/scripting/config_manager.h

```cpp
#pragma once

#include <sol/sol.hpp>
#include <string>
#include <glm/glm.hpp>
#include <iostream>

class ConfigManager {
public:
    bool load(const std::string& filepath);

    // Get a simple value: config.get<float>("camera_speed")
    template<typename T>
    T get(const std::string& key, T defaultValue = T{}) const
    {
        sol::object obj = m_lua[key];
        if (obj.valid() && obj.is<T>()) {
            return obj.as<T>();
        }
        return defaultValue;
    }

    // Get a nested value: config.getNested<float>("physics", "gravity")
    template<typename T>
    T getNested(const std::string& table, const std::string& key,
                T defaultValue = T{}) const
    {
        sol::optional<sol::table> tbl = m_lua[table];
        if (tbl) {
            sol::object obj = (*tbl)[key];
            if (obj.valid() && obj.is<T>()) {
                return obj.as<T>();
            }
        }
        return defaultValue;
    }

    // Get a deeply nested value: config.getDeep<int>("weapons", "shotgun", "damage")
    template<typename T>
    T getDeep(const std::string& t1, const std::string& t2,
              const std::string& key, T defaultValue = T{}) const
    {
        sol::optional<sol::table> tbl1 = m_lua[t1];
        if (tbl1) {
            sol::optional<sol::table> tbl2 = (*tbl1)[t2];
            if (tbl2) {
                sol::object obj = (*tbl2)[key];
                if (obj.valid() && obj.is<T>()) {
                    return obj.as<T>();
                }
            }
        }
        return defaultValue;
    }

    // Get a colour from a Lua array table {r, g, b, a}
    glm::vec4 getColor(const std::string& table, const std::string& key,
                       glm::vec4 defaultValue = {1, 1, 1, 1}) const;

    // Access the raw Lua state (for advanced use / console integration)
    sol::state& getLuaState() { return m_lua; }
    const sol::state& getLuaState() const { return m_lua; }

private:
    sol::state m_lua;
};
```

### src/engine/scripting/config_manager.cpp

```cpp
#include "engine/scripting/config_manager.h"

bool ConfigManager::load(const std::string& filepath)
{
    m_lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string,
                         sol::lib::table);

    try {
        m_lua.script_file(filepath);
        std::cout << "[Config] Loaded: " << filepath << std::endl;
        return true;
    }
    catch (const sol::error& e) {
        std::cerr << "[Config] Error loading " << filepath << ": "
                  << e.what() << std::endl;
        return false;
    }
}

glm::vec4 ConfigManager::getColor(const std::string& table,
                                   const std::string& key,
                                   glm::vec4 defaultValue) const
{
    sol::optional<sol::table> tbl = m_lua[table];
    if (!tbl) return defaultValue;

    sol::optional<sol::table> colorTbl = (*tbl)[key];
    if (!colorTbl) return defaultValue;

    return glm::vec4(
        colorTbl->get_or(1, defaultValue.r),  // Lua arrays are 1-indexed
        colorTbl->get_or(2, defaultValue.g),
        colorTbl->get_or(3, defaultValue.b),
        colorTbl->get_or(4, defaultValue.a)
    );
}
```

### Using Config Values at Startup

Now we replace hardcoded values across the engine. The ConfigManager loads once at startup, and systems read from it during initialisation.

```cpp
// In Game::init() or wherever you set up the engine

ConfigManager config;
if (!config.load("assets/scripts/config.lua")) {
    std::cerr << "Failed to load config, using defaults" << std::endl;
}

// Store in registry context so all systems can access it
// (Same pattern we've used for AudioManager, InputManager, etc.)
registry.ctx().emplace<ConfigManager>(std::move(config));
```

### Replacing Hardcoded PhysicsConfig

In earlier chapters we had a PhysicsConfig struct with hardcoded values. Now it loads from Lua:

```cpp
// Before (hardcoded in physics_system.cpp or a header)
struct PhysicsConfig {
    float gravity    = -9.81f;
    float jumpForce  = 8.0f;
    float friction   = 6.0f;
    float airControl = 0.3f;
    float maxFallSpeed = -50.0f;
};

// After (loaded from config.lua)
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

// In physics system initialisation:
auto& config = registry.ctx().get<ConfigManager>();
auto physicsConfig = loadPhysicsConfig(config);
registry.ctx().emplace<PhysicsConfig>(physicsConfig);
```

The same pattern applies everywhere:

```cpp
// Camera setup
auto& config = registry.ctx().get<ConfigManager>();
float cameraSpeed    = config.get<float>("camera_speed", 10.0f);
float sensitivity    = config.get<float>("mouse_sensitivity", 0.1f);
float fov            = config.get<float>("fov", 90.0f);

// Weapon setup
float shotgunDamage  = config.getDeep<float>("weapons", "shotgun", "damage", 80.0f);
int   pelletCount    = config.getDeep<int>("weapons", "shotgun", "pellet_count", 8);
float spreadAngle    = config.getDeep<float>("weapons", "shotgun", "spread_angle", 6.0f);

// HUD setup
float crosshairSize  = config.getNested<float>("hud", "crosshair_size", 16.0f);
glm::vec4 crossColor = config.getColor("hud", "crosshair_color", {0, 1, 0, 0.8f});

// Audio setup
float masterVol      = config.getNested<float>("audio", "master_volume", 0.8f);
```

Every `get` call takes a default value. If the config file is missing or a key does not exist, the engine falls back to sane defaults. The game always works, even with a broken or empty config file.

```
CONFIG LOADING FLOW
────────────────────────────────────────────────────────────

  ┌─────────────┐     execute      ┌──────────────┐
  │ config.lua  │ ──────────────→  │  Lua State    │
  │             │    (sol2)        │  (variables)  │
  └─────────────┘                  └──────┬───────┘
                                          │
                     ┌────────────────────┼────────────────────┐
                     │                    │                    │
                     ▼                    ▼                    ▼
              PhysicsConfig        CameraConfig         WeaponDefs
              (gravity, jump)      (speed, FOV)         (damage, rate)
                     │                    │                    │
                     ▼                    ▼                    ▼
              PhysicsSystem        CameraSystem         WeaponSystem
```

This is the first real payoff of embedding Lua. We have not written a single gameplay script yet, and already every tweakable value in the engine is editable without recompilation. Chapter 50a will formalise this further with a dedicated config reload system, but the foundation is here now.

---

## The Script Manager

Configuration was the appetiser. Now we build the system that lets Lua drive gameplay behaviour. We need a central ScriptManager that owns the Lua state, exposes engine APIs to scripts, and manages script lifecycles.

### src/engine/scripting/script_manager.h

```cpp
#pragma once

#include <sol/sol.hpp>
#include <entt/entt.hpp>
#include <string>
#include <unordered_map>

class Console;

class ScriptManager {
public:
    // Initialise Lua state and bind engine APIs
    void init(entt::registry& registry, Console& console);

    // Execute a script file
    bool executeFile(const std::string& filepath);

    // Execute a string of Lua code (for console integration)
    bool executeString(const std::string& code);

    // Call a named Lua function if it exists
    template<typename... Args>
    void callIfExists(const std::string& funcName, Args&&... args)
    {
        sol::function fn = m_lua[funcName];
        if (fn.valid()) {
            auto result = fn(std::forward<Args>(args)...);
            if (!result.valid()) {
                sol::error err = result;
                std::cerr << "[Script] Error in " << funcName << ": "
                          << err.what() << std::endl;
            }
        }
    }

    // Get the Lua state (for components that need direct access)
    sol::state& getLuaState() { return m_lua; }

    // Reload a specific script file (for hot-reload)
    bool reloadFile(const std::string& filepath);

private:
    sol::state m_lua;
    entt::registry* m_registry = nullptr;
    Console* m_console = nullptr;

    // Cached script file contents for reload detection
    std::unordered_map<std::string, std::string> m_loadedScripts;

    // Binding functions — each registers a group of APIs
    void bindMathTypes();
    void bindRegistry();
    void bindComponents();
    void bindInput();
    void bindAudio();
    void bindConsole();
};
```

### src/engine/scripting/script_manager.cpp

This is a large file. We will build it section by section.

```cpp
#include "engine/scripting/script_manager.h"
#include "engine/debug/console.h"
#include "engine/ecs/components.h"
#include "engine/input/input_manager.h"
#include "engine/audio/audio_manager.h"
#include <fstream>
#include <sstream>
#include <iostream>

void ScriptManager::init(entt::registry& registry, Console& console)
{
    m_registry = &registry;
    m_console  = &console;

    // Open standard Lua libraries
    m_lua.open_libraries(
        sol::lib::base,
        sol::lib::math,
        sol::lib::string,
        sol::lib::table,
        sol::lib::io,
        sol::lib::os
    );

    // Bind engine APIs to Lua
    bindMathTypes();
    bindRegistry();
    bindComponents();
    bindInput();
    bindAudio();
    bindConsole();

    // Provide a global 'log' function for scripts
    m_lua.set_function("log", [&console](const std::string& msg) {
        console.print("[Script] " + msg);
        std::cout << "[Script] " << msg << std::endl;
    });

    // Provide dt as a global that the ScriptSystem updates each frame
    m_lua["dt"] = 0.0f;

    std::cout << "[ScriptManager] Initialised" << std::endl;
}

// executeFile and executeString both wrap script execution in try/catch.
// On success they return true. On failure they log the error to stderr
// and the console, then return false. reloadFile simply calls executeFile.

bool ScriptManager::executeFile(const std::string& filepath)
{
    try {
        m_lua.script_file(filepath);
        return true;
    } catch (const sol::error& e) {
        std::cerr << "[Script] Error in " << filepath << ": " << e.what() << std::endl;
        if (m_console) m_console->print("[Script] Error: " + std::string(e.what()));
        return false;
    }
}

bool ScriptManager::executeString(const std::string& code)
{
    try { m_lua.script(code); return true; }
    catch (const sol::error& e) {
        std::cerr << "[Script] Error: " << e.what() << std::endl;
        if (m_console) m_console->print("[Script] Error: " + std::string(e.what()));
        return false;
    }
}
```

---

## Binding Math Types

Lua does not know what a `glm::vec3` is. We need to teach it. sol2 makes this straightforward with user-type bindings:

```cpp
void ScriptManager::bindMathTypes()
{
    // vec3
    auto vec3_type = m_lua.new_usertype<glm::vec3>("vec3",
        sol::constructors<glm::vec3(), glm::vec3(float, float, float)>(),
        "x", &glm::vec3::x,
        "y", &glm::vec3::y,
        "z", &glm::vec3::z
    );

    // vec3 arithmetic
    vec3_type[sol::meta_function::addition] =
        [](const glm::vec3& a, const glm::vec3& b) { return a + b; };
    vec3_type[sol::meta_function::subtraction] =
        [](const glm::vec3& a, const glm::vec3& b) { return a - b; };
    vec3_type[sol::meta_function::multiplication] =
        [](const glm::vec3& a, float s) { return a * s; };
    vec3_type[sol::meta_function::to_string] =
        [](const glm::vec3& v) {
            return "vec3(" + std::to_string(v.x) + ", "
                           + std::to_string(v.y) + ", "
                           + std::to_string(v.z) + ")";
        };

    // Utility functions
    m_lua.set_function("vec3_distance", [](const glm::vec3& a, const glm::vec3& b) {
        return glm::distance(a, b);
    });

    m_lua.set_function("vec3_normalize", [](const glm::vec3& v) {
        float len = glm::length(v);
        return len > 0.0001f ? v / len : glm::vec3(0.0f);
    });

    m_lua.set_function("vec3_length", [](const glm::vec3& v) {
        return glm::length(v);
    });

    // vec4 (mainly for colours)
    m_lua.new_usertype<glm::vec4>("vec4",
        sol::constructors<glm::vec4(), glm::vec4(float, float, float, float)>(),
        "x", &glm::vec4::x,  "r", &glm::vec4::x,
        "y", &glm::vec4::y,  "g", &glm::vec4::y,
        "z", &glm::vec4::z,  "b", &glm::vec4::z,
        "w", &glm::vec4::w,  "a", &glm::vec4::w
    );
}
```

Now a Lua script can write:

```lua
local pos = vec3(10.0, 0.0, 5.0)
local target = vec3(20.0, 0.0, 15.0)
local dist = vec3_distance(pos, target)
log("Distance to target: " .. tostring(dist))
```

---

## Binding the ECS Registry

This is the core of the scripting system. Scripts need to create entities, add components, query components, and destroy entities. We expose a subset of the EnTT registry API:

```cpp
void ScriptManager::bindRegistry()
{
    // Create a "world" table that wraps the registry
    sol::table world = m_lua.create_named_table("world");

    // Create an entity
    world.set_function("create_entity", [this]() -> entt::entity {
        return m_registry->create();
    });

    // Destroy an entity
    world.set_function("destroy_entity", [this](entt::entity e) {
        if (m_registry->valid(e)) {
            m_registry->destroy(e);
        }
    });

    // Check if an entity is valid
    world.set_function("is_valid", [this](entt::entity e) -> bool {
        return m_registry->valid(e);
    });

    // Get the player entity (convenience — searches for PlayerTag component)
    world.set_function("get_player", [this]() -> sol::object {
        auto view = m_registry->view<PlayerTag>();
        for (auto entity : view) {
            return sol::make_object(m_lua, entity);
        }
        return sol::nil;
    });

    // Find entities by name (searches NameComponent)
    world.set_function("find_by_name",
        [this](const std::string& name) -> sol::object {
            auto view = m_registry->view<NameComponent>();
            for (auto [entity, nc] : view.each()) {
                if (nc.name == name) {
                    return sol::make_object(m_lua, entity);
                }
            }
            return sol::nil;
        }
    );

    // Find all entities with a given tag
    world.set_function("find_all_by_name",
        [this](const std::string& name) -> sol::as_table_t<std::vector<entt::entity>> {
            std::vector<entt::entity> results;
            auto view = m_registry->view<NameComponent>();
            for (auto [entity, nc] : view.each()) {
                if (nc.name == name) {
                    results.push_back(entity);
                }
            }
            return sol::as_table(results);
        }
    );
}
```

### A Note About Entity IDs in Lua

EnTT entity identifiers are 32-bit integers (by default). sol2 handles the conversion automatically. In Lua, they appear as numbers. This is fine for script logic, but be aware that entity IDs are not stable across save/load. If a script needs to reference a specific entity, use `world.find_by_name()` rather than storing a raw ID.

---

## Binding Components

We need to expose the components that scripts will read and write most often. We do not expose every component — only the ones that make sense for scripted gameplay.

```cpp
void ScriptManager::bindComponents()
{
    // --- Transform ---
    m_lua.new_usertype<Transform>("Transform",
        "position", &Transform::position,
        "rotation", &Transform::rotation,
        "scale",    &Transform::scale
    );

    sol::table world = m_lua["world"];

    world.set_function("get_transform",
        [this](entt::entity e) -> sol::object {
            if (m_registry->valid(e) && m_registry->all_of<Transform>(e)) {
                return sol::make_object(m_lua,
                    std::ref(m_registry->get<Transform>(e)));
            }
            return sol::nil;
        }
    );

    world.set_function("set_position",
        [this](entt::entity e, float x, float y, float z) {
            if (m_registry->valid(e) && m_registry->all_of<Transform>(e)) {
                m_registry->get<Transform>(e).position = glm::vec3(x, y, z);
            }
        }
    );

    // --- Health ---
    m_lua.new_usertype<Health>("Health",
        "current",  &Health::current,
        "max",      &Health::max
    );

    world.set_function("get_health",
        [this](entt::entity e) -> sol::object {
            if (m_registry->valid(e) && m_registry->all_of<Health>(e)) {
                return sol::make_object(m_lua,
                    std::ref(m_registry->get<Health>(e)));
            }
            return sol::nil;
        }
    );

    world.set_function("damage_entity",
        [this](entt::entity e, float amount) {
            if (m_registry->valid(e) && m_registry->all_of<Health>(e)) {
                auto& health = m_registry->get<Health>(e);
                health.current = std::max(0.0f, health.current - amount);
            }
        }
    );

    // --- Velocity, Name, Tags ---
    // The same pattern applies for every component you want to expose.
    // Bind the usertype, then add get/set functions to the world table.
    // Velocity exposes linear/angular. NameComponent exposes get_name/set_name.
    // TagList (a std::set<std::string>) exposes add_tag/has_tag.

    m_lua.new_usertype<Velocity>("Velocity",
        "linear",  &Velocity::linear,
        "angular", &Velocity::angular
    );

    world.set_function("get_velocity",
        [this](entt::entity e) -> sol::object {
            if (m_registry->valid(e) && m_registry->all_of<Velocity>(e)) {
                return sol::make_object(m_lua,
                    std::ref(m_registry->get<Velocity>(e)));
            }
            return sol::nil;
        }
    );

    world.set_function("get_name", [this](entt::entity e) -> std::string {
        if (m_registry->valid(e) && m_registry->all_of<NameComponent>(e))
            return m_registry->get<NameComponent>(e).name;
        return "";
    });

    world.set_function("add_tag", [this](entt::entity e, const std::string& tag) {
        if (m_registry->valid(e))
            m_registry->get_or_emplace<TagList>(e).tags.insert(tag);
    });

    world.set_function("has_tag",
        [this](entt::entity e, const std::string& tag) -> bool {
            if (m_registry->valid(e) && m_registry->all_of<TagList>(e))
                return m_registry->get<TagList>(e).tags.count(tag) > 0;
            return false;
        }
    );
}
```

The TagList and NameComponent types are small additions to `engine/ecs/components.h`:

```cpp
#include <set>
struct TagList { std::set<std::string> tags; };
struct NameComponent { std::string name; };
```

---

## Binding Input, Audio, and Console

Scripts need to check key presses, play sounds, and print to the console. Each gets its own Lua table that delegates to the existing C++ managers:

```cpp
void ScriptManager::bindInput()
{
    sol::table input = m_lua.create_named_table("input");
    input.set_function("is_key_down", [this](const std::string& key) -> bool {
        return m_registry->ctx().get<InputManager>().isKeyDown(key);
    });
    input.set_function("is_key_pressed", [this](const std::string& key) -> bool {
        return m_registry->ctx().get<InputManager>().isKeyPressed(key);
    });
    input.set_function("get_mouse_delta", [this]() -> std::tuple<float, float> {
        auto delta = m_registry->ctx().get<InputManager>().getMouseDelta();
        return {delta.x, delta.y};
    });
}

void ScriptManager::bindAudio()
{
    sol::table audio = m_lua.create_named_table("audio");
    audio.set_function("play_sound", [this](const std::string& name) {
        m_registry->ctx().get<AudioManager>().playSound(name);
    });
    audio.set_function("play_sound_at",
        [this](const std::string& name, float x, float y, float z) {
            m_registry->ctx().get<AudioManager>().playSoundAt(name, {x, y, z});
        });
    audio.set_function("play_music", [this](const std::string& name) {
        m_registry->ctx().get<AudioManager>().playMusic(name);
    });
}

void ScriptManager::bindConsole()
{
    sol::table console = m_lua.create_named_table("console");
    console.set_function("print", [this](const std::string& msg) {
        if (m_console) m_console->print(msg);
    });
}
```

---

## The LuaScript Component

Now we connect Lua scripts to individual entities. A LuaScript component attaches a `.lua` file to an entity. That file defines callback functions that the engine calls at the right time.

```cpp
// In engine/ecs/components.h

struct LuaScript {
    std::string scriptPath;           // Path to the .lua file
    sol::environment environment;     // Isolated Lua environment for this entity
    bool initialised = false;         // Has on_create been called?
};
```

Each entity gets its own Lua **environment**. This is critical. Without environments, all scripts would share the same global namespace. If two door scripts both define a variable called `isOpen`, they would clobber each other. An environment is a Lua table that acts as the global scope for a specific script instance. Variables in one environment are invisible to another.

```
SCRIPT ENVIRONMENTS (isolation)
────────────────────────────────────────────────────────────

  Entity 1 (door_A)              Entity 2 (door_B)
  ┌─────────────────┐            ┌─────────────────┐
  │ environment_1    │            │ environment_2    │
  │                  │            │                  │
  │ isOpen = false   │            │ isOpen = true    │
  │ openSpeed = 2.0  │            │ openSpeed = 1.5  │
  │ on_update(dt)    │            │ on_update(dt)    │
  │   ...            │            │   ...            │
  └─────────────────┘            └─────────────────┘
         │                              │
         └──────────┬───────────────────┘
                    │
                    ▼
            Shared read-only access to:
            world.*, input.*, audio.*, vec3, log, etc.
```

---

## The ScriptSystem

The ScriptSystem iterates over every entity with a LuaScript component and calls the appropriate Lua callbacks.

### src/engine/scripting/script_system.h

```cpp
#pragma once

#include <entt/entt.hpp>

class ScriptManager;

class ScriptSystem {
public:
    void init(entt::registry& registry, ScriptManager& scriptMgr);
    void update(float dt);
    void shutdown();

private:
    entt::registry* m_registry = nullptr;
    ScriptManager*  m_scriptMgr = nullptr;

    // Load and initialise a script for an entity
    void initScript(entt::entity entity, LuaScript& script);

    // Called when a LuaScript component is removed or entity destroyed
    void onScriptRemoved(entt::registry& reg, entt::entity entity);
};
```

### src/engine/scripting/script_system.cpp

```cpp
#include "engine/scripting/script_system.h"
#include "engine/scripting/script_manager.h"
#include "engine/ecs/components.h"
#include <iostream>
#include <fstream>

void ScriptSystem::init(entt::registry& registry, ScriptManager& scriptMgr)
{
    m_registry  = &registry;
    m_scriptMgr = &scriptMgr;

    // Listen for script component removal to call on_destroy
    registry.on_destroy<LuaScript>().connect<&ScriptSystem::onScriptRemoved>(*this);
}

void ScriptSystem::initScript(entt::entity entity, LuaScript& script)
{
    auto& lua = m_scriptMgr->getLuaState();

    // Create an isolated environment for this script instance
    // The environment inherits read access to all global bindings (world, input, etc.)
    script.environment = sol::environment(lua, sol::create, lua.globals());

    // Give the script access to its own entity
    script.environment["self"] = entity;

    // Execute the script file within this environment
    try {
        lua.script_file(script.scriptPath, script.environment);
    }
    catch (const sol::error& e) {
        std::cerr << "[Script] Error loading " << script.scriptPath
                  << ": " << e.what() << std::endl;
        return;
    }

    // Call on_create if it exists
    sol::function onCreate = script.environment["on_create"];
    if (onCreate.valid()) {
        auto result = onCreate();
        if (!result.valid()) {
            sol::error err = result;
            std::cerr << "[Script] on_create error in " << script.scriptPath
                      << ": " << err.what() << std::endl;
        }
    }

    script.initialised = true;
}

void ScriptSystem::update(float dt)
{
    // Update the global dt value so all scripts can read it
    m_scriptMgr->getLuaState()["dt"] = dt;

    auto view = m_registry->view<LuaScript>();
    for (auto [entity, script] : view.each()) {

        // Lazy initialisation — load script on first update
        if (!script.initialised) {
            initScript(entity, script);
            if (!script.initialised) continue;  // Script failed to load
        }

        // Update self reference (in case entity was re-used, though unlikely)
        script.environment["self"] = entity;

        // Call on_update(dt)
        sol::function onUpdate = script.environment["on_update"];
        if (onUpdate.valid()) {
            auto result = onUpdate(dt);
            if (!result.valid()) {
                sol::error err = result;
                std::cerr << "[Script] on_update error in " << script.scriptPath
                          << ": " << err.what() << std::endl;
            }
        }
    }
}

void ScriptSystem::shutdown()
{
    // Call on_destroy for all active scripts
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

### Integration into the Game Loop

```cpp
// In Game::init()
auto& scriptMgr = registry.ctx().emplace<ScriptManager>();
scriptMgr.init(registry, console);

auto& scriptSystem = registry.ctx().emplace<ScriptSystem>();
scriptSystem.init(registry, scriptMgr);

// In Game::update(float dt)
// After physics, before rendering
auto& scriptSystem = registry.ctx().get<ScriptSystem>();
scriptSystem.update(dt);

// In Game::shutdown()
auto& scriptSystem = registry.ctx().get<ScriptSystem>();
scriptSystem.shutdown();
```

---

## Example Scripts

Now for the payoff. Let us write real gameplay scripts.

### Example 1: Interactive Door

A door that opens when the player gets close and closes when they walk away. In C++, this would be a dedicated DoorSystem with hardcoded trigger distances and movement speeds. In Lua, it is a self-contained script file.

```lua
-- assets/scripts/entities/door.lua
-- A proximity-triggered door.
-- Attach this to any entity with a Transform component.
-- Set 'open_offset' and 'trigger_distance' in the script or
-- override them via the level editor properties.

-- Configuration (can be overridden per-instance)
open_offset       = vec3(0.0, 3.0, 0.0)   -- How far the door moves when open
trigger_distance  = 5.0                     -- How close the player must be
move_speed        = 3.0                     -- Units per second

-- State
is_open           = false
open_amount       = 0.0                     -- 0 = closed, 1 = fully open
closed_position   = nil                     -- Captured on create

function on_create()
    local transform = world.get_transform(self)
    if transform then
        closed_position = vec3(transform.position.x,
                               transform.position.y,
                               transform.position.z)
    end
    log("Door script initialised")
end

function on_update(dt)
    local player = world.get_player()
    if not player then return end

    local door_tf  = world.get_transform(self)
    local player_tf = world.get_transform(player)
    if not door_tf or not player_tf then return end

    -- Check distance to player
    local dist = vec3_distance(door_tf.position, player_tf.position)
    local should_open = dist < trigger_distance

    -- Smoothly interpolate open_amount
    if should_open and open_amount < 1.0 then
        open_amount = math.min(1.0, open_amount + move_speed * dt)
    elseif not should_open and open_amount > 0.0 then
        open_amount = math.max(0.0, open_amount - move_speed * dt)
    end

    -- Apply position
    if closed_position then
        door_tf.position.x = closed_position.x + open_offset.x * open_amount
        door_tf.position.y = closed_position.y + open_offset.y * open_amount
        door_tf.position.z = closed_position.z + open_offset.z * open_amount
    end

    -- Track state change for sound
    local was_open = is_open
    is_open = open_amount > 0.5

    if is_open and not was_open then
        audio.play_sound_at("door_open",
            door_tf.position.x, door_tf.position.y, door_tf.position.z)
    elseif not is_open and was_open then
        audio.play_sound_at("door_close",
            door_tf.position.x, door_tf.position.y, door_tf.position.z)
    end
end

function on_destroy()
    log("Door script destroyed")
end
```

To attach this script to an entity, add a LuaScript component — either in code or through the level editor:

```cpp
// In code:
auto door = registry.create();
registry.emplace<Transform>(door, glm::vec3(10, 0, 5));
registry.emplace<NameComponent>(door, NameComponent{"door_A"});
registry.emplace<Renderable>(door, meshManager.get("door"), materialManager.get("metal"));
registry.emplace<LuaScript>(door, LuaScript{"assets/scripts/entities/door.lua"});
```

### Example 2: Enemy Patrol Route

An enemy that walks between waypoints. In C++, this was hardcoded in an AI system with baked-in positions. Now a designer can define patrol routes per-entity:

```lua
-- assets/scripts/entities/patrol.lua
-- An entity that patrols between waypoints.

-- Define waypoints (a designer can edit these directly)
waypoints = {
    vec3(10.0, 0.0, 10.0),
    vec3(20.0, 0.0, 10.0),
    vec3(20.0, 0.0, 20.0),
    vec3(10.0, 0.0, 20.0),
}

current_waypoint  = 1
move_speed        = 4.0
arrival_threshold = 1.0
wait_time         = 2.0     -- Seconds to pause at each waypoint
wait_timer        = 0.0
is_waiting        = false

function on_create()
    log("Patrol script started with " .. #waypoints .. " waypoints")
end

function on_update(dt)
    local transform = world.get_transform(self)
    if not transform then return end

    -- Waiting at a waypoint
    if is_waiting then
        wait_timer = wait_timer - dt
        if wait_timer <= 0.0 then
            is_waiting = false
            current_waypoint = current_waypoint + 1
            if current_waypoint > #waypoints then
                current_waypoint = 1
            end
        end
        return
    end

    -- Move toward current waypoint
    local target = waypoints[current_waypoint]
    local to_target = vec3(
        target.x - transform.position.x,
        0.0,  -- Keep on ground plane
        target.z - transform.position.z
    )

    local dist = vec3_length(to_target)

    if dist < arrival_threshold then
        -- Arrived at waypoint
        is_waiting = true
        wait_timer = wait_time
        log("Reached waypoint " .. current_waypoint .. ", waiting " .. wait_time .. "s")
    else
        -- Move toward target
        local dir = vec3_normalize(to_target)
        transform.position.x = transform.position.x + dir.x * move_speed * dt
        transform.position.z = transform.position.z + dir.z * move_speed * dt
    end
end
```

### Example 3: Custom Pickup Effect

A pickup item that gives health, plays a sound, shows a message, and triggers a screen flash — all without touching C++:

```lua
-- assets/scripts/entities/health_pickup.lua
-- A health pickup with visual and audio feedback.

heal_amount       = 25
respawn_time      = 10.0
bob_speed         = 2.0
bob_height        = 0.3
pickup_distance   = 2.0

-- State
base_y            = 0.0
is_active         = true
respawn_timer     = 0.0
time_alive        = 0.0

function on_create()
    local transform = world.get_transform(self)
    if transform then
        base_y = transform.position.y
    end
end

function on_update(dt)
    local transform = world.get_transform(self)
    if not transform then return end

    time_alive = time_alive + dt

    if not is_active then
        -- Waiting to respawn
        respawn_timer = respawn_timer - dt
        if respawn_timer <= 0.0 then
            is_active = true
            transform.scale = vec3(1.0, 1.0, 1.0)
            log("Health pickup respawned")
        end
        return
    end

    -- Bobbing animation
    transform.position.y = base_y + math.sin(time_alive * bob_speed) * bob_height

    -- Check if player is close enough to pick up
    local player = world.get_player()
    if not player then return end

    local player_tf = world.get_transform(player)
    if not player_tf then return end

    local dist = vec3_distance(transform.position, player_tf.position)
    if dist < pickup_distance then
        -- Heal the player
        local health = world.get_health(player)
        if health and health.current < health.max then
            health.current = math.min(health.max, health.current + heal_amount)

            -- Feedback
            audio.play_sound("health_pickup")
            console.print("Picked up +" .. heal_amount .. " health!")

            -- Deactivate (shrink to zero — a simple "disappear" effect)
            is_active = false
            respawn_timer = respawn_time
            transform.scale = vec3(0.0, 0.0, 0.0)

            log("Player picked up health: " .. health.current .. "/" .. health.max)
        end
    end
end
```

---

## Console Integration

The developer console from Chapter 27 already has a command system. We add a `lua` command that passes arbitrary Lua code to the ScriptManager. This turns the console into a live scripting terminal.

```cpp
// During console command registration (in Game::init or similar)

auto& scriptMgr = registry.ctx().get<ScriptManager>();

console.registerCommand("lua", "Execute Lua code. Usage: lua <code>",
    [&scriptMgr](const std::vector<std::string>& args) {
        if (args.empty()) {
            scriptMgr.executeString("print('Lua is ready')");
            return;
        }

        // Join all args back into a single string
        std::string code;
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0) code += " ";
            code += args[i];
        }

        scriptMgr.executeString(code);
    }
);

console.registerCommand("reload_scripts", "Reload all Lua scripts",
    [&registry](const std::vector<std::string>& args) {
        auto view = registry.view<LuaScript>();
        int count = 0;
        for (auto [entity, script] : view.each()) {
            script.initialised = false;  // Force re-init on next update
            count++;
        }
        std::cout << "[Console] Marked " << count
                  << " scripts for reload" << std::endl;
    }
);

console.registerCommand("reload_config", "Reload config.lua",
    [&registry](const std::vector<std::string>& args) {
        auto& config = registry.ctx().get<ConfigManager>();
        config.load("assets/scripts/config.lua");
    }
);
```

Now you can type commands in the console at runtime:

```
> lua log("Hello from console!")
[Script] Hello from console!

> lua world.damage_entity(world.get_player(), 10)
(player takes 10 damage)

> lua local p = world.get_transform(world.get_player()); log(tostring(p.position))
[Script] vec3(12.500000, 1.000000, 8.300000)

> reload_config
[Config] Loaded: assets/scripts/config.lua

> reload_scripts
[Console] Marked 3 scripts for reload
```

This is enormously powerful for debugging. You can inspect and modify any entity at runtime, test gameplay changes without restarting, and prototype new behaviours by typing Lua directly into the console.

---

## C++ Concept Sidebar: Embedding vs Extending

There are two ways to combine a scripting language with a host application. **Embedding** means C++ is the host — it creates the Lua state, decides when to call Lua functions, and controls the game loop. Lua is a tool the host uses. **Extending** means the script is the host — a Lua program runs a main loop and calls into C++ libraries for rendering, physics, and audio. Love2D and Corona SDK use the extending model.

QEngine uses embedding, for three reasons:

1. **Performance.** The game loop ticks thousands of entities per frame. That loop is C++. Only per-entity behaviour logic is Lua.
2. **Safety.** A broken Lua script cannot crash the engine. The C++ host catches errors and continues.
3. **Incremental adoption.** We can script one door and leave everything else in C++.

This is the standard approach. Unreal, Unity, and Godot all use the embedding pattern — the engine is compiled, user code runs in a managed/scripted environment that the engine controls.

---

## Project Structure Summary

Here is the complete set of new and modified files from this chapter:

```
src/
  engine/
    scripting/
      config_manager.h       ← NEW — loads config.lua, typed value access
      config_manager.cpp     ← NEW
      script_manager.h       ← NEW — Lua state, engine API bindings
      script_manager.cpp     ← NEW — bindMathTypes, bindRegistry, etc.
      script_system.h        ← NEW — ticks LuaScript components each frame
      script_system.cpp      ← NEW — lazy init, on_create/on_update/on_destroy
    ecs/
      components.h           ← MODIFIED — add LuaScript, TagList, NameComponent
    debug/
      console.cpp            ← MODIFIED — add lua, reload_scripts, reload_config

assets/
  scripts/
    config.lua               ← NEW — all tweakable engine values
    entities/
      door.lua               ← NEW — proximity-triggered door
      patrol.lua             ← NEW — waypoint patrol behaviour
      health_pickup.lua      ← NEW — pickup with feedback effects
```

---

## Safety, Performance, and Hot-Reload

### Error Handling

Lua scripts will have bugs. The engine must not crash. Every call from C++ into Lua goes through sol2's protected call mechanism — our ScriptSystem already checks `result.valid()` and logs errors. Two additional safety measures worth adopting:

1. **Restrict libraries in release builds.** Use `#ifdef NDEBUG` to only open `base`, `math`, `string`, and `table` in release. Omit `io` and `os` so scripts cannot access the filesystem or execute shell commands.

2. **Instruction count limits.** Use `lua_sethook` with `LUA_MASKCOUNT` to abort scripts after a million instructions, preventing infinite loops from hanging the engine.

### Performance

Lua is fast for a scripting language, but it is not C++. Three rules:

1. **Keep hot loops in C++.** Physics, rendering, collision — these stay compiled. Lua handles high-level logic: state machines, triggers, events.

2. **Minimise boundary crossings.** Call `world.get_transform(self)` once per frame and cache the result in a `local` variable. Do not call it inside a loop.

3. **Use `local`.** In Lua, global access goes through a table lookup. Local access is a register operation. Always declare variables with `local` inside functions.

### Hot-Reload

During development, restarting to test script changes is painful. The simplest approach: check file modification times once per second using `std::filesystem::last_write_time`, compare against a stored timestamp in the LuaScript component, and set `initialised = false` on any changed script. The ScriptSystem re-runs the file and calls `on_create` on the next frame. For now, the `reload_scripts` console command provides manual reload, which is sufficient.

---

## What We Built

Let us take stock. This chapter added three layers of Lua integration:

```
LAYER 1: Configuration
────────────────────────────────────────────────────────────
  config.lua → ConfigManager → PhysicsConfig, CameraConfig, etc.
  Replaces hardcoded numbers. No scripts, just data.

LAYER 2: Entity Scripting
────────────────────────────────────────────────────────────
  door.lua, patrol.lua → LuaScript component → ScriptSystem
  Per-entity behaviour with on_create/on_update/on_destroy.

LAYER 3: Live Console
────────────────────────────────────────────────────────────
  Type "lua ..." in developer console → ScriptManager::executeString
  Inspect and modify the game at runtime.
```

Each layer builds on the one below it. Configuration uses a Lua state to load a file and read values. Entity scripting uses a Lua state with engine bindings and per-entity environments. The console uses the same Lua state for ad-hoc commands.

The key architectural decisions:

- **sol2 over raw Lua C API.** Life is too short for manual stack manipulation. sol2 handles type conversion, error checking, and binding with zero runtime overhead beyond what the C API requires.
- **Environments for isolation.** Each script instance gets its own scope. Global variables in one script cannot interfere with another.
- **Lazy initialisation.** Scripts load and run `on_create` on their first update, not when the component is added. This means scripts can safely reference other entities that might not exist yet during level loading.
- **Fail-safe defaults.** Every config value has a default. Every Lua call is protected. A broken script logs an error and continues. The engine never crashes from bad Lua.
- **Config as the foundation.** Before any gameplay scripting, we established Lua as the configuration layer. This is historically accurate (Lua was a config language first) and practically essential (tweakable values are the most common use case).

---

## What's Next

Chapter 50 covers the **Asset Pipeline** — a build system that processes raw assets (textures, meshes, sounds, scripts, configs) into optimised formats for the engine. Lua scripts and config files will be validated and optionally precompiled to Lua bytecode during the asset build, catching syntax errors before the game ever runs.

The ConfigManager introduced here will be expanded in Chapter 50a's cleanup into a more formal system with schema validation, per-user overrides, and runtime change notifications. The pattern stays the same — Lua files holding data, C++ reading it — but the infrastructure around it matures.

The scripting system itself can grow in many directions: visual script debugging, a script editor in the level editor, type-safe component binding generation, coroutines for multi-frame sequences (Lua coroutines map perfectly to "wait 2 seconds, then open the door" patterns). We have built the foundation. The rest is iteration.
