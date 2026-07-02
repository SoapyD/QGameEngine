# Chapter 15b: Jolt Kinematic Movers & Sensor Bodies

## What You'll Learn
- Converting movers (lifts, doors) to kinematic Jolt bodies
- The difference between `MoveKinematic` and `SetPosition`
- Static bodies for non-level entities (the shelf)
- Trigger volumes as Jolt sensor bodies
- Tuning physics demos for Jolt's friction model
- The player riding lifts without any custom rider code

---

## Step 3: Kinematic Movers

Lifts and doors should be kinematic Jolt bodies. When a kinematic body moves, it pushes dynamic bodies and virtual characters out of the way — solving the "lift doesn't carry the player" problem.

### Update `scene_setup.cpp`

For entities with a `Mover` component, create a kinematic body:

```cpp
void createKinematicBody(entt::registry& registry, entt::entity entity)
{
    auto& jolt = registry.ctx().get<JoltWorld>();
    auto& bodyInterface = jolt.getBodyInterface();
    auto& pos = registry.get<Position>(entity);
    auto& col = registry.get<AABBCollider>(entity);

    JPH::BoxShapeSettings shapeSettings(
        JPH::Vec3(col.halfExtents.x, col.halfExtents.y, col.halfExtents.z)
    );
    shapeSettings.SetEmbedded();

    auto shapeResult = shapeSettings.Create();

    JPH::BodyCreationSettings bodySettings(
        shapeResult.Get(),
        JPH::RVec3(pos.value.x, pos.value.y, pos.value.z),
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Kinematic,
        Layers::MOVING
    );

    JPH::BodyID bodyId = bodyInterface.CreateAndAddBody(
        bodySettings, JPH::EActivation::Activate
    );

    registry.emplace<JoltBody>(entity, bodyId);
}
```

### Syncing Movers to Jolt

After the mover system updates `pos.value` via `glm::mix`, we need to push that new position into the Jolt kinematic body.

Add a new system that runs right after `moverSystem`:

### New file: `src/engine/ecs/systems/mover_sync_system.h`

```cpp
#pragma once

#include <entt/entt.hpp>

void moverSyncSystem(entt::registry& registry);
```

### New file: `src/engine/ecs/systems/mover_sync_system.cpp`

```cpp
#include "engine/ecs/systems/mover_sync_system.h"
#include "engine/ecs/components.h"
#include "engine/physics/jolt_world.h"
#include "engine/physics/physics_config.h"

void moverSyncSystem(entt::registry& registry)
{
    const auto& config = registry.ctx().get<PhysicsConfig>();
    auto& jolt = registry.ctx().get<JoltWorld>();
    auto& bodyInterface = jolt.getBodyInterface();

    auto view = registry.view<Position, Mover, JoltBody>();
    for (auto [entity, pos, mover, joltBody] : view.each())
    {
        // Move the kinematic body to match the mover's current position
        bodyInterface.MoveKinematic(
            joltBody.id,
            JPH::RVec3(pos.value.x, pos.value.y, pos.value.z),
            JPH::Quat::sIdentity(),
            config.fixedDeltaTime  // Jolt uses this to compute velocity
        );
    }
}
```

> **`MoveKinematic` vs `SetPosition`** — `MoveKinematic` tells Jolt where the body should be at the end of the next physics step. Jolt calculates the velocity needed to reach that position, which means it pushes anything in the way. `SetPosition` would teleport the body without pushing anything.

---

## Step 4: Static Bodies for Non-Level Geometry

The shelf (our raised platform) has an `AABBCollider` but no Jolt body. Dynamic cubes will fall straight through it because Jolt doesn't know it exists. Any non-level-geometry entity that should block physics objects needs a static Jolt body.

### Update `scene_setup.cpp`

```cpp
void createStaticBody(entt::registry& registry, entt::entity entity)
{
    auto& jolt = registry.ctx().get<JoltWorld>();
    auto& bodyInterface = jolt.getBodyInterface();
    auto& pos = registry.get<Position>(entity);
    auto& col = registry.get<AABBCollider>(entity);

    JPH::BoxShapeSettings shapeSettings(
        JPH::Vec3(col.halfExtents.x, col.halfExtents.y, col.halfExtents.z)
    );
    shapeSettings.SetEmbedded();

    auto shapeResult = shapeSettings.Create();

    JPH::BodyCreationSettings bodySettings(
        shapeResult.Get(),
        JPH::RVec3(pos.value.x, pos.value.y, pos.value.z),
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Static,
        Layers::NON_MOVING
    );

    JPH::BodyID bodyId = bodyInterface.CreateAndAddBody(
        bodySettings, JPH::EActivation::DontActivate
    );

    registry.emplace<JoltBody>(entity, bodyId);
}
```

Then add the call after the shelf entity is created in `setupScene`:

```cpp
createStaticBody(registry, shelf);
```

> **Level geometry vs static bodies** — `createLevelBodies` creates static bodies from the level's surface data (floors, walls, ceilings). `createStaticBody` is for individual entities like the shelf that aren't part of the level geometry but still need to block physics objects.

---

## Step 5: Tune Physics Demos for Jolt

Jolt's contact friction is different from our old custom physics. Cube 1 had a gentle velocity of `(-0.5, 0, 0)` which was enough in the old system, but Jolt's friction stops it almost immediately. Update Cube 1 to start higher (so it visibly falls onto the shelf) with enough velocity to slide off the edge:

```cpp
glm::vec3 startPos(20.5f, 4.0f, 5.0f);
glm::vec3 startVel(-6.0f, 0.0f, 0.0f);
```

---

## Step 6: Trigger Volumes as Sensors

Our trigger volumes (lava, teleporter, lift activator) should be Jolt sensor bodies — they detect overlap but don't block movement.

### Update `scene_setup.cpp`

For entities with `AABBCollider.isTrigger == true`:

```cpp
void createSensorBody(entt::registry& registry, entt::entity entity)
{
    auto& jolt = registry.ctx().get<JoltWorld>();
    auto& bodyInterface = jolt.getBodyInterface();
    auto& pos = registry.get<Position>(entity);
    auto& col = registry.get<AABBCollider>(entity);

    JPH::BoxShapeSettings shapeSettings(
        JPH::Vec3(col.halfExtents.x, col.halfExtents.y, col.halfExtents.z)
    );
    shapeSettings.SetEmbedded();

    auto shapeResult = shapeSettings.Create();

    JPH::BodyCreationSettings bodySettings(
        shapeResult.Get(),
        JPH::RVec3(pos.value.x, pos.value.y, pos.value.z),
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Static,
        Layers::SENSOR
    );
    bodySettings.mIsSensor = true;

    JPH::BodyID bodyId = bodyInterface.CreateAndAddBody(
        bodySettings, JPH::EActivation::DontActivate
    );

    registry.emplace<JoltBody>(entity, bodyId);
}
```

> **Note:** The `triggerSystem` still uses its own AABB overlap check against ECS positions. You can keep this working as-is since the positions are still in the ECS. Later, you could replace it with Jolt's contact listener for sensor overlap events, but that's an optimisation, not a requirement.

### Update `scene_setup.h`

Add declarations for the new functions so `main.cpp` can call them in Chapter 15c:

```cpp
void createStaticBody(entt::registry& registry, entt::entity entity);
void createKinematicBody(entt::registry& registry, entt::entity entity);
void createSensorBody(entt::registry& registry, entt::entity entity);
```

---

## What Changed — Summary

| File | Change |
|------|--------|
| `scene_setup.h` | Added `createStaticBody()`, `createKinematicBody()`, `createSensorBody()` declarations |
| `scene_setup.cpp` | Added `createStaticBody()`, `createKinematicBody()`, `createSensorBody()`, shelf Jolt body, tuned Cube 1 for Jolt friction |
| `mover_sync_system.h/cpp` | **New** — Pushes mover positions into Jolt kinematic bodies |

---

## New C++ Concept: Kinematic Bodies

A kinematic body is controlled by code, not physics. It has infinite mass — nothing can push it. But it pushes everything else.

```cpp
// Create a kinematic body
bodySettings.mMotionType = EMotionType::Kinematic;

// Each frame, tell it where to be
bodyInterface.MoveKinematic(bodyId, targetPosition, targetRotation, deltaTime);
```

The `MoveKinematic` call is critical. It tells Jolt: "this body needs to be at `targetPosition` after `deltaTime` seconds." Jolt calculates the velocity needed, and during the physics step, the body moves at that velocity — pushing anything in its path.

This is exactly what our movers (lifts, doors) need. The `moverSystem` calculates the position via `glm::mix`, then `moverSyncSystem` tells Jolt's kinematic body to move there. During the physics step, the kinematic body sweeps to its target position and pushes the player's CharacterVirtual.

---

## What's Next

The movers and triggers are now Jolt-powered. In **Chapter 15c**, we'll wire everything together in `main.cpp`, update `CMakeLists.txt`, and review the new architecture compared to the old custom physics.
