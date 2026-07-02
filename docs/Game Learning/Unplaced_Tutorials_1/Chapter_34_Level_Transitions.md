# Chapter 34: Level Transitions

## What You'll Learn
- Why a game needs level transitions and what's involved
- Reusing the trigger system (Ch 11) to detect level exits
- What to preserve (player state) vs what to destroy (everything else)
- Capturing and restoring player persistent state across maps
- A LevelManager that orchestrates the full transition flow
- Proper teardown: clearing the registry, spatial hash, and level geometry
- Spawn points: named positions in each level file
- The `map` console command for quick testing (Ch 27)
- Loading screen integration with the state machine (Ch 22)
- Level sequencing and victory conditions

---

## Why Level Transitions

Right now QEngine loads a single map and stays there forever. A real game has multiple levels — `e1m1`, `e1m2`, `e1m3` — and the player moves between them. When the player walks into an exit trigger, the engine needs to:

1. Snapshot the player's current state (health, ammo, weapons, score)
2. Tear down the entire current world
3. Load the next level from disk
4. Recreate the player at a named spawn point with their preserved stats

This is one of the most error-prone operations in a game engine. Get it wrong and you leak memory, lose player data, or crash. Get it right and the player never thinks about it — they just walk through a door and appear in a new world.

---

## The Exit Trigger

We already have trigger volumes from Chapter 11. The `TriggerAction::ChangeLevel` case was left as a stub. Now we fill it in.

First, a dedicated component that marks a trigger as a level exit. This is pure data — no behaviour:

```cpp
// In src/engine/ecs/components.h

struct LevelExitTrigger {
    std::string targetLevel;      // e.g. "e1m2"
    std::string spawnPointName;   // Where to place the player in the new level
};
```

A level exit entity in the map file has `Position`, `AABBCollider` (with `isTrigger = true`), `TriggerVolume` (with `action = TriggerAction::ChangeLevel`), and `LevelExitTrigger`. The trigger system detects the overlap; the LevelExitTrigger tells us where to go.

### Detection in the Trigger System

In the existing `triggerSystem()` from Chapter 11, we add a case for `ChangeLevel`:

```cpp
// In src/engine/ecs/systems/trigger_system.cpp — inside the switch statement

case TriggerAction::ChangeLevel: {
    // Only the player triggers level transitions
    if (!registry.all_of<TagPlayer>(entity)) break;

    // Read the exit data
    if (registry.all_of<LevelExitTrigger>(trigEntity)) {
        const auto& exit = registry.get<LevelExitTrigger>(trigEntity);

        // Find the LevelManager (stored as a context variable on the registry)
        auto* levelMgr = registry.ctx().find<LevelManager*>();
        if (levelMgr && *levelMgr) {
            (*levelMgr)->requestTransition(exit.targetLevel, exit.spawnPointName);
        }
    }

    trigger.triggered = true;
    break;
}
```

The `LevelManager` pointer is stored as an EnTT context variable. This lets the trigger system request a transition without holding a reference to the manager directly.

---

## What to Preserve vs What to Destroy

Not everything survives a level transition. Here is the breakdown:

```
┌─────────────────────────────────────────────────────────────┐
│                    LEVEL TRANSITION                          │
├────────────────────────┬────────────────────────────────────┤
│      PRESERVE          │           DESTROY                  │
│                        │                                    │
│  Player health         │  All entities (enemies, items,     │
│  Player armour         │    doors, lifts, triggers)         │
│  Weapon inventory      │  Level geometry (sectors,          │
│  Ammo counts           │    surfaces, BSP tree)             │
│  Current weapon        │  Spatial hash contents             │
│  Score                 │  Navigation graph                  │
│  Secrets found         │  Level-specific audio              │
│  Kill count            │  Particle emitters                 │
│                        │  Decals                            │
├────────────────────────┴────────────────────────────────────┤
│      RECREATE IN NEW LEVEL                                  │
│                                                             │
│  Player entity with preserved stats at new spawn point      │
│  Level geometry from new map file                           │
│  Enemies, items, triggers from new map file                 │
│  Spatial hash rebuilt from new geometry                     │
│  Navigation graph for new level                             │
│  Lighting for new level                                     │
└─────────────────────────────────────────────────────────────┘
```

The key insight: we **destroy everything** including the player entity, but we snapshot the important data first. Then we create a brand-new player entity in the new level and paste the snapshot back on.

---

## The Transition Flow

Here is the full sequence from trigger to gameplay:

```
Player touches exit trigger
        │
        ▼
requestTransition("e1m2", "from_e1m1")
        │
        ▼
┌───────────────────────┐
│ Capture player state  │  health, armour, ammo, weapons, score
└───────┬───────────────┘
        │
        ▼
┌───────────────────────┐
│ Push LoadingState     │  Show "Loading e1m2..." on screen
└───────┬───────────────┘
        │
        ▼
┌───────────────────────┐
│ registry.clear()      │  Destroy ALL entities and components
└───────┬───────────────┘
        │
        ▼
┌───────────────────────┐
│ Clear level systems   │  Unload geometry, spatial hash, nav graph
└───────┬───────────────┘
        │
        ▼
┌───────────────────────┐
│ Load new level file   │  Geometry, entities, triggers, lights
└───────┬───────────────┘
        │
        ▼
┌───────────────────────┐
│ Spawn player at       │  Create player entity, restore snapshot
│ named spawn point     │
└───────┬───────────────┘
        │
        ▼
┌───────────────────────┐
│ Pop LoadingState      │  Resume PlayingState with new level
└───────────────────────┘
```

---

## PlayerPersistentState

A plain struct that holds everything the player carries between levels. This is **not** a component — it is a temporary snapshot used only during transitions:

```cpp
// In src/engine/game/player_persistent_state.h
#pragma once

#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>

// Forward-declared enums defined in your weapon/ammo headers
enum class WeaponType;
enum class AmmoType;

struct PlayerPersistentState {
    int health = 100;
    int maxHealth = 100;
    int armour = 0;

    std::vector<WeaponType> weapons;
    std::unordered_map<AmmoType, int> ammo;
    int currentWeapon = 0;

    int score = 0;
    int secrets = 0;
    int kills = 0;
};
```

### Capture and Restore

Two free functions extract the snapshot from a living player entity and apply it to a new one:

```cpp
// In src/engine/game/player_persistent_state.cpp
#include "engine/game/player_persistent_state.h"
#include "engine/ecs/components.h"

PlayerPersistentState capturePlayerState(entt::registry& registry, entt::entity player) {
    PlayerPersistentState state;

    // Health
    if (registry.all_of<Health>(player)) {
        const auto& hp = registry.get<Health>(player);
        state.health = static_cast<int>(hp.current);
        state.maxHealth = static_cast<int>(hp.max);
    }

    // Armour
    if (registry.all_of<Armour>(player)) {
        state.armour = static_cast<int>(registry.get<Armour>(player).current);
    }

    // Weapons and ammo
    if (registry.all_of<WeaponInventory>(player)) {
        const auto& inv = registry.get<WeaponInventory>(player);
        state.weapons = inv.owned;
        state.currentWeapon = inv.currentIndex;
    }

    if (registry.all_of<AmmoInventory>(player)) {
        state.ammo = registry.get<AmmoInventory>(player).counts;
    }

    // Score and stats
    if (registry.all_of<PlayerStats>(player)) {
        const auto& stats = registry.get<PlayerStats>(player);
        state.score = stats.score;
        state.secrets = stats.secrets;
        state.kills = stats.kills;
    }

    return state;
}

void restorePlayerState(entt::registry& registry, entt::entity player,
                        const PlayerPersistentState& state) {
    // Health
    if (registry.all_of<Health>(player)) {
        auto& hp = registry.get<Health>(player);
        hp.current = static_cast<float>(state.health);
        hp.max = static_cast<float>(state.maxHealth);
    }

    // Armour
    if (registry.all_of<Armour>(player)) {
        registry.get<Armour>(player).current = static_cast<float>(state.armour);
    }

    // Weapons and ammo
    if (registry.all_of<WeaponInventory>(player)) {
        auto& inv = registry.get<WeaponInventory>(player);
        inv.owned = state.weapons;
        inv.currentIndex = state.currentWeapon;
    }

    if (registry.all_of<AmmoInventory>(player)) {
        registry.get<AmmoInventory>(player).counts = state.ammo;
    }

    // Score and stats
    if (registry.all_of<PlayerStats>(player)) {
        auto& stats = registry.get<PlayerStats>(player);
        stats.score = state.score;
        stats.secrets = state.secrets;
        stats.kills = state.kills;
    }
}
```

These functions use `all_of` checks so they degrade gracefully if a component is missing. The capture function reads from the old player before the registry is cleared; the restore function writes to the new player after the level is loaded.

---

## Spawn Points

Each level defines named positions where the player can appear. These are loaded from the level file alongside geometry and entities:

```cpp
// In src/engine/game/spawn_point.h
#pragma once

#include <string>
#include <glm/glm.hpp>

struct SpawnPoint {
    std::string name;       // "start", "from_e1m1", "secret_entrance"
    glm::vec3 position;
    float yaw;              // Direction the player faces (degrees)
};
```

In the level file (JSON format from Chapter 8), spawn points live in their own array:

```json
{
    "spawn_points": [
        {
            "name": "start",
            "position": [0.0, 1.0, 0.0],
            "yaw": 90.0
        },
        {
            "name": "from_e1m1",
            "position": [24.0, 1.0, -8.0],
            "yaw": 180.0
        },
        {
            "name": "secret",
            "position": [50.0, 1.0, 12.0],
            "yaw": 0.0
        }
    ]
}
```

A lookup function finds a spawn point by name:

```cpp
// In src/engine/game/spawn_point.cpp

#include "engine/game/spawn_point.h"
#include <vector>
#include <optional>

std::optional<SpawnPoint> findSpawnPoint(const std::vector<SpawnPoint>& points,
                                         const std::string& name) {
    for (const auto& sp : points) {
        if (sp.name == name) {
            return sp;
        }
    }
    return std::nullopt;
}
```

If the named spawn point is not found, the LevelManager falls back to the first spawn point in the list (usually named `"start"`).

---

## The LevelManager

This class orchestrates the entire transition. It is not an ECS system — like the GameStateManager, it sits above ECS and coordinates the process:

### src/engine/game/level_manager.h

```cpp
#pragma once

#include <string>
#include <vector>
#include <entt/entt.hpp>
#include "engine/game/player_persistent_state.h"
#include "engine/game/spawn_point.h"

class GameStateManager;
class SpatialHash;
class AudioManager;

class LevelManager {
public:
    LevelManager(SpatialHash& spatialHash, AudioManager& audio);

    // Request a transition (deferred — happens next update)
    void requestTransition(const std::string& levelName,
                           const std::string& spawnPoint);

    // Called every frame from PlayingState::update()
    void update(entt::registry& registry, GameStateManager& stateManager);

    const std::string& getCurrentLevel() const { return m_currentLevel; }
    bool isTransitioning() const { return m_transitioning; }

    // Called once at game start to load the first level
    void loadInitialLevel(entt::registry& registry, const std::string& levelName);

private:
    std::string m_currentLevel;
    std::string m_pendingLevel;
    std::string m_pendingSpawnPoint;
    bool m_transitioning = false;

    SpatialHash& m_spatialHash;
    AudioManager& m_audio;

    // Loaded spawn points for the current level
    std::vector<SpawnPoint> m_spawnPoints;

    void executeTransition(entt::registry& registry, GameStateManager& stateManager);
    void clearLevel(entt::registry& registry);
    void loadLevel(entt::registry& registry, const std::string& levelName);
    void spawnPlayer(entt::registry& registry, const std::string& spawnPointName,
                     const PlayerPersistentState& state);
};
```

### src/engine/game/level_manager.cpp

```cpp
#include "engine/game/level_manager.h"
#include "engine/core/game_state_manager.h"
#include "engine/physics/spatial_hash.h"
#include "engine/audio/audio_manager.h"
#include "engine/ecs/components.h"
#include "game/states/loading_state.h"
#include <iostream>

LevelManager::LevelManager(SpatialHash& spatialHash, AudioManager& audio)
    : m_spatialHash(spatialHash), m_audio(audio) {}

void LevelManager::requestTransition(const std::string& levelName,
                                      const std::string& spawnPoint) {
    if (m_transitioning) return;  // Already transitioning — ignore

    m_pendingLevel = levelName;
    m_pendingSpawnPoint = spawnPoint;
    m_transitioning = true;

    std::cout << "[LevelManager] Transition requested: " << levelName
              << " (spawn: " << spawnPoint << ")" << std::endl;
}

void LevelManager::update(entt::registry& registry, GameStateManager& stateManager) {
    if (!m_transitioning) return;

    executeTransition(registry, stateManager);
    m_transitioning = false;
}

void LevelManager::executeTransition(entt::registry& registry,
                                       GameStateManager& stateManager) {
    // ─── Step 1: Capture player state ────────────────────────────
    PlayerPersistentState playerState;
    auto playerView = registry.view<TagPlayer>();
    for (auto [entity, tag] : playerView.each()) {
        playerState = capturePlayerState(registry, entity);
        break;  // Only one player
    }

    std::cout << "[LevelManager] Player state captured (HP: " << playerState.health
              << ", Score: " << playerState.score << ")" << std::endl;

    // ─── Step 2: Show loading screen ─────────────────────────────
    stateManager.pushState(std::make_unique<LoadingState>(m_pendingLevel));

    // ─── Step 3: Clear the current level ─────────────────────────
    clearLevel(registry);

    // ─── Step 4: Load the new level ──────────────────────────────
    loadLevel(registry, m_pendingLevel);

    // ─── Step 5: Spawn player at the named spawn point ───────────
    spawnPlayer(registry, m_pendingSpawnPoint, playerState);

    // ─── Step 6: Store ourselves as registry context ─────────────
    LevelManager* self = this;
    registry.ctx().emplace<LevelManager*>(self);

    // ─── Step 7: Transition complete ─────────────────────────────
    m_currentLevel = m_pendingLevel;
    m_pendingLevel.clear();
    m_pendingSpawnPoint.clear();

    std::cout << "[LevelManager] Now playing: " << m_currentLevel << std::endl;

    // ─── Step 8: Pop loading screen ──────────────────────────────
    stateManager.popState();
}

void LevelManager::loadInitialLevel(entt::registry& registry,
                                      const std::string& levelName) {
    loadLevel(registry, levelName);
    m_currentLevel = levelName;

    // Store the LevelManager pointer in the registry context so
    // trigger_system can find it without a direct reference
    LevelManager* self = this;
    registry.ctx().emplace<LevelManager*>(self);
}
```

---

## clearLevel() — Proper Teardown

The most critical part of a level transition. Everything from the old level must be cleaned up:

```cpp
// In src/engine/game/level_manager.cpp

void LevelManager::clearLevel(entt::registry& registry) {
    // ─── 1. Stop level audio ─────────────────────────────────────
    // Stop all one-shot and looping sounds tied to entities.
    // Music is handled separately — let it fade out.
    m_audio.stopAllSounds();
    // m_audio.fadeOutMusic(1.0f);  // Optional: crossfade to new level music

    // ─── 2. Destroy all entities ─────────────────────────────────
    // registry.clear() destroys every entity and every component.
    // After this call, the registry is completely empty.
    registry.clear();

    // ─── 3. Clear the spatial hash ───────────────────────────────
    m_spatialHash.clear();

    // ─── 4. Clear spawn points ───────────────────────────────────
    m_spawnPoints.clear();

    // ─── 5. Unload level geometry ────────────────────────────────
    // If your level geometry is stored outside the registry (e.g. a
    // LevelGeometry struct with sector/surface data from Ch 8), clear it:
    //
    // m_levelGeometry.clear();
    // m_bspTree.reset();
    // m_navGraph.clear();

    std::cout << "[LevelManager] Level cleared" << std::endl;
}
```

### Why registry.clear()?

EnTT's `registry.clear()` is the nuclear option — it destroys every entity and removes all component pools. This is exactly what we want. Any component with a destructor (e.g. one that holds a `std::vector` or `std::string`) is properly cleaned up. After `clear()`, the registry is in the same state as a freshly constructed one.

This is safer than trying to selectively destroy entities. With selective destruction, it is easy to miss something — a forgotten entity with a dangling reference, a particle emitter that outlives its level. Clearing everything and rebuilding is simpler and more reliable.

---

## loadLevel() — Building the New World

Loading a new level reuses all the existing infrastructure from Chapter 8. The LevelManager just calls the same loading code that `loadInitialLevel` uses:

```cpp
// In src/engine/game/level_manager.cpp

void LevelManager::loadLevel(entt::registry& registry, const std::string& levelName) {
    std::string path = "assets/levels/" + levelName + ".json";

    std::cout << "[LevelManager] Loading level: " << path << std::endl;

    // ─── 1. Load and parse the level file ────────────────────────
    // This is the same level loading code from Chapter 8.
    // It creates entities for all geometry, enemies, items, etc.
    //
    // LevelLoader::load(path, registry, m_spatialHash);
    //
    // In practice, your LevelLoader handles:
    //   - Level geometry (sectors, surfaces, BSP data)
    //   - Enemy entities (position, AI brain, health, mesh)
    //   - Item entities (position, pickup component, mesh)
    //   - Trigger entities (position, collider, trigger volume)
    //   - Door/lift entities (position, mover component)
    //   - Light entities (position, light component)
    //   - Spawn points

    // ─── 2. Parse spawn points from the level data ───────────────
    // The level file includes a "spawn_points" array.
    // Parse it into our m_spawnPoints vector:

    // Example using nlohmann/json (Ch 23):
    // std::ifstream file(path);
    // json levelData = json::parse(file);
    //
    // m_spawnPoints.clear();
    // if (levelData.contains("spawn_points")) {
    //     for (const auto& sp : levelData["spawn_points"]) {
    //         SpawnPoint point;
    //         point.name = sp["name"];
    //         point.position = glm::vec3(
    //             sp["position"][0], sp["position"][1], sp["position"][2]);
    //         point.yaw = sp["yaw"];
    //         m_spawnPoints.push_back(point);
    //     }
    // }

    // ─── 3. Rebuild spatial hash ─────────────────────────────────
    // Insert all collidable entities into the spatial hash.
    auto colView = registry.view<Position, AABBCollider>();
    for (auto [entity, pos, col] : colView.each()) {
        m_spatialHash.insert(entity, pos.value, col.halfExtents);
    }

    // ─── 4. Start level music ────────────────────────────────────
    // m_audio.playMusic("assets/music/" + levelName + ".ogg");

    std::cout << "[LevelManager] Level loaded: " << levelName << std::endl;
}
```

---

## spawnPlayer() — Recreating the Player

After the new level is loaded, we create a fresh player entity and apply the preserved state:

```cpp
// In src/engine/game/level_manager.cpp

void LevelManager::spawnPlayer(entt::registry& registry,
                                 const std::string& spawnPointName,
                                 const PlayerPersistentState& state) {
    // ─── Find the spawn point ────────────────────────────────────
    auto sp = findSpawnPoint(m_spawnPoints, spawnPointName);

    if (!sp.has_value()) {
        std::cerr << "[LevelManager] Spawn point '" << spawnPointName
                  << "' not found, using first available" << std::endl;

        if (!m_spawnPoints.empty()) {
            sp = m_spawnPoints[0];
        } else {
            // Last resort: origin
            sp = SpawnPoint{ "default", glm::vec3(0.0f, 1.0f, 0.0f), 0.0f };
            std::cerr << "[LevelManager] No spawn points in level, using origin"
                      << std::endl;
        }
    }

    // ─── Create the player entity ────────────────────────────────
    // This mirrors the player creation code from your initial level setup
    auto player = registry.create();

    registry.emplace<TagPlayer>(player);
    registry.emplace<Position>(player, sp->position);
    registry.emplace<Velocity>(player, glm::vec3(0.0f));
    registry.emplace<Rotation>(player, 0.0f, sp->yaw);  // pitch, yaw
    registry.emplace<Gravity>(player);
    registry.emplace<AABBCollider>(player, glm::vec3(0.4f, 0.9f, 0.4f));
    registry.emplace<Health>(player, 100.0f, 100.0f);
    registry.emplace<Armour>(player, 0.0f);
    registry.emplace<WeaponInventory>(player);
    registry.emplace<AmmoInventory>(player);
    registry.emplace<PlayerStats>(player);

    // ─── Restore preserved state ─────────────────────────────────
    restorePlayerState(registry, player, state);

    // ─── Insert into spatial hash ────────────────────────────────
    m_spatialHash.insert(player, sp->position, glm::vec3(0.4f, 0.9f, 0.4f));

    std::cout << "[LevelManager] Player spawned at '"
              << sp->name << "' (" << sp->position.x << ", "
              << sp->position.y << ", " << sp->position.z << ")"
              << std::endl;
}
```

---

## Loading Screen Integration

The `LoadingState` from Chapter 22 already handles displaying a loading screen. We pass it the level name so it can show "Loading e1m2...":

```cpp
// In src/game/states/loading_state.h
#pragma once

#include "engine/core/game_state.h"
#include <string>

class LoadingState : public GameState {
public:
    explicit LoadingState(const std::string& levelName)
        : m_levelName(levelName) {}

    void enter() override {
        // Nothing to set up — just display the screen
    }

    void update(float dt) override {
        // The loading state does not drive the loading itself.
        // LevelManager::executeTransition() pushes this state,
        // does the work synchronously, then pops it.
        //
        // For a more polished experience, you could break loading
        // into steps and advance one step per frame:
        //
        // switch (m_step) {
        //     case 0: clearLevel();    m_step++; break;
        //     case 1: loadGeometry();  m_step++; break;
        //     case 2: loadEntities();  m_step++; break;
        //     case 3: spawnPlayer();   m_step++; break;
        //     case 4: m_stateManager->popState(); break;
        // }
    }

    void render() override {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // Draw "Loading <level>..." using TextRenderer (Ch 15a / Ch 30)
        // float centerX = screenWidth / 2.0f;
        // float centerY = screenHeight / 2.0f;
        // std::string text = "Loading " + m_levelName + "...";
        // font.renderText(text, centerX, centerY, 1.0f, glm::vec4(1.0f));
    }

    bool isTransparent() const override { return false; }
    std::string getName() const override { return "Loading"; }

private:
    std::string m_levelName;
    // int m_step = 0;  // For multi-frame loading
};
```

In a simple implementation, the loading state is pushed and popped within the same frame — the screen flashes black briefly. For a smoother experience, break the loading work into steps and advance one per frame so the loading screen actually renders between steps.

---

## The `map` Console Command

Testing level transitions without walking to the exit every time is essential. The `map` command from Chapter 27 triggers a transition directly:

```cpp
// In src/game/debug_commands.cpp — inside registerDebugCommands()

console.registerCommand("map", "Load a level: map e1m2",
    [&registry, &console](const std::vector<std::string>& args) {
        if (args.empty()) {
            // Print the current level
            auto* levelMgr = registry.ctx().find<LevelManager*>();
            if (levelMgr && *levelMgr) {
                console.print("Current level: " + (*levelMgr)->getCurrentLevel());
            }
            console.print("Usage: map <levelname>");
            return;
        }

        auto* levelMgr = registry.ctx().find<LevelManager*>();
        if (levelMgr && *levelMgr) {
            (*levelMgr)->requestTransition(args[0], "start");
            console.print("Loading " + args[0] + "...");
            console.toggle();  // Close console for the transition
        } else {
            console.print("Error: LevelManager not available");
        }
    });
```

Type `map e1m3` in the console and you jump straight to that level with a default spawn point of `"start"`. The player's current state is preserved, just like a normal transition.

---

## Level Sequencing

A game needs to know which level comes next. A simple lookup table handles this:

```cpp
// In src/engine/game/level_sequence.h
#pragma once

#include <string>
#include <vector>
#include <optional>

// Level progression for the game
const std::vector<std::string> levelOrder = {
    "e1m1", "e1m2", "e1m3", "e1m4", "e1m5"
};

// Given a level name, return the next level in sequence.
// Returns std::nullopt if this is the last level.
inline std::optional<std::string> getNextLevel(const std::string& currentLevel) {
    for (size_t i = 0; i < levelOrder.size(); i++) {
        if (levelOrder[i] == currentLevel) {
            if (i + 1 < levelOrder.size()) {
                return levelOrder[i + 1];
            }
            return std::nullopt;  // Last level — no next
        }
    }
    return std::nullopt;  // Level not in sequence (custom/debug map)
}
```

### Generic Exit Triggers

Some exit triggers specify an explicit target level (`targetLevel = "e1m3"`). Others are generic "end of level" exits that should advance to the next map in sequence. Handle this in the trigger system:

```cpp
// In the ChangeLevel trigger case:

case TriggerAction::ChangeLevel: {
    if (!registry.all_of<TagPlayer>(entity)) break;

    if (registry.all_of<LevelExitTrigger>(trigEntity)) {
        const auto& exit = registry.get<LevelExitTrigger>(trigEntity);
        auto* levelMgr = registry.ctx().find<LevelManager*>();
        if (!levelMgr || !*levelMgr) break;

        std::string target = exit.targetLevel;

        // If targetLevel is empty, advance to the next level in sequence
        if (target.empty()) {
            auto next = getNextLevel((*levelMgr)->getCurrentLevel());
            if (next.has_value()) {
                target = next.value();
            } else {
                // Last level — push victory state instead
                // stateManager.pushState(std::make_unique<VictoryState>(...));
                break;
            }
        }

        std::string spawn = exit.spawnPointName.empty() ? "start" : exit.spawnPointName;
        (*levelMgr)->requestTransition(target, spawn);
    }

    trigger.triggered = true;
    break;
}
```

### Victory State

After the final level, instead of loading another map, push a `VictoryState` that shows the player's totals — kills, secrets, completion time. This follows the same GameState pattern from Chapter 21. The player can then return to the main menu.

---

## Wiring It Into PlayingState

The LevelManager needs to be updated every frame from the PlayingState:

```cpp
// In src/game/states/playing_state.h — add a member:
LevelManager& m_levelManager;

// In src/game/states/playing_state.cpp — update():
void PlayingState::update(float dt) {
    if (m_console.isOpen()) return;

    // Check for level transitions before running game systems
    m_levelManager.update(m_registry, *m_stateManager);
    if (m_levelManager.isTransitioning()) return;  // Skip this frame

    // Normal game update
    inputSystem(m_registry, m_window, m_camera, dt);
    aiSystem(m_registry, dt);
    physicsSystem(m_registry, dt);
    combatSystem(m_registry, dt);
    triggerSystem(m_registry, dt);
    pickupSystem(m_registry);
    audioSystem(m_registry, m_audio, m_camera, dt);
    // ... other systems
}
```

And in `main.cpp`, create the LevelManager and pass it through:

```cpp
// In main.cpp
SpatialHash spatialHash(4.0f);  // Cell size from Ch 9
AudioManager audio;
LevelManager levelManager(spatialHash, audio);

// Load the first level
levelManager.loadInitialLevel(registry, "e1m1");

// Create the playing state with the level manager
stateManager.pushState(std::make_unique<PlayingState>(
    registry, window, audio, camera, levelManager));
```

---

## C++ Concept: `std::unordered_map` with Enum Keys

The `PlayerPersistentState` uses `std::unordered_map<AmmoType, int>` to store ammo counts. This works because `AmmoType` is an `enum class`, and `std::unordered_map` needs a hash function for its key type.

### C++20 and Later

In C++20, scoped enums work directly as unordered_map keys. The standard library provides hash specialisations for all integral types, and the compiler implicitly uses the underlying type:

```cpp
enum class AmmoType { Shells, Nails, Rockets, Cells };

// This just works in C++20:
std::unordered_map<AmmoType, int> ammo;
ammo[AmmoType::Shells] = 25;
ammo[AmmoType::Rockets] = 5;
```

### Pre-C++20

Before C++20, `std::hash` is not specialised for `enum class` types. You have two options:

**Option A: Custom hasher struct**

```cpp
struct EnumHash {
    template <typename T>
    std::size_t operator()(T t) const {
        return std::hash<std::underlying_type_t<T>>{}(
            static_cast<std::underlying_type_t<T>>(t));
    }
};

// Use it as the third template parameter:
std::unordered_map<AmmoType, int, EnumHash> ammo;
```

**Option B: Specialise `std::hash` for your enum**

```cpp
template <>
struct std::hash<AmmoType> {
    std::size_t operator()(AmmoType t) const noexcept {
        return std::hash<int>{}(static_cast<int>(t));
    }
};

// Now std::unordered_map<AmmoType, int> works without extra arguments:
std::unordered_map<AmmoType, int> ammo;
```

Option B is cleaner at the usage site but requires a specialisation per enum. Option A is a single generic hasher that works for all enums. QEngine targets C++20, so neither workaround is needed — but knowing both approaches is useful when working with older codebases.

---

## What's Next

There is no next chapter. This is it.

Over 34 chapters — 20 core chapters building the engine from scratch, plus 14 nice-to-haves adding the features that turn a tech demo into a real game — you have built **QEngine**: a complete 3D first-person shooter engine using C++, OpenGL, and an ECS architecture powered by EnTT.

Here is what you built:

- **Rendering**: Shaders, textures, meshes, lighting, skybox, shadows, post-processing, font rendering, decals, particles
- **Physics**: AABB collision detection, spatial hashing, gravity, movement, trigger volumes
- **Gameplay**: Weapons, projectiles, items, pickups, doors, lifts, boss fights, level transitions
- **AI**: State machines, pathfinding, sight checks, multi-phase boss behaviour
- **Audio**: Spatial sound, music, environmental effects
- **Networking**: Client-server architecture, state synchronisation, client-side prediction
- **Infrastructure**: Game state machine, menus, save/load, developer console, debug rendering, frustum culling
- **Animation**: Keyframe interpolation, view models, skeletal animation

Every component is pure data. Every system is a stateless function. The architecture scales because adding a new feature means adding a new component and a new system — nothing else changes. You can adapt QEngine to a horror game, a platformer, an arena shooter, or anything else by swapping out the gameplay components and systems while keeping the engine core intact.

### Other Roadmaps

Three additional roadmaps extend QEngine in specific directions:

- **TrenchBroom Integration** — connect QEngine to the TrenchBroom level editor so designers can build maps visually using brushes, compile them, and load them directly into the engine
- **Top-Down Shooter Adaptation** — restructure the camera, input, and rendering for a top-down perspective while reusing the same ECS components and systems
- **Multiplayer Infrastructure** — expand the networking foundation from Chapters 17-19 into a production-ready multiplayer system with dedicated servers, matchmaking, and anti-cheat

### Build Something

The purpose of a game engine is to make games. You now have every tool you need. Pick a theme, sketch a few levels, design some enemies, and ship it. The hardest part — understanding how all the pieces fit together — is behind you. The fun part is next.
