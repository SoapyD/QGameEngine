# Chapter 23: Save & Load System

## What You'll Learn
- Serialising ECS entities and components to JSON
- Handling resource references (textures, meshes, sounds) during save/load
- Stable entity references that survive save/load cycles
- Save slots with metadata
- Integrating save/load with the game state machine

---

## The Challenge

ECS makes saving **conceptually simple** — the entire game state is just components attached to entities. But the details are tricky:

1. **Components are arbitrary structs** — there's no automatic way to convert them to text
2. **Resource handles** (texture IDs, mesh pointers) are runtime values — saving raw pointer values is meaningless
3. **Entity IDs** change between runs — if entity 5 is a door that entity 12 triggers, we need that relationship to survive save/load
4. **Not everything should be saved** — GPU resources, cached data, and derived state should be reconstructed, not serialised

---

## Strategy: What to Save vs Reconstruct

| Save | Reconstruct on Load |
|------|---------------------|
| Entity existence | OpenGL objects (VAOs, textures) |
| Position, Velocity, Health | Mesh geometry (reload from file) |
| AI state, patrol routes | Sound handles (restart loops) |
| Inventory contents | Spatial hash (rebuild from positions) |
| Door/lift state (open/closed) | Shader programs |
| Player stats, score | Computed lighting |
| Level name, progress | HUD state (derived from components) |

The principle: save **game state**, reconstruct **engine state**.

---

## JSON Library: nlohmann/json

We'll use nlohmann/json — a single-header C++ JSON library. It's the standard choice for C++ JSON.

### Setup

Download `json.hpp` from https://github.com/nlohmann/json and place in `extern/json/`:

```cmake
# In CMakeLists.txt
add_library(json INTERFACE)
target_include_directories(json INTERFACE extern/json)
target_link_libraries(QEngine PRIVATE ... json)
```

### Basic Usage

```cpp
#include <nlohmann/json.hpp>
using json = nlohmann::json;

// Create JSON
json j;
j["name"] = "Player";
j["health"] = 100;
j["position"] = { {"x", 3.0f}, {"y", 1.0f}, {"z", -5.0f} };

// To string
std::string str = j.dump(2);  // 2-space indentation
// {
//   "name": "Player",
//   "health": 100,
//   "position": { "x": 3.0, "y": 1.0, "z": -5.0 }
// }

// Parse from string
json parsed = json::parse(str);
float x = parsed["position"]["x"];
```

---

## Component Serialisers

Each saveable component needs two functions: `to_json` and `from_json`. nlohmann/json uses these automatically via argument-dependent lookup (ADL).

### src/engine/ecs/serialisation.h

```cpp
#pragma once

#include <nlohmann/json.hpp>
#include "engine/ecs/components.h"
#include <glm/glm.hpp>

using json = nlohmann::json;

// ─── GLM helpers ────────────────────────────────────────────────
inline void to_json(json& j, const glm::vec3& v) {
    j = { {"x", v.x}, {"y", v.y}, {"z", v.z} };
}

inline void from_json(const json& j, glm::vec3& v) {
    v.x = j.at("x").get<float>();
    v.y = j.at("y").get<float>();
    v.z = j.at("z").get<float>();
}

// ─── Position ───────────────────────────────────────────────────
inline void to_json(json& j, const Position& p) {
    j = { {"value", p.value} };
}

inline void from_json(const json& j, Position& p) {
    p.value = j.at("value").get<glm::vec3>();
}

// ─── Velocity ───────────────────────────────────────────────────
inline void to_json(json& j, const Velocity& v) {
    j = { {"value", v.value} };
}

inline void from_json(const json& j, Velocity& v) {
    v.value = j.at("value").get<glm::vec3>();
}

// ─── Health ─────────────────────────────────────────────────────
inline void to_json(json& j, const Health& h) {
    j = { {"current", h.current}, {"max", h.max} };
}

inline void from_json(const json& j, Health& h) {
    h.current = j.at("current").get<float>();
    h.max = j.at("max").get<float>();
}

// ─── Rotation ───────────────────────────────────────────────────
inline void to_json(json& j, const Rotation& r) {
    j = { {"euler", r.euler} };
}

inline void from_json(const json& j, Rotation& r) {
    r.euler = j.at("euler").get<glm::vec3>();
}

// ─── CharacterPhysics ───────────────────────────────────────────
inline void to_json(json& j, const CharacterPhysics& cp) {
    j = {
        {"moveSpeed", cp.moveSpeed},
        {"jumpSpeed", cp.jumpSpeed},
        {"groundAccel", cp.groundAccel},
        {"airAccel", cp.airAccel}
    };
}

inline void from_json(const json& j, CharacterPhysics& cp) {
    cp.moveSpeed = j.at("moveSpeed").get<float>();
    cp.jumpSpeed = j.at("jumpSpeed").get<float>();
    cp.groundAccel = j.at("groundAccel").get<float>();
    cp.airAccel = j.at("airAccel").get<float>();
}

// ─── Mover (doors, lifts) ───────────────────────────────────────
inline void to_json(json& j, const Mover& m) {
    j = {
        {"state", static_cast<int>(m.state)},
        {"openPosition", m.openPosition},
        {"closedPosition", m.closedPosition},
        {"speed", m.speed},
        {"waitTime", m.waitTime},
        {"timer", m.timer}
    };
}

inline void from_json(const json& j, Mover& m) {
    m.state = static_cast<MoverState>(j.at("state").get<int>());
    m.openPosition = j.at("openPosition").get<glm::vec3>();
    m.closedPosition = j.at("closedPosition").get<glm::vec3>();
    m.speed = j.at("speed").get<float>();
    m.waitTime = j.at("waitTime").get<float>();
    m.timer = j.at("timer").get<float>();
}

// ─── Tags (no data, just presence) ─────────────────────────────
// Tags are saved as empty objects — their existence is what matters
// TagPlayer, OnGround, etc. are handled specially in the save system
```

### C++ Concept: ADL (Argument-Dependent Lookup)

When nlohmann/json calls `to_json(j, myComponent)`, C++ automatically finds the right `to_json` function by looking at the namespace of `myComponent`'s type. This is called **ADL** — the compiler searches the namespaces of the argument types for matching functions.

This means you don't need to register serialisers anywhere — just define `to_json`/`from_json` in the same namespace as your component, and it works.

---

## The Save System

### src/engine/save/save_system.h

```cpp
#pragma once

#include <entt/entt.hpp>
#include <string>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

struct SaveMetadata {
    std::string saveName;
    std::string mapName;
    std::string timestamp;
    float playTime;        // Seconds played
    int saveSlot;
};

class SaveSystem {
public:
    // Save the entire game state to a file
    static bool saveGame(const entt::registry& registry,
                          const std::string& mapName,
                          float playTime,
                          int slot);

    // Load a game from a save file
    static bool loadGame(entt::registry& registry,
                          std::string& outMapName,
                          float& outPlayTime,
                          int slot);

    // Get metadata for a save slot without loading it
    static bool getSaveInfo(int slot, SaveMetadata& outMeta);

    // Delete a save slot
    static bool deleteSave(int slot);

    // Get the file path for a save slot
    static std::string getSavePath(int slot);

private:
    // Serialise a single entity to JSON
    static json serialiseEntity(const entt::registry& registry,
                                 entt::entity entity);

    // Deserialise a single entity from JSON
    static entt::entity deserialiseEntity(entt::registry& registry,
                                           const json& entityJson);
};
```

### src/engine/save/save_system.cpp

```cpp
#include "engine/save/save_system.h"
#include "engine/ecs/serialisation.h"
#include "engine/ecs/components.h"
#include <fstream>
#include <iostream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <filesystem>

std::string SaveSystem::getSavePath(int slot) {
    return "saves/save_" + std::to_string(slot) + ".json";
}

bool SaveSystem::saveGame(const entt::registry& registry,
                            const std::string& mapName,
                            float playTime,
                            int slot) {

    json saveFile;

    // ─── Metadata ───────────────────────────────────────────────
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::stringstream timeStr;
    timeStr << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");

    saveFile["metadata"] = {
        {"version", 1},
        {"mapName", mapName},
        {"playTime", playTime},
        {"timestamp", timeStr.str()},
        {"slot", slot}
    };

    // ─── Entities ───────────────────────────────────────────────
    json entities = json::array();

    // Save all entities that have a Position (our proxy for "is a game entity")
    // Skip purely visual or derived entities
    auto view = registry.view<Position>();
    for (auto entity : view) {
        entities.push_back(serialiseEntity(registry, entity));
    }

    saveFile["entities"] = entities;

    // ─── Write to file ──────────────────────────────────────────
    std::filesystem::create_directories("saves");

    std::ofstream file(getSavePath(slot));
    if (!file.is_open()) {
        std::cerr << "ERROR: Could not create save file" << std::endl;
        return false;
    }

    file << saveFile.dump(2);  // Pretty-printed with 2-space indent
    file.close();

    std::cout << "Game saved to slot " << slot << std::endl;
    return true;
}

json SaveSystem::serialiseEntity(const entt::registry& registry,
                                   entt::entity entity) {
    json j;

    // ─── Core components ────────────────────────────────────────
    // Each component is saved under its type name as a key

    if (registry.all_of<Position>(entity)) {
        j["Position"] = registry.get<Position>(entity);
    }
    if (registry.all_of<Velocity>(entity)) {
        j["Velocity"] = registry.get<Velocity>(entity);
    }
    if (registry.all_of<Rotation>(entity)) {
        j["Rotation"] = registry.get<Rotation>(entity);
    }
    if (registry.all_of<Health>(entity)) {
        j["Health"] = registry.get<Health>(entity);
    }
    if (registry.all_of<CharacterPhysics>(entity)) {
        j["CharacterPhysics"] = registry.get<CharacterPhysics>(entity);
    }

    // ─── Tags (no data) ────────────────────────────────────────
    if (registry.all_of<TagPlayer>(entity)) {
        j["TagPlayer"] = json::object();
    }
    if (registry.all_of<OnGround>(entity)) {
        j["OnGround"] = { {"value", registry.get<OnGround>(entity).value} };
    }
    if (registry.all_of<Gravity>(entity)) {
        j["Gravity"] = json::object();
    }

    // ─── Gameplay components ────────────────────────────────────
    if (registry.all_of<Mover>(entity)) {
        j["Mover"] = registry.get<Mover>(entity);
    }

    // ─── Visual reference (save by name, not GPU handle) ────────
    if (registry.all_of<MeshRenderer>(entity)) {
        auto& mr = registry.get<MeshRenderer>(entity);
        j["MeshRenderer"] = {
            {"meshName", mr.meshName},
            {"textureName", mr.textureName}
        };
    }

    // ─── AI state ───────────────────────────────────────────────
    if (registry.all_of<AIBrain>(entity)) {
        auto& ai = registry.get<AIBrain>(entity);
        j["AIBrain"] = {
            {"state", static_cast<int>(ai.state)},
            {"detectRange", ai.detectRange},
            {"attackRange", ai.attackRange},
            {"enemyType", ai.enemyType}
        };
    }

    return j;
}

bool SaveSystem::loadGame(entt::registry& registry,
                            std::string& outMapName,
                            float& outPlayTime,
                            int slot) {

    std::ifstream file(getSavePath(slot));
    if (!file.is_open()) {
        std::cerr << "ERROR: Save file not found: " << getSavePath(slot) << std::endl;
        return false;
    }

    json saveFile;
    try {
        saveFile = json::parse(file);
    } catch (const json::parse_error& e) {
        std::cerr << "ERROR: Corrupt save file: " << e.what() << std::endl;
        return false;
    }
    file.close();

    // ─── Read metadata ──────────────────────────────────────────
    outMapName = saveFile["metadata"]["mapName"];
    outPlayTime = saveFile["metadata"]["playTime"];

    // ─── Clear existing entities ────────────────────────────────
    registry.clear();

    // ─── Recreate entities ──────────────────────────────────────
    for (const auto& entityJson : saveFile["entities"]) {
        deserialiseEntity(registry, entityJson);
    }

    std::cout << "Game loaded from slot " << slot << std::endl;
    return true;
}

entt::entity SaveSystem::deserialiseEntity(entt::registry& registry,
                                             const json& j) {
    auto entity = registry.create();

    // ─── Core components ────────────────────────────────────────
    if (j.contains("Position")) {
        registry.emplace<Position>(entity, j["Position"].get<Position>());
    }
    if (j.contains("Velocity")) {
        registry.emplace<Velocity>(entity, j["Velocity"].get<Velocity>());
    }
    if (j.contains("Rotation")) {
        registry.emplace<Rotation>(entity, j["Rotation"].get<Rotation>());
    }
    if (j.contains("Health")) {
        registry.emplace<Health>(entity, j["Health"].get<Health>());
    }
    if (j.contains("CharacterPhysics")) {
        registry.emplace<CharacterPhysics>(entity,
            j["CharacterPhysics"].get<CharacterPhysics>());
    }

    // ─── Tags ───────────────────────────────────────────────────
    if (j.contains("TagPlayer")) {
        registry.emplace<TagPlayer>(entity);
    }
    if (j.contains("OnGround")) {
        registry.emplace<OnGround>(entity, j["OnGround"]["value"].get<bool>());
    }
    if (j.contains("Gravity")) {
        registry.emplace<Gravity>(entity);
    }

    // ─── Gameplay ───────────────────────────────────────────────
    if (j.contains("Mover")) {
        registry.emplace<Mover>(entity, j["Mover"].get<Mover>());
    }

    // ─── Visual (reconstruct from name) ─────────────────────────
    if (j.contains("MeshRenderer")) {
        MeshRenderer mr;
        mr.meshName = j["MeshRenderer"]["meshName"];
        mr.textureName = j["MeshRenderer"]["textureName"];
        // Resolve names to actual GPU handles via the resource cache:
        // mr.meshId = MeshCache::get(mr.meshName);
        // mr.textureId = TextureCache::get(mr.textureName);
        registry.emplace<MeshRenderer>(entity, mr);
    }

    // ─── AI ─────────────────────────────────────────────────────
    if (j.contains("AIBrain")) {
        AIBrain ai;
        ai.state = static_cast<AIState>(j["AIBrain"]["state"].get<int>());
        ai.detectRange = j["AIBrain"]["detectRange"];
        ai.attackRange = j["AIBrain"]["attackRange"];
        ai.enemyType = j["AIBrain"]["enemyType"];
        registry.emplace<AIBrain>(entity, ai);
    }

    return entity;
}

bool SaveSystem::getSaveInfo(int slot, SaveMetadata& outMeta) {
    std::ifstream file(getSavePath(slot));
    if (!file.is_open()) return false;

    try {
        json saveFile = json::parse(file);
        outMeta.saveName = "Save " + std::to_string(slot);
        outMeta.mapName = saveFile["metadata"]["mapName"];
        outMeta.playTime = saveFile["metadata"]["playTime"];
        outMeta.timestamp = saveFile["metadata"]["timestamp"];
        outMeta.saveSlot = slot;
        return true;
    } catch (...) {
        return false;
    }
}

bool SaveSystem::deleteSave(int slot) {
    return std::filesystem::remove(getSavePath(slot));
}
```

---

## Save File Example

```json
{
  "metadata": {
    "version": 1,
    "mapName": "e1m1",
    "playTime": 342.5,
    "timestamp": "2026-02-17 14:32:01",
    "slot": 0
  },
  "entities": [
    {
      "Position": { "value": { "x": 3.0, "y": 1.0, "z": -5.0 } },
      "Velocity": { "value": { "x": 0.0, "y": 0.0, "z": 0.0 } },
      "Rotation": { "euler": { "x": 0.0, "y": 90.0, "z": 0.0 } },
      "Health": { "current": 75, "max": 100 },
      "CharacterPhysics": { "moveSpeed": 7.0, "jumpSpeed": 8.0, "groundAccel": 10.0, "airAccel": 1.0 },
      "TagPlayer": {},
      "OnGround": { "value": true },
      "Gravity": {},
      "MeshRenderer": { "meshName": "player", "textureName": "player_skin" }
    },
    {
      "Position": { "value": { "x": 10.0, "y": 0.0, "z": -8.0 } },
      "Health": { "current": 50, "max": 50 },
      "AIBrain": { "state": 1, "detectRange": 15.0, "attackRange": 2.0, "enemyType": "grunt" },
      "MeshRenderer": { "meshName": "grunt", "textureName": "grunt_skin" }
    },
    {
      "Position": { "value": { "x": 5.0, "y": 0.0, "z": -3.0 } },
      "Mover": {
        "state": 0,
        "openPosition": { "x": 5.0, "y": 3.0, "z": -3.0 },
        "closedPosition": { "x": 5.0, "y": 0.0, "z": -3.0 },
        "speed": 2.0,
        "waitTime": 3.0,
        "timer": 0.0
      },
      "MeshRenderer": { "meshName": "door_metal", "textureName": "door_tex" }
    }
  ]
}
```

Human-readable. You can edit save files in a text editor for debugging.

---

## Integrating with Game States

### Saving (in PlayingState)

```cpp
void PlayingState::update(float dt) {
    // Quick save with F5
    static bool f5Pressed = false;
    bool f5Now = m_input.isKeyPressed(GLFW_KEY_F5);
    if (f5Now && !f5Pressed) {
        SaveSystem::saveGame(m_registry, m_currentMap, m_playTime, 0);
        // Show "Game Saved" message on HUD (fade out over 2 seconds)
    }
    f5Pressed = f5Now;

    // Quick load with F9
    static bool f9Pressed = false;
    bool f9Now = m_input.isKeyPressed(GLFW_KEY_F9);
    if (f9Now && !f9Pressed) {
        std::string mapName;
        float playTime;
        if (SaveSystem::loadGame(m_registry, mapName, playTime, 0)) {
            m_currentMap = mapName;
            m_playTime = playTime;
            // Rebuild spatial hash, restart audio loops, etc.
            reconstructRuntimeState(m_registry);
        }
    }
    f9Pressed = f9Now;
}
```

### Post-Load Reconstruction

After loading, some runtime state needs rebuilding:

```cpp
void reconstructRuntimeState(entt::registry& registry) {
    // Rebuild spatial hash from positions
    auto view = registry.view<Position, AABBCollider>();
    for (auto [entity, pos, col] : view.each()) {
        spatialHash.insert(entity, pos.value, col.halfExtents);
    }

    // Restart looping audio
    auto audioView = registry.view<Position, AudioSource>();
    for (auto [entity, pos, src] : audioView.each()) {
        if (src.looping) {
            src.loopHandle = audio.playLoop(src.soundName, src.volume);
            audio.setLoopPosition(src.loopHandle, pos.value);
            src.playing = true;
        }
    }
}
```

---

## C++ Concept: `std::filesystem`

```cpp
#include <filesystem>
std::filesystem::create_directories("saves");
std::filesystem::remove(path);
```

C++17 added `<filesystem>` for portable file/directory operations. No more platform-specific `mkdir` calls. Key functions:

- `create_directories("a/b/c")` — creates all missing directories in the path
- `exists(path)` — check if a file/directory exists
- `remove(path)` — delete a file
- `file_size(path)` — get file size in bytes

---

## Adding New Components to the Save System

When you add a new component to QEngine, making it saveable requires three steps:

1. Write `to_json` and `from_json` in `serialisation.h`
2. Add a `registry.all_of<NewComponent>` block in `serialiseEntity()`
3. Add a `j.contains("NewComponent")` block in `deserialiseEntity()`

If the component contains resource handles (texture IDs, mesh pointers), save the **name/path** and re-resolve it on load.

---

## What's Next

In **Chapter 24**, we'll add a skybox — a cubemap rendered behind everything that gives the world a sky. It's a quick visual upgrade with minimal code.
