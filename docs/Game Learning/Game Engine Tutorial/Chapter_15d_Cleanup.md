# Chapter 15d: Post-Jolt Cleanup — Archiving Dead Code & Splitting Scene Setup

## What You'll Learn
- Identifying and archiving systems replaced by Jolt Physics
- Splitting a bloated scene setup file into focused helpers
- Adding the `startDelay` feature to movers
- Tuning `stepHeight` for reliable stair stepping

---

## Step 1: Archive Dead Systems

With Jolt Physics handling collision, gravity, friction, ground detection, and movement for physics bodies, four old systems are no longer compiled or called. They still exist as source files. Move them to an archive folder so the `systems/` directory only contains active code.

### Create the archive directory

```
src/engine/ecs/systems/archived/
```

### Move these files into it

| File | What It Did | Replaced By |
|------|-------------|-------------|
| `collision_system.h/.cpp` | AABB sweep collision detection and response | Jolt's rigid body solver + `CharacterVirtual::ExtendedUpdate` |
| `physics_system.h/.cpp` | Gravity, friction, ground detection via raycasting | Jolt gravity + `CharacterVirtual` ground state |
| `movement_system.h/.cpp` | Applied `Velocity` to `Position` each tick | Jolt body simulation + `joltSyncSystem` (projectile movement patched into `combatSystem` in Ch 14b) |
| `player_movement_system.h/.cpp` | Quake-style acceleration from input → velocity | `playerCharacterSystem` (same acceleration, but drives `CharacterVirtual`) |

These files are already absent from `CMakeLists.txt` — they won't affect the build. Archiving them keeps the active systems directory clean while preserving the code for reference.

> **Why archive instead of delete?** These systems are educational artifacts. They show how custom physics works before a physics engine takes over. Future readers of the tutorial can compare the hand-rolled approach with the Jolt approach.

---

## Step 2: Split Scene Setup

`scene_setup.cpp` is ~630 lines and does three very different jobs:

1. **Jolt body creation** — `createLevelBodies`, `createDynamicBody`, `createKinematicBody`, `createStaticBody`, `createSensorBody`
2. **Level geometry** — `createShowcaseLevel` (hardcoded room definition)
3. **Entity spawning** — `setupScene` (player, lights, physics demos, doors, lifts, triggers, lava, teleporter)

Split it into three files, each with a clear responsibility.

### New file: `src/engine/ecs/jolt_body_helpers.h`

```cpp
#pragma once

#include <entt/entt.hpp>
#include "engine/level/level.h"

void createLevelBodies(entt::registry& registry, const Level& level);
void createDynamicBody(entt::registry& registry, entt::entity entity);
void createKinematicBody(entt::registry& registry, entt::entity entity);
void createStaticBody(entt::registry& registry, entt::entity entity);
void createSensorBody(entt::registry& registry, entt::entity entity);
```

### New file: `src/engine/ecs/jolt_body_helpers.cpp`

Move the five `create*Body` functions here. The includes are:

```cpp
#include "engine/ecs/jolt_body_helpers.h"
#include "engine/ecs/components.h"
#include "engine/physics/jolt_world.h"
```

Then move `createLevelBodies`, `createDynamicBody`, `createKinematicBody`, `createStaticBody`, and `createSensorBody` from `scene_setup.cpp` into this file. No changes to the function bodies.

### New file: `src/engine/ecs/showcase_level.h`

```cpp
#pragma once

#include "engine/level/level.h"

Level createShowcaseLevel();
```

### New file: `src/engine/ecs/showcase_level.cpp`

Move the `createShowcaseLevel` function here. Remove the `static` keyword (it's no longer file-local). The includes are:

```cpp
#include "engine/ecs/showcase_level.h"
#include "engine/level/level_loader.h"
```

### Update `scene_setup.cpp`

After extracting, `scene_setup.cpp` only contains `setupScene`. Update its includes:

```cpp
#include "engine/ecs/scene_setup.h"
#include "engine/ecs/jolt_body_helpers.h"
#include "engine/ecs/showcase_level.h"
#include "engine/ecs/components.h"
#include "engine/ecs/weapon_definitions.h"
#include "engine/level/level.h"
#include "engine/level/level_loader.h"
```

### Update `scene_setup.h`

Remove the Jolt body declarations (they now live in `jolt_body_helpers.h`):

```cpp
#pragma once

#include <entt/entt.hpp>
#include "engine/core/resource_manager.h"
#include "engine/level/level.h"

Level setupScene(entt::registry& registry, const ResourceManager& resources);
```

### Update `main.cpp` includes

Replace the single `scene_setup.h` include with both:

```cpp
#include "engine/ecs/scene_setup.h"
#include "engine/ecs/jolt_body_helpers.h"
```

### Update `CMakeLists.txt`

Add the two new source files:

```cmake
src/engine/ecs/jolt_body_helpers.cpp
src/engine/ecs/showcase_level.cpp
```

---

## Step 3: Add Start Delay to Movers

The lift starts moving the instant the player steps on the trigger. A short delay gives the player time to position themselves.

### Update `MoverState` enum in `components.h`

Add a new state between `Idle` and `Moving`:

```cpp
enum class MoverState
{
    Idle,        // at start position
    StartDelay,  // triggered, waiting before moving
    Moving,      // moving to end position
    Waiting,     // at end position, waiting before returning
    Returning    // moving back to start position
};
```

### Add `startDelay` field to `Mover` in `components.h`

```cpp
struct Mover {
    glm::vec3 startPos;
    glm::vec3 endPos;
    float speed = 2.0f;
    float waitTime = 3.0f;
    float startDelay = 0.0f;    // delay before movement begins
    float timer = 0.0f;
    float progress = 0.0f;
    MoverState state = MoverState::Idle;
    bool requiresTrigger = true;
};
```

### Handle `StartDelay` in `mover_system.cpp`

Add a new case in the switch, between `Idle` and `Moving`:

```cpp
case MoverState::StartDelay:
{
    // triggered, counting down before moving
    mover.timer -= dt;
    if (mover.timer <= 0.0f)
    {
        mover.state = MoverState::Moving;
    }
    break;
}
```

### Update `trigger_system.cpp`

When a trigger activates a mover, check for `startDelay`:

```cpp
auto& mover = registry.get<Mover>(trigger.target);
if (mover.state == MoverState::Idle)
{
    if (mover.startDelay > 0.0f)
    {
        mover.state = MoverState::StartDelay;
        mover.timer = mover.startDelay;
    }
    else
    {
        mover.state = MoverState::Moving;
    }
}
```

The existing `timer` field is reused for the countdown — it gets reset to `waitTime` later when the mover reaches its destination, so there's no conflict.

### Set delay on the lift in `scene_setup.cpp`

The lift's Mover now has a `startDelay` argument (third float after `speed` and `waitTime`):

```cpp
registry.emplace<Mover>(lift, bottomPos, topPos, 2.0f, 2.0f, 2.0f, 0.0f, 0.0f,
                          MoverState::Idle, true);
//                                         ^^^
//                              startDelay = 2.0f (2 seconds before lift moves)
```

The door keeps `startDelay = 0.0f` (no delay):

```cpp
registry.emplace<Mover>(door, closedPos, openPos, 3.0f, 4.0f, 0.0f, 0.0f, 0.0f,
                          MoverState::Idle, true);
```

---

## Step 4: Tune Step Height

Jolt's `CharacterVirtual::ExtendedUpdate` uses `mWalkStairsStepUp` to determine how high the character can step onto obstacles. The default `0.5f` can be unreliable when the step geometry is close to the floor body's fattened surface. Increase it to `1.5f` for robust stepping:

### Update `CharacterPhysics` in `components.h`

```cpp
float stepHeight = 1.5f; // Max height of a step the player can walk up
```

This value feeds into `ExtendedUpdate` via `playerCharacterSystem`:

```cpp
updateSettings.mStickToFloorStepDown = JPH::Vec3(0.0f, -physics.stepHeight, 0.0f);
updateSettings.mWalkStairsStepUp = JPH::Vec3(0.0f, physics.stepHeight, 0.0f);
```

> **Why so high?** The player won't actually step up 1.5 units onto thin air — `ExtendedUpdate` only steps up when there's a solid surface to land on. A generous value ensures the character never gets blocked by geometry that *should* be walkable, like the lift edge or small curbs.

---

## Step 5: Minor Cleanups

### Add missing `createStaticBody` declaration

If `scene_setup.h` still declares the Jolt body helpers (before splitting), ensure `createStaticBody` is declared:

```cpp
void createStaticBody(entt::registry& registry, entt::entity entity);
```

### Remove debug output from `main.cpp`

Remove these temporary debug lines added during Chapter 15 development:

```cpp
// REMOVE:
std::cout << "Level bodies created for " << level.sectors.size() << " sectors" << std::endl;
std::cout << "Total Jolt bodies: " << joltWorld.physicsSystem->GetNumBodies() << std::endl;
// ...
std::cout << "Total Jolt bodies after all init: " << joltWorld.physicsSystem->GetNumBodies() << std::endl;
```

---

## What Changed — Summary

| File | Change |
|------|--------|
| `systems/archived/` | **New directory** — moved 4 dead system files |
| `jolt_body_helpers.h/.cpp` | **New** — extracted 5 Jolt body creation functions |
| `showcase_level.h/.cpp` | **New** — extracted level geometry builder |
| `scene_setup.h` | Simplified — only declares `setupScene` |
| `scene_setup.cpp` | Reduced to ~370 lines — only entity spawning |
| `components.h` | Added `StartDelay` state, `startDelay` field, updated `stepHeight` |
| `mover_system.cpp` | Added `StartDelay` case |
| `trigger_system.cpp` | Checks `startDelay` before activating movers |
| `main.cpp` | Updated includes, removed debug output |
| `CMakeLists.txt` | Added 2 new source files |

### Files archived

| File | Was |
|------|-----|
| `collision_system.h/.cpp` | AABB sweep collision (Ch 9) |
| `physics_system.h/.cpp` | Gravity + friction + ground detection (Ch 10) |
| `movement_system.h/.cpp` | Velocity → Position integration (Ch 10) |
| `player_movement_system.h/.cpp` | Quake-style acceleration (Ch 10) |

---

## What You Should See

After building and running:

1. **Lift has a 2-second delay** — step on the trigger, wait, then it rises
2. **Stepping onto the lift is smooth** — no jittering at the edge
3. **All existing features still work** — doors, cubes, lava, teleporter, weapons
4. **The `systems/` directory is clean** — only active systems remain

---

## What's Next

The physics are solid, the code is clean, and the architecture is maintainable. In **Chapter 16**, we'll start building gameplay content — crosshair, expanded HUD, death/respawn, and damage feedback.
