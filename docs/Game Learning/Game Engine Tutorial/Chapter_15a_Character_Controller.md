# Chapter 15a: Jolt Character Controller — CharacterVirtual & Player Movement

## What You'll Learn
- Using Jolt's `CharacterVirtual` for player movement
- Wiring WASD/jump input into the character controller
- Automatic stair stepping and slope handling
- Quake-style ground and air acceleration via Jolt

---

## The Goal

Chapter 14 gave us a Jolt-powered physics world with solid floors and falling cubes. Now we add the player. Rather than building a character controller from scratch (as we did with `playerMovementSystem` + `physicsSystem` + `groundDetectionSystem`), we'll use Jolt's built-in `CharacterVirtual` — a specialised class designed for FPS player movement.

---

## Step 1: CharacterVirtual vs Character

Jolt offers two character controller types:

| | `Character` | `CharacterVirtual` |
|---|---|---|
| **Physics** | Full rigid body in the simulation | No rigid body — uses collision queries |
| **Control** | Less direct — apply forces | Direct — set velocity each frame |
| **Interactions** | Other bodies see and push it | Invisible to other bodies (optional proxy) |
| **Best for** | AI characters | Player characters |

We'll use `CharacterVirtual` because:
- We want direct velocity control (Quake-style acceleration)
- We need to update it inside our fixed timestep loop with precise timing
- The player's movement rules are game-specific, not physics-derived

---

## Step 2: Player Character System

This system replaces both `playerMovementSystem` and the old `physicsSystem` (gravity/friction) for the player entity. It creates a `CharacterVirtual`, applies input-driven velocity, and steps the character each tick.

### New Component: `JoltCharacter`

Add to `components.h`:

```cpp
#include <Jolt/Physics/Character/CharacterVirtual.h>

// Links an ECS entity to a Jolt CharacterVirtual controller
struct JoltCharacter
{
    JPH::Ref<JPH::CharacterVirtual> character;
};
```

### New file: `src/engine/ecs/systems/player_character_system.h`

```cpp
#pragma once

#include <entt/entt.hpp>

void initPlayerCharacter(entt::registry& registry);
void playerCharacterSystem(entt::registry& registry);
```

### New file: `src/engine/ecs/systems/player_character_system.cpp`

```cpp
#include "engine/ecs/systems/player_character_system.h"
#include "engine/ecs/components.h"
#include "engine/physics/jolt_world.h"
#include "engine/physics/physics_config.h"

#include <Jolt/Physics/Character/CharacterVirtual.h>

void initPlayerCharacter(entt::registry& registry)
{
    auto& jolt = registry.ctx().get<JoltWorld>();

    auto view = registry.view<Position, AABBCollider, TagPlayer>();
    for (auto [entity, pos, col] : view.each())
    {
        // Create a capsule shape for the player
        // Capsule height = total height minus the two hemisphere caps
        float radius = col.halfExtents.x;  // 0.3
        float halfHeight = col.halfExtents.y - radius;  // 0.85 - 0.3 = 0.55
        if (halfHeight < 0.01f) halfHeight = 0.01f;

        JPH::Ref<JPH::Shape> capsuleShape = new JPH::CapsuleShape(halfHeight, radius);

        // Configure the character
        JPH::Ref<JPH::CharacterVirtualSettings> settings = new JPH::CharacterVirtualSettings();
        settings->mShape = capsuleShape;
        settings->mMaxSlopeAngle = JPH::DegreesToRadians(50.0f);
        settings->mMaxStrength = 100.0f;
        settings->mMass = 70.0f;
        settings->mPredictiveContactDistance = 0.1f;

        // Create the character at the entity's current position
        JPH::Ref<JPH::CharacterVirtual> character = new JPH::CharacterVirtual(
            settings,
            JPH::RVec3(pos.value.x, pos.value.y, pos.value.z),
            JPH::Quat::sIdentity(),
            0,  // user data
            jolt.physicsSystem.get()
        );

        registry.emplace<JoltCharacter>(entity, character);
    }
}

void playerCharacterSystem(entt::registry& registry)
{
    const auto& config = registry.ctx().get<PhysicsConfig>();
    float dt = config.fixedDeltaTime;
    auto& jolt = registry.ctx().get<JoltWorld>();

    auto view = registry.view<Position, JoltCharacter, PlayerInput, CharacterPhysics, OnGround>();
    for (auto [entity, pos, joltChar, input, physics, ground] : view.each())
    {
        auto& character = joltChar.character;

        // ─── Read ground state from Jolt ────────────────────────
        bool onGround = character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
        ground.value = onGround;

        // ─── Build velocity from input ──────────────────────────
        JPH::Vec3 currentVel = character->GetLinearVelocity();
        JPH::Vec3 desiredVel(0.0f, 0.0f, 0.0f);

        if (onGround)
        {
            // Ground movement — Quake-style acceleration
            JPH::Vec3 wishDir(input.wishDir.x, 0.0f, input.wishDir.z);
            float wishSpeed = physics.maxGroundSpeed;

            if (wishDir.LengthSq() > 0.0f)
            {
                wishDir = wishDir.Normalized();
                float currentSpeed = currentVel.Dot(wishDir);
                float addSpeed = wishSpeed - currentSpeed;
                if (addSpeed > 0.0f)
                {
                    float accelSpeed = physics.groundAcceleration * wishSpeed * dt;
                    if (accelSpeed > addSpeed) accelSpeed = addSpeed;
                    desiredVel = JPH::Vec3(currentVel.GetX(), 0.0f, currentVel.GetZ())
                                 + wishDir * accelSpeed;
                }
                else
                {
                    desiredVel = JPH::Vec3(currentVel.GetX(), 0.0f, currentVel.GetZ());
                }
            }
            else
            {
                // No input — apply ground friction
                JPH::Vec3 horizontalVel(currentVel.GetX(), 0.0f, currentVel.GetZ());
                float speed = horizontalVel.Length();
                if (speed > 0.1f)
                {
                    float drop = speed * physics.groundFriction * dt;
                    float newSpeed = std::max(speed - drop, 0.0f);
                    desiredVel = horizontalVel * (newSpeed / speed);
                }
            }

            // Jump
            if (input.jump)
            {
                desiredVel += JPH::Vec3(0.0f, physics.jumpForce, 0.0f);
            }
            else
            {
                // Keep ground velocity vertical component
                desiredVel += JPH::Vec3(0.0f, currentVel.GetY(), 0.0f);
            }
        }
        else
        {
            // Air movement — limited air control
            JPH::Vec3 wishDir(input.wishDir.x, 0.0f, input.wishDir.z);

            if (wishDir.LengthSq() > 0.0f)
            {
                wishDir = wishDir.Normalized();
                float currentSpeed = JPH::Vec3(currentVel.GetX(), 0.0f, currentVel.GetZ()).Dot(wishDir);
                float addSpeed = physics.maxAirSpeed - currentSpeed;
                if (addSpeed > 0.0f)
                {
                    float accelSpeed = physics.airAcceleration * physics.maxAirSpeed * dt;
                    if (accelSpeed > addSpeed) accelSpeed = addSpeed;
                    desiredVel = JPH::Vec3(currentVel.GetX(), currentVel.GetY(), currentVel.GetZ())
                                 + wishDir * accelSpeed;
                }
                else
                {
                    desiredVel = JPH::Vec3(currentVel.GetX(), currentVel.GetY(), currentVel.GetZ());
                }
            }
            else
            {
                desiredVel = JPH::Vec3(currentVel.GetX(), currentVel.GetY(), currentVel.GetZ());
            }

            // Apply gravity while in the air
            desiredVel += JPH::Vec3(0.0f, -20.0f * dt, 0.0f);
        }

        character->SetLinearVelocity(desiredVel);

        // ─── Step the character ─────────────────────────────────
        // ExtendedUpdate handles collision, stair stepping, and floor sticking
        JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;
        updateSettings.mWalkStairsStepUp = JPH::Vec3(0.0f, physics.stepHeight, 0.0f);
        updateSettings.mStickToFloorStepDown = JPH::Vec3(0.0f, -physics.stepHeight, 0.0f);

        character->ExtendedUpdate(
            dt,
            -character->GetUp() * jolt.physicsSystem->GetGravity().Length(),
            updateSettings,
            jolt.physicsSystem->GetDefaultBroadPhaseLayerFilter(Layers::MOVING),
            jolt.physicsSystem->GetDefaultLayerFilter(Layers::MOVING),
            {},  // body filter
            {},  // shape filter
            *jolt.tempAllocator
        );

        // ─── Write position back to ECS ─────────────────────────
        JPH::RVec3 charPos = character->GetPosition();
        pos.value = glm::vec3(charPos.GetX(), charPos.GetY(), charPos.GetZ());
    }
}
```

### Key Differences from the Old System

| Old Approach | Jolt Approach |
|---|---|
| Manual gravity in `physicsSystem` | Gravity applied to velocity before `ExtendedUpdate` |
| Manual friction with `horizontalVel *= factor` | Same Quake-style friction, but Jolt handles resting contact |
| `groundDetectionSystem` raycasting | `character->GetGroundState()` — built-in ground detection |
| Stair-stepping hack in `collisionSystem` | `ExtendedUpdate` with `mWalkStairsStepUp` — automatic |
| Player position via `movementSystem` | `character->GetPosition()` — Jolt resolves collisions internally |

---

## What Changed — Summary

| File | Change |
|------|--------|
| `components.h` | Added `JoltCharacter` component |
| `player_character_system.h/cpp` | **New** — Quake-style movement via Jolt CharacterVirtual |

---

## New C++ Concept: CharacterVirtual

`CharacterVirtual` is Jolt's solution for player characters in games. Unlike a rigid body, it doesn't participate in the physics simulation directly — instead, it performs its own collision queries each frame.

The key method is `ExtendedUpdate`:

```cpp
character->ExtendedUpdate(
    deltaTime,        // how far forward in time to simulate
    gravity,          // gravity vector (for floor sticking)
    updateSettings,   // stair step and floor stick configuration
    broadPhaseFilter, // which broad-phase layers to collide with
    layerFilter,      // which object layers to collide with
    bodyFilter,       // per-body filtering (empty = all)
    shapeFilter,      // per-shape filtering (empty = all)
    allocator         // Jolt temp allocator
);
```

This single call:
1. Applies the character's velocity to move it forward
2. Detects collisions and slides along surfaces
3. Steps up stairs if the obstacle is short enough
4. Sticks to the floor when walking down slopes
5. Updates ground state (on ground, in air, on steep slope)

All the behaviour we hand-coded across four systems — in one function call.

---

## What's Next

The player can move, jump, and collide with the world. In **Chapter 15b**, we'll convert movers (lifts, doors) to kinematic Jolt bodies so they push the player physically, and set up sensor bodies for trigger volumes.
