# Chapter 10: Physics & Movement

## What You'll Learn
- Gravity, friction, and terminal velocity
- Fixed timestep — why physics must run at a constant rate
- Jumping and ground detection
- Quake-style air control and bunny hopping
- Stair stepping — walking up small ledges smoothly
- The physics system in ECS

---

## Why a Separate Physics Chapter?

Collision detection (Chapter 9) tells us "these things are touching." Physics tells us **why things move** — gravity pulls you down, friction slows you on the ground, jumping launches you upward. Physics generates the velocities; collision corrects them.

---

## Fixed Timestep

Physics must run at a **constant rate**, independent of frame rate. Why?

If physics runs once per frame:
- At 60fps: gravity applies 60 times per second
- At 144fps: gravity applies 144 times per second → you fall faster
- At 30fps: gravity applies 30 times per second → you fall slower

The fix: run physics at a fixed rate (e.g. 60 times per second) and let rendering run at whatever frame rate it wants.

### The Accumulator Pattern

Update the game loop in `src/main.cpp`. Your current loop uses `deltaTime` directly, which varies with frame rate. Replace it with a fixed-timestep accumulator:

```cpp
// ─── Game Loop ───────────────────────────────────────────────
constexpr float FIXED_TIMESTEP = 1.0f / 60.0f;  // 60 physics ticks per second
float accumulator = 0.0f;
float lastFrame = 0.0f;

// enable depth testing (so closer things draw in front of further things)
glEnable(GL_DEPTH_TEST);

while (!window.shouldClose())
{
    float currentFrame = (float)glfwGetTime();
    float frameTime = currentFrame - lastFrame;
    lastFrame = currentFrame;

    // Cap frame time to prevent spiral of death
    if (frameTime > 0.25f) frameTime = 0.25f;

    accumulator += frameTime;

    input.update();
    window.pollEvents();

    // ─── Input (runs once per frame) ─────────────────────────
    if (input.isKeyPressed(GLFW_KEY_ESCAPE))
        glfwSetWindowShouldClose(window.getHandle(), true);

    if (input.isKeyPressed(GLFW_KEY_W))
        camera.processKeyboard(Camera::FORWARD, frameTime);
    if (input.isKeyPressed(GLFW_KEY_S))
        camera.processKeyboard(Camera::BACKWARD, frameTime);
    if (input.isKeyPressed(GLFW_KEY_A))
        camera.processKeyboard(Camera::LEFT, frameTime);
    if (input.isKeyPressed(GLFW_KEY_D))
        camera.processKeyboard(Camera::RIGHT, frameTime);

    camera.processMouse(input.getMouseXOffset(), input.getMouseYOffset());

    // ─── Fixed-rate systems (physics tick) ───────────────────
    while (accumulator >= FIXED_TIMESTEP)
    {
        physicsSystem(registry, FIXED_TIMESTEP);
        collisionSystem(registry, spatialHash, level, FIXED_TIMESTEP);
        movementSystem(registry, FIXED_TIMESTEP);

        accumulator -= FIXED_TIMESTEP;
    }

    // ─── Render (runs once per frame at variable rate) ───────
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    float aspectRatio = (float)window.getWidth() / (float)window.getHeight();
    renderSystem(registry, camera, aspectRatio);

    window.swapBuffers();
}
```

You'll also need to add a new include at the top of `src/main.cpp` for the physics system (which we'll create later in this chapter):

```cpp
#include "engine/ecs/systems/physics_system.h"
```

Note the key changes from the Chapter 9 version:
- `deltaTime` and `lastFrame` are replaced by `FIXED_TIMESTEP`, `accumulator`, and `frameTime`
- Physics, collision, and movement now run inside the `while (accumulator >= FIXED_TIMESTEP)` loop using `FIXED_TIMESTEP` instead of `deltaTime`
- Camera input still uses `frameTime` (the actual frame duration) for smooth look/movement
- `physicsSystem` is new — we'll implement it later in this chapter

### C++ Concept: `constexpr`

```cpp
constexpr float FIXED_TIMESTEP = 1.0f / 60.0f;
```

`constexpr` means "evaluated at compile time." The compiler computes `1.0f / 60.0f` once and bakes the result into the executable. It's like `const` but stronger — guaranteed to be a compile-time constant.

Use `constexpr` for values that are truly constant and known at compile time (physics constants, math constants, buffer sizes).

### The Spiral of Death

If a frame takes longer than `FIXED_TIMESTEP`, the accumulator grows. If physics is slow, we need more physics ticks to catch up, which makes the frame even slower... a death spiral.

The `if (frameTime > 0.25f) frameTime = 0.25f` cap prevents this. If a frame takes longer than 250ms, we just let the simulation slow down rather than trying to catch up.

---

## Physics Components

Add to `components.h`:

```cpp
struct Gravity {
    float strength = 20.0f;    // Quake uses ~800 units/s² (our scale is smaller)
};

struct OnGround {
    bool value = false;
};

struct CharacterPhysics {
    float groundFriction = 6.0f;
    float airFriction = 0.1f;
    float maxGroundSpeed = 7.0f;
    float maxAirSpeed = 1.0f;       // Quake's air speed cap (enables bunny hopping!)
    float groundAcceleration = 10.0f;
    float airAcceleration = 10.0f;
    float jumpForce = 8.0f;
    float stepHeight = 1.5f;         // Max height of a step the player can walk up
};
```

**Illustrative example** — this is what a complete player entity would look like using these components. Don't add this yet; we'll wire up a proper player entity in a later chapter once the input and movement systems are in place:
```cpp
registry.emplace<Position>(player, glm::vec3(0.0f, 1.0f, 0.0f));
registry.emplace<Velocity>(player);
registry.emplace<Gravity>(player);
registry.emplace<OnGround>(player);
registry.emplace<CharacterPhysics>(player);
registry.emplace<AABBCollider>(player, glm::vec3(0.4f, 0.9f, 0.4f), false);
registry.emplace<TagPlayer>(player);  // TagPlayer would be a simple tag component
```

---

## The Physics System

### src/engine/ecs/systems/physics_system.h

```cpp
#pragma once

#include <entt/entt.hpp>

void physicsSystem(entt::registry& registry, float dt);
```

### src/engine/ecs/systems/physics_system.cpp

```cpp
#include "engine/ecs/systems/physics_system.h"
#include "engine/ecs/components.h"

void physicsSystem(entt::registry& registry, float dt) {
    // ─── Apply gravity ───────────────────────────────────────────
    auto gravityView = registry.view<Velocity, Gravity, OnGround>();

    for (auto [entity, vel, grav, ground] : gravityView.each()) {
        if (!ground.value) {
            vel.value.y -= grav.strength * dt;

            // Terminal velocity (cap fall speed)
            if (vel.value.y < -50.0f) {
                vel.value.y = -50.0f;
            }
        }
    }

    // ─── Apply friction ──────────────────────────────────────────
    auto frictionView = registry.view<Velocity, OnGround, CharacterPhysics>();

    for (auto [entity, vel, ground, phys] : frictionView.each()) {
        float friction = ground.value ? phys.groundFriction : phys.airFriction;

        // Only apply friction to horizontal movement (X and Z)
        glm::vec2 horizontal(vel.value.x, vel.value.z);
        float speed = glm::length(horizontal);

        if (speed < 0.1f) {
            // Below threshold — just stop
            if (ground.value) {
                vel.value.x = 0.0f;
                vel.value.z = 0.0f;
            }
            continue;
        }

        // Friction reduces speed
        float drop = speed * friction * dt;
        float newSpeed = std::max(speed - drop, 0.0f);
        float scale = newSpeed / speed;

        vel.value.x *= scale;
        vel.value.z *= scale;
    }
}
```

---

## Ground Detection

How do we know if the player is on the ground? After collision resolution, we check if there's a solid surface directly below.

Add the declaration to `src/engine/ecs/systems/physics_system.h`. You'll need to forward-declare `Level` since the header doesn't know about it yet:

```cpp
#pragma once

#include <entt/entt.hpp>

struct Level;  // forward declaration (defined in engine/level/level.h)

void physicsSystem(entt::registry& registry, float dt);
void groundDetectionSystem(entt::registry& registry, const Level& level);
```

Then add the implementation to `src/engine/ecs/systems/physics_system.cpp`. You'll need to include the raycast and level headers at the top of the file:

```cpp
#include "engine/physics/raycast.h"
#include "engine/level/level.h"
```

```cpp
void groundDetectionSystem(entt::registry& registry, const Level& level) {
    auto view = registry.view<Position, AABBCollider, OnGround>();

    for (auto [entity, pos, col, ground] : view.each()) {
        // Cast a short ray downward from the bottom of the collider
        glm::vec3 feetPos = pos.value - glm::vec3(0.0f, col.halfExtents.y, 0.0f);
        float probeDistance = 0.1f;  // Small distance below feet

        Ray downRay;
        downRay.origin = feetPos;
        downRay.direction = glm::vec3(0.0f, -1.0f, 0.0f);

        ground.value = false;

        // Check against level geometry
        for (const auto& sector : level.sectors) {
            for (const auto& surface : sector.surfaces) {
                // Only check roughly horizontal surfaces (floors)
                if (surface.normal.y < 0.7f) continue;

                // Build a thin AABB for the surface
                AABB surfBox;
                surfBox.min = glm::min(
                    glm::min(surface.vertices[0], surface.vertices[1]),
                    glm::min(surface.vertices[2], surface.vertices[3]));
                surfBox.max = glm::max(
                    glm::max(surface.vertices[0], surface.vertices[1]),
                    glm::max(surface.vertices[2], surface.vertices[3]));
                surfBox.min.y -= 0.05f;
                surfBox.max.y += 0.05f;

                auto hit = rayIntersectsAABB(downRay, surfBox);
                if (hit.has_value() && hit.value() <= probeDistance) {
                    ground.value = true;
                    break;
                }
            }
            if (ground.value) break;
        }
    }
}
```

The `0.7f` normal check means a surface must be roughly horizontal (normal pointing mostly upward) to count as ground. Steep slopes wouldn't count — the player would slide off them.

### Wiring It Up in main.cpp

Add `groundDetectionSystem` to the fixed-timestep loop in `src/main.cpp`, **after** `movementSystem` (positions must be final before we probe the ground):

```cpp
while (accumulator >= FIXED_TIMESTEP)
{
    physicsSystem(registry, FIXED_TIMESTEP);
    collisionSystem(registry, spatialHash, level, FIXED_TIMESTEP);
    movementSystem(registry, FIXED_TIMESTEP);
    groundDetectionSystem(registry, level);  // ← NEW

    accumulator -= FIXED_TIMESTEP;
}
```

### Testing It

To verify gravity and ground detection are working, update the second test cube in `src/engine/ecs/scene_setup.cpp`. Rename it from `wall` to `cube2`, raise it to `y = 4.0`, and add `Velocity`, `AABBCollider`, `Gravity`, and `OnGround`:

```cpp
auto cube2 = registry.create();
registry.emplace<Position>(cube2, glm::vec3(2.0f, 4.0f, 0.0f));    // raised up
registry.emplace<Velocity>(cube2);                                    // starts stationary
registry.emplace<AABBCollider>(cube2, glm::vec3(0.5f), false);       // 1m cube collider
registry.emplace<Gravity>(cube2);                                     // falls under gravity
registry.emplace<OnGround>(cube2);                                    // ground detection target
registry.emplace<MeshRenderer>
(
    cube2,
    cubeMesh->getVAO(),
    0u,
    litShader->getId(),
    wallTexture->getId(),
    true,
    cubeMesh->getIndexCount()
);
```

When you run the engine, the box should fall from its raised position, collide with the floor, and stop — proving that gravity, collision, and ground detection are all working together.

---

## Jumping

Jumping is simple — when the player presses jump and is on the ground, set the Y velocity.

**Illustrative** — this depends on a `PlayerInput` component that we'll create in a later chapter. Don't add this yet; it shows the concept of how jumping will work once player input is wired up. It would go in `src/engine/ecs/systems/physics_system.cpp`:

```cpp
void handleJump(entt::registry& registry) {
    auto view = registry.view<Velocity, OnGround, CharacterPhysics, PlayerInput>();

    for (auto [entity, vel, ground, phys, input] : view.each()) {
        if (input.jump && ground.value) {
            vel.value.y = phys.jumpForce;
            ground.value = false;  // Immediately leave the ground
        }
    }
}
```

This can be called from the input system or as a separate system. The jump force is a velocity, not an acceleration — you instantly get upward speed, and gravity pulls you back down.

---

## Quake-Style Movement: Acceleration and Air Control

Quake's movement is legendary. It feels incredibly responsive and has emergent mechanics like bunny hopping and strafe jumping. The key is how acceleration works differently on the ground vs in the air.

### Ground Acceleration

On the ground, the player accelerates toward their desired direction up to `maxGroundSpeed`.

**Illustrative** — this helper and the `characterMovementSystem` below both depend on `PlayerInput`, which we'll create in a later chapter. Don't add these yet. They would go in `src/engine/ecs/systems/physics_system.cpp`:

```cpp
void applyAcceleration(glm::vec3& velocity, const glm::vec3& wishDir,
                        float wishSpeed, float accel, float dt) {
    // Project current velocity onto the desired direction
    float currentSpeed = glm::dot(velocity, wishDir);

    // How much speed we need to add
    float addSpeed = wishSpeed - currentSpeed;
    if (addSpeed <= 0.0f) return;  // Already at or above target speed

    // How much speed we CAN add this frame
    float accelSpeed = accel * dt * wishSpeed;

    // Don't add more than needed
    if (accelSpeed > addSpeed) accelSpeed = addSpeed;

    velocity += wishDir * accelSpeed;
}
```

### Air Acceleration (The Secret to Bunny Hopping)

In the air, the same function is called but with `maxAirSpeed` instead of `maxGroundSpeed`. Here's the magic: `maxAirSpeed` in Quake is tiny (about 30 units/s vs 320 ground speed). But the acceleration function only caps the **component of velocity in the desired direction** — not the total speed.

This means: if you're moving forward at 320 u/s and you press forward+strafe, the **forward** component is already above 30, so no acceleration is added forward. But the **strafe** component is 0, which is below 30, so strafe acceleration is applied. The result: your total speed **increases** beyond the ground cap.

```
Going forward at 320 →
                        ↗ After strafe accel, total speed > 320
                       ╱
                      ╱
Press strafe →
```

This is why Quake players strafe-jump — it's not a bug, it's an emergent consequence of this acceleration model.

### Putting It Together (Illustrative)

```cpp
void characterMovementSystem(entt::registry& registry, float dt) {
    auto view = registry.view<Velocity, OnGround, CharacterPhysics, PlayerInput>();

    for (auto [entity, vel, ground, phys, input] : view.each()) {
        // Build wish direction from input (already in world space from input system)
        glm::vec3 wishDir(input.moveDir.x, 0.0f, input.moveDir.y);
        float wishSpeed = 0.0f;

        if (glm::length(wishDir) > 0.001f) {
            wishDir = glm::normalize(wishDir);
            wishSpeed = ground.value ? phys.maxGroundSpeed : phys.maxAirSpeed;
        }

        // Apply acceleration
        float accel = ground.value ? phys.groundAcceleration : phys.airAcceleration;
        applyAcceleration(vel.value, wishDir, wishSpeed, accel, dt);
    }
}
```

---

## Stair Stepping (Concept — Future Chapter)

In Quake, the player glides up small steps without jumping. This will be implemented in a later chapter when we have a walking player entity. For now, here's the concept:

1. Try to move forward
2. If blocked, try moving up by `stepHeight`, then forward, then back down
3. If the second attempt gets further, use it

```cpp
void stairStep(glm::vec3& position, glm::vec3& velocity,
               const AABBCollider& collider, const Level& level,
               float stepHeight, float dt) {
    glm::vec3 originalPos = position;
    glm::vec3 horizontalVel = glm::vec3(velocity.x, 0.0f, velocity.z);

    // Attempt 1: normal movement
    glm::vec3 normalEnd = position + horizontalVel * dt;
    // (run collision check to see how far we actually get)
    // ... normalEnd = resolved position after collision

    // Attempt 2: step up, move, step down
    glm::vec3 steppedPos = position + glm::vec3(0.0f, stepHeight, 0.0f);
    // (check collision for the upward move — might hit a ceiling)

    glm::vec3 steppedEnd = steppedPos + horizontalVel * dt;
    // (check collision for the horizontal move at the raised height)

    // Step back down
    steppedEnd.y = position.y;
    // (check collision for the downward move — find the actual floor)

    // Use whichever attempt moved further horizontally
    float normalDist = glm::length(normalEnd - originalPos);
    float steppedDist = glm::length(steppedEnd - originalPos);

    if (steppedDist > normalDist) {
        position = steppedEnd;
    } else {
        position = normalEnd;
    }
}
```

**Illustrative pseudo-code** — the actual implementation would use swept AABB tests for each phase and would go in `src/engine/ecs/systems/physics_system.cpp`. The concept is: "if direct movement fails, try going up and over."

> **Note:** In Chapter 13 we implement a simplified version of this using a collision-skip approach: if a horizontal sweep hits an obstacle whose top is within `stepHeight` of the player's feet, we skip the collision entirely and let gravity + ground detection handle the vertical adjustment. This is less general than the three-step algorithm above but works well for walking over thin platforms like lifts.

---

## Bitwise Collision Layers

Not everything should collide with everything. Players shouldn't collide with their own bullets. Enemies might walk through each other but not through walls.

**Collision layers** solve this using bitmasks.

**Illustrative** — this redefines `AABBCollider` with `layer` and `mask` fields, which would break the current version in `src/engine/ecs/components.h`. Don't apply this yet; we'll integrate collision layers when we add enemies and projectiles in a later chapter:

```cpp
// C++ Concept: Bitwise operations
namespace CollisionLayer {
    constexpr uint32_t NONE        = 0;
    constexpr uint32_t WORLD       = 1 << 0;  // 0b00000001
    constexpr uint32_t PLAYER      = 1 << 1;  // 0b00000010
    constexpr uint32_t ENEMY       = 1 << 2;  // 0b00000100
    constexpr uint32_t PROJECTILE  = 1 << 3;  // 0b00001000
    constexpr uint32_t PICKUP      = 1 << 4;  // 0b00010000
    constexpr uint32_t TRIGGER     = 1 << 5;  // 0b00100000
}

struct AABBCollider {
    glm::vec3 halfExtents = glm::vec3(0.5f);
    bool isTrigger = false;
    uint32_t layer = CollisionLayer::WORLD;     // What layer am I on?
    uint32_t mask = 0xFFFFFFFF;                  // What layers do I collide with?
};
```

Two entities collide only if each one's layer is in the other's mask. This would go in `src/engine/ecs/systems/collision_system.cpp` when collision layers are implemented:

```cpp
bool shouldCollide(const AABBCollider& a, const AABBCollider& b) {
    return (a.layer & b.mask) != 0 && (b.layer & a.mask) != 0;
}
```

### C++ Concept: Bitwise Operations

```cpp
1 << 3    // Shift 1 left by 3 positions = 0b00001000 = 8
a & b     // AND: bits that are 1 in both a and b
a | b     // OR: bits that are 1 in either a or b
a ^ b     // XOR: bits that are 1 in exactly one of a or b
~a        // NOT: flip all bits
```

Example: A player projectile should hit enemies and world, but not the player:

```cpp
// Player projectile:
layer = CollisionLayer::PROJECTILE
mask  = CollisionLayer::WORLD | CollisionLayer::ENEMY  // 0b00000101

// Player:
layer = CollisionLayer::PLAYER                          // 0b00000010

// Check: projectile.mask & player.layer = 0b00000101 & 0b00000010 = 0
// No collision — the projectile passes through the player.
```

---

## The Complete Tick Order for Physics

This is the full target order. Systems marked with `*` are implemented in this chapter; the rest are illustrative and will be added in later chapters:

```
1. InputSystem              ← Read WASD, mouse, jump button         (future)
2. CharacterMovementSystem  ← Apply acceleration based on input     (future)
3. HandleJump               ← Set Y velocity if jumping             (future)
4. PhysicsSystem *          ← Apply gravity and friction
5. CollisionSystem *        ← Sweep and resolve against world + entities
6. MovementSystem *         ← Apply final velocity to position
7. GroundDetectionSystem *  ← Update OnGround for next frame
8. ...
9. RenderSystem *
```

---

## What's Next

In **Chapter 11**, we'll add interactive level elements — doors that open, lifts that move, and trigger volumes that activate events. These bring the level to life.
