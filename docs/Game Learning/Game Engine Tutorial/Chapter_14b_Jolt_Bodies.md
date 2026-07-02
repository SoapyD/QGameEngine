# Chapter 14b: Jolt Physics — Bodies, Sync & Wiring

## What You'll Learn
- Adding a `JoltBody` component to link ECS entities to Jolt bodies
- Converting level surfaces into static rigid bodies
- Converting entity colliders into dynamic rigid bodies
- Syncing Jolt body transforms back into ECS `Position` components
- Wiring Jolt into `main.cpp` and removing old custom physics systems
- Fixing projectile movement and adding Jolt impulses to the combat system
- Updating `CMakeLists.txt` to swap source files

---

## Step 4: Add a JoltBody Component

We need to link ECS entities to their Jolt physics bodies. A simple component stores the Jolt `BodyID`.

### Add to `components.h`

```cpp
// Links an ECS entity to a Jolt Physics body
struct JoltBody
{
    JPH::BodyID id;
};
```

You'll need to add this include at the top of `components.h`:

```cpp
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
```

---

## Step 5: Create Static Bodies from Level Surfaces

Each surface in the level becomes a static box body in Jolt. This replaces the per-frame AABB construction that `collisionSystem` was doing.

### Update `scene_setup.cpp`

Add the include:

```cpp
#include "engine/physics/jolt_world.h"
```

First, declare both new functions in `scene_setup.h` so `main.cpp` can call them:

```cpp
void createLevelBodies(entt::registry& registry, const Level& level);
void createDynamicBody(entt::registry& registry, entt::entity entity);
```

Then in `scene_setup.cpp`, after `setupScene` creates the level geometry, add a function to create the static bodies:

```cpp
void createLevelBodies(entt::registry& registry, const Level& level)
{
    auto& jolt = registry.ctx().get<JoltWorld>();
    auto& bodyInterface = jolt.getBodyInterface();

    for (const auto& sector : level.sectors)
    {
        for (const auto& surface : sector.surfaces)
        {
            // Compute AABB from the surface vertices
            glm::vec3 surfMin = glm::min(
                glm::min(surface.vertices[0], surface.vertices[1]),
                glm::min(surface.vertices[2], surface.vertices[3])
            );
            glm::vec3 surfMax = glm::max(
                glm::max(surface.vertices[0], surface.vertices[1]),
                glm::max(surface.vertices[2], surface.vertices[3])
            );

            // Fatten thin dimensions (same as old collision system)
            for (int i = 0; i < 3; i++)
            {
                if (surfMax[i] - surfMin[i] < 0.01f)
                {
                    surfMin[i] -= 0.1f;
                    surfMax[i] += 0.1f;
                }
            }

            // Jolt box shape takes half-extents
            glm::vec3 halfExtents = (surfMax - surfMin) * 0.5f;
            glm::vec3 centre = (surfMin + surfMax) * 0.5f;

            JPH::BoxShapeSettings shapeSettings(
                JPH::Vec3(halfExtents.x, halfExtents.y, halfExtents.z)
            );
            shapeSettings.SetEmbedded();

            auto shapeResult = shapeSettings.Create();
            if (!shapeResult.IsValid()) continue;

            JPH::BodyCreationSettings bodySettings(
                shapeResult.Get(),
                JPH::RVec3(centre.x, centre.y, centre.z),
                JPH::Quat::sIdentity(),
                JPH::EMotionType::Static,
                Layers::NON_MOVING
            );

            bodyInterface.CreateAndAddBody(bodySettings, JPH::EActivation::DontActivate);
        }
    }
}
```

Static bodies don't need an ECS entity -- they're part of the world geometry and never move or get destroyed during gameplay.

---

## Step 6: Create Dynamic Bodies for Physics Entities

Entities that have `Position`, `Velocity`, and `AABBCollider` (our physics-driven cubes) need dynamic Jolt bodies.

### Update entity creation in `scene_setup.cpp`

For each physics entity (e.g., the test cubes), create a Jolt body and attach a `JoltBody` component:

```cpp
void createDynamicBody(entt::registry& registry, entt::entity entity)
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
        JPH::EMotionType::Dynamic,
        Layers::MOVING
    );

    // Match our gravity strength
    bodySettings.mGravityFactor = 1.0f;

    // Set initial velocity if the entity has one
    if (registry.all_of<Velocity>(entity))
    {
        auto& vel = registry.get<Velocity>(entity);
        bodySettings.mLinearVelocity = JPH::Vec3(vel.value.x, vel.value.y, vel.value.z);
    }

    JPH::BodyID bodyId = bodyInterface.CreateAndAddBody(
        bodySettings, JPH::EActivation::Activate
    );

    registry.emplace<JoltBody>(entity, bodyId);
}
```

Call `createDynamicBody` inline for each physics test cube after creating it in `setupScene`. For example, after the falling cube:

```cpp
// Cube 2: dropped from near the ceiling (pure gravity test)
{
    auto cube = registry.create();
    registry.emplace<Position>(cube, startPos);
    registry.emplace<Velocity>(cube, startVel);
    registry.emplace<AABBCollider>(cube, glm::vec3(0.5f), false);
    // ... other components ...

    createDynamicBody(registry, cube);  // <-- add this line
}
```

Do the same for every test cube. The player entity does **not** get a dynamic body — Chapter 15a handles the player with a `CharacterVirtual` controller instead.

---

## Step 7: The Jolt Sync System

This system reads Jolt body transforms and writes them back into ECS `Position` (and optionally `Velocity`) components. It replaces `movementSystem` and `groundDetectionSystem`.

### New file: `src/engine/ecs/systems/jolt_sync_system.h`

```cpp
#pragma once

#include <entt/entt.hpp>

void joltSyncSystem(entt::registry& registry);
```

### New file: `src/engine/ecs/systems/jolt_sync_system.cpp`

```cpp
#include "engine/ecs/systems/jolt_sync_system.h"
#include "engine/ecs/components.h"
#include "engine/physics/jolt_world.h"

void joltSyncSystem(entt::registry& registry)
{
    auto& jolt = registry.ctx().get<JoltWorld>();
    auto& bodyInterface = jolt.getBodyInterface();

    auto view = registry.view<Position, JoltBody>();
    for (auto [entity, pos, joltBody] : view.each())
    {
        // Read position from Jolt
        JPH::RVec3 joltPos = bodyInterface.GetCenterOfMassPosition(joltBody.id);
        pos.value = glm::vec3(joltPos.GetX(), joltPos.GetY(), joltPos.GetZ());

        // Sync velocity if the entity has one
        if (registry.all_of<Velocity>(entity))
        {
            JPH::Vec3 joltVel = bodyInterface.GetLinearVelocity(joltBody.id);
            auto& vel = registry.get<Velocity>(entity);
            vel.value = glm::vec3(joltVel.GetX(), joltVel.GetY(), joltVel.GetZ());
        }

        // Update ground state if the entity has one
        if (registry.all_of<OnGround>(entity))
        {
            auto& ground = registry.get<OnGround>(entity);
            // A body is "on ground" if it has very low vertical velocity
            // and is not in free-fall. This is a simple heuristic —
            // Chapter 15 replaces this with CharacterVirtual's ground detection.
            JPH::Vec3 joltVel = bodyInterface.GetLinearVelocity(joltBody.id);
            ground.value = std::abs(joltVel.GetY()) < 0.5f;
        }
    }
}
```

---

## Step 8: Wire It Up in main.cpp

### Remove old includes

Remove these includes (the systems are being replaced):

```cpp
// REMOVE these:
#include "engine/ecs/systems/collision_system.h"
#include "engine/ecs/systems/movement_system.h"
#include "engine/ecs/systems/physics_system.h"
#include "engine/physics/spatial_hash.h"
```

> **Keep** `#include "engine/physics/physics_config.h"` — we still use `PhysicsConfig` for the fixed timestep value.

### Add new includes

```cpp
#include "engine/physics/jolt_world.h"
#include "engine/ecs/systems/jolt_sync_system.h"
```

### Initialize Jolt before the game loop

Replace the old `SpatialHash` setup and add Jolt. Keep `PhysicsConfig` for the timestep:

```cpp
// REMOVE:
// SpatialHash spatialHash(4.0f);

// KEEP — we still use fixedDeltaTime:
auto& physicsConfig = registry.ctx().emplace<PhysicsConfig>();

// ADD:
auto& joltWorld = registry.ctx().emplace<JoltWorld>();
joltWorld.init();
```

### Create bodies after scene setup

After `setupScene` returns, create the static Jolt bodies from level geometry. Dynamic bodies are already created inline in `setupScene` (the `createDynamicBody` calls you added in Step 6):

```cpp
Level level = setupScene(registry, resources);

// Create Jolt bodies from the level geometry
createLevelBodies(registry, level);

// Optimise the broad-phase after all bodies are added
joltWorld.physicsSystem->OptimizeBroadPhase();
```

### Update the tick loop

Replace the old system calls:

```cpp
while (fixedTimestep.step())
{
    weaponSwitchSystem(registry);
    playerMovementSystem(registry);

    // REMOVED: physicsSystem(registry);
    moverSystem(registry);
    // REMOVED: collisionSystem(registry, spatialHash, level);
    // REMOVED: movementSystem(registry);

    // NEW: Step Jolt physics and sync transforms back to ECS
    joltWorld.step(physicsConfig.fixedDeltaTime);
    joltSyncSystem(registry);

    // REMOVED: groundDetectionSystem(registry, level);
    combatSystem(registry, level);
    lifetimeSystem(registry);
    triggerSystem(registry);
    demoResetSystem(registry);
}
```

> **Note:** `physicsConfig.fixedDeltaTime` is `1.0f / 60.0f` — the same value used by `FixedTimestep`. By reading from `PhysicsConfig`, both the timestep and the physics step stay in sync. If you ever change the tick rate, you only change it in one place.

### Shutdown Jolt before exit

Before `return 0;` in `main()`:

```cpp
joltWorld.shutdown();
resources.clear();
return 0;
```

---

## Step 8b: Update demo_reset_system for Jolt

The `demoResetSystem` resets cubes to their starting position on a timer. But it only resets the ECS `Position` — the Jolt body stays wherever it fell. On the next frame, `joltSyncSystem` overwrites the ECS position with Jolt's position, causing the cube to flicker.

The fix: when resetting, also teleport the Jolt body.

### Update `demo_reset_system.cpp`

Add the include:

```cpp
#include "engine/physics/jolt_world.h"
```

Then inside the reset block (after the `OnGround` reset), add:

```cpp
// Teleport the Jolt body back to the start position
if (registry.all_of<JoltBody>(entity))
{
    auto& jolt = registry.ctx().get<JoltWorld>();
    auto& body = registry.get<JoltBody>(entity);
    auto& bi = jolt.getBodyInterface();
    bi.SetPosition(body.id,
        JPH::RVec3(demo.startPosition.x, demo.startPosition.y, demo.startPosition.z),
        JPH::EActivation::Activate);
    bi.SetLinearVelocity(body.id,
        JPH::Vec3(demo.startVelocity.x, demo.startVelocity.y, demo.startVelocity.z));
}
```

`SetPosition` teleports the body instantly (no collision response). `Activate` wakes the body so Jolt simulates it again — without this, a body that was at rest would stay asleep at the old position.

---

## Step 8c: Fix Projectile Movement & Jolt Impulses

We just removed `movementSystem`, which applied `Velocity` to `Position` every tick. Dynamic bodies don't need it — Jolt moves them now. But **projectile entities** still rely on velocity-based movement — they don't have Jolt bodies, they're lightweight short-lived entities that fly in a straight line until they hit something.

We also have a new problem: when a projectile hits a dynamic body (like the test cubes), it should **push** it. Before Jolt, the splash damage function applied knockback via the `Velocity` component. Now that Jolt controls dynamic bodies, `Velocity` changes are ignored — we need to apply Jolt **impulses** instead.

### Update `combat_system.cpp`

Add the Jolt include:

```cpp
#include "engine/physics/jolt_world.h"
```

#### Projectile movement

In the projectile collision section of `combatSystem`, add velocity integration **before** the collision check. Projectiles are simple — no collision response, just straight-line flight:

```cpp
// ─── Projectile movement & collision ─────────────────────────
auto projView = registry.view<Position, Velocity, AABBCollider, Projectile>();
std::vector<entt::entity> toDestroy;

for (auto [projEntity, pos, vel, col, proj] : projView.each())
{
    // move projectile (no Jolt body — simple velocity integration)
    pos.value += vel.value * dt;

    AABB projBox = AABB::fromCentreSize(pos.value, col.halfExtents);
```

One line — `pos.value += vel.value * dt` — replaces the role `movementSystem` used to play for projectiles.

#### Jolt impulse on direct hit

Inside the `if (projBox.intersects(targetBox))` block, after the health damage check, add:

```cpp
// push Jolt bodies on impact
if (registry.all_of<JoltBody>(target))
{
    auto& joltBody = registry.get<JoltBody>(target);
    auto& jolt = registry.ctx().get<JoltWorld>();
    glm::vec3 dir = glm::normalize(vel.value);
    float impulseMag = proj.damage * 2.0f;
    jolt.getBodyInterface().AddImpulse
    (
        joltBody.id,
        JPH::Vec3(dir.x * impulseMag, dir.y * impulseMag, dir.z * impulseMag)
    );
}
```

`AddImpulse` applies an instantaneous change in momentum — the body accelerates based on its mass. A 10-damage projectile applies 20 units of impulse in the travel direction, which sends a 1 kg cube flying but barely nudges a 100 kg crate.

#### Jolt impulse for splash damage

The `applySplashDamage` function only iterates entities with `Health` — the test cubes don't have `Health`, so they're skipped entirely. Add a second pass after the existing loop that pushes any Jolt body within the blast radius:

```cpp
// push Jolt bodies away from explosion (even without Health)
auto joltView = registry.view<Position, JoltBody>();
for (auto [entity, pos, joltBody] : joltView.each())
{
    if (entity == ignore) continue;

    float distance = glm::length(pos.value - center);
    if (distance > radius) continue;

    float scale = 1.0f - (distance / radius);
    glm::vec3 pushDir = (distance > 0.01f)
        ? glm::normalize(pos.value - center)
        : glm::vec3(0.0f, 1.0f, 0.0f);
    float knockback = maxDamage * scale * 2.0f;

    auto& jolt = registry.ctx().get<JoltWorld>();
    jolt.getBodyInterface().AddImpulse
    (
        joltBody.id,
        JPH::Vec3(pushDir.x * knockback, pushDir.y * knockback, pushDir.z * knockback)
    );
}
```

The linear falloff means entities at the centre of the explosion get full force, tapering to zero at the edge. The `distance > 0.01f` guard avoids a division-by-zero when the body is exactly at the blast centre — in that case we default to pushing straight up.

> **Why two loops?** The first loop (`Position, Health`) handles damage + velocity knockback for entities that still use the old velocity-based movement (like the player before Chapter 15a). The second loop (`Position, JoltBody`) handles impulse for Jolt-managed bodies. An entity with both `Health` and `JoltBody` gets processed by both — damage from the first, physics push from the second.

---

## Step 9: Update CMakeLists.txt Source Files

### Remove old physics files

Remove these from the `add_executable` source list:

```cmake
# REMOVE:
src/engine/ecs/systems/collision_system.cpp
src/engine/ecs/systems/movement_system.cpp
src/engine/ecs/systems/physics_system.cpp
src/engine/physics/collision.cpp
src/engine/physics/spatial_hash.cpp
```

### Add new files

```cmake
# ADD:
src/engine/physics/jolt_world.cpp
src/engine/ecs/systems/jolt_sync_system.cpp
```

> **Don't delete** the old `.cpp` and `.h` files from disk yet -- just remove them from the build. You might want to reference them later, and they serve as documentation of how the custom physics worked.

---

## What Changed — Summary

| File | Change |
|------|--------|
| `components.h` | Added `JoltBody` component, Jolt includes |
| `scene_setup.cpp` | Added `createLevelBodies()`, `createDynamicBody()`, inline calls for each cube |
| `jolt_sync_system.h/cpp` | **New** -- Reads Jolt body transforms into ECS components |
| `demo_reset_system.cpp` | Added Jolt body teleport on timer reset |
| `combat_system.cpp` | Added projectile velocity integration, Jolt impulse on impact and splash |
| `main.cpp` | Replaced old physics setup and system calls with Jolt |
| `CMakeLists.txt` | Removed old physics sources, added new sources |

### Files removed from build (not deleted)

| File | Was |
|------|-----|
| `collision_system.cpp/h` | AABB sweep collision detection |
| `movement_system.cpp/h` | Velocity integration (`pos += vel * dt`) |
| `physics_system.cpp/h` | Gravity, friction, ground detection |
| `collision.cpp/h` | Sweep AABB helper (Minkowski method) |
| `spatial_hash.cpp/h` | Broad-phase spatial hashing |

---

## What You Should See

After building and running:

1. **The room is solid** -- walls and floor block movement (via Jolt static bodies instead of sweep AABB)
2. **Test cubes fall and land cleanly** -- no jitter, no micro-bouncing. Jolt's resting contact solver keeps them perfectly still once they settle
3. **The sliding cube slides and stops** -- friction is handled by Jolt's material system
4. **The lift and door still work** -- `moverSystem` is unchanged, it animates position independently of physics
5. **Projectiles still fly** -- rockets and nails travel through the air and explode/collide as before
6. **Shooting a cube pushes it** -- fire a rocket near a test cube and watch it fly. Direct hits push in the travel direction, splash damage pushes outward from the blast centre
7. **The player can't move yet** -- that's expected! The player entity doesn't have a Jolt body; we deliberately skipped it. Chapter 15a adds a proper character controller

### Troubleshooting

**Build fails with "cannot find Jolt/Jolt.h":**
- FetchContent needs an internet connection on first build
- Check that `SOURCE_SUBDIR Build` is in your `FetchContent_Declare`
- Clean the build directory and re-run CMake

**Objects fall through the floor:**
- Check that `createLevelBodies` is called after `setupScene`
- Verify the surface fattening produces non-zero thickness bodies
- Call `OptimizeBroadPhase()` after adding all bodies

**Cubes explode or fly away:**
- Check initial positions aren't overlapping other bodies
- Jolt resolves interpenetration aggressively -- make sure cubes spawn above surfaces

**Linker errors about Jolt symbols:**
- Ensure `Jolt` is in `target_link_libraries`
- Make sure `jolt_world.cpp` exists and is in the source list

---

## New C++ Concept: EMotionType

Jolt bodies have three motion types:

| Type | Description | Use For |
|------|-------------|---------|
| `Static` | Never moves. Infinite mass. | Level geometry (floors, walls) |
| `Dynamic` | Fully simulated. Responds to forces. | Physics objects (cubes, barrels) |
| `Kinematic` | Moves via code, not forces. Pushes dynamic bodies. | Moving platforms (lifts, doors) |

A `Kinematic` body is perfect for our `Mover` entities -- Chapter 15b converts them to kinematic Jolt bodies so lifts and doors push the player physically.

---

## What's Next

The physics world is running and objects behave correctly, but the player is frozen -- they have no Jolt body. In **Chapter 15a**, we'll add a `CharacterVirtual` controller for the player with proper ground detection, stair stepping, and jumping.
