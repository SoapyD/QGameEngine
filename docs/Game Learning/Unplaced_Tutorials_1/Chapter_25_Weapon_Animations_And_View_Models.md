# Chapter 25: Weapon Animations & View Models

## What You'll Learn
- What a view model is and why it's rendered separately
- Rendering the player's weapon with a different FOV
- Keyframe animation for fire, reload, and weapon switch
- Procedural effects: view bob, recoil kick, idle sway
- An ECS-friendly animation system

---

## What is a View Model?

The view model is the weapon you see in the bottom-right of the screen in an FPS. It's **not** the same mesh that other players see — it's a separate, higher-detail model rendered in its own pass.

```
┌───────────────────────────────────────┐
│                                       │
│          Game World                   │
│       (rendered normally)             │
│                                       │
│                                       │
│                           ┌───────┐   │
│                           │Weapon │   │
│                           │(view  │   │
│                           │model) │   │
│                           └───────┘   │
└───────────────────────────────────────┘
```

### Why a Separate Render Pass?

1. **Different FOV**: The view model uses a narrower field of view (~55°) than the world (~90°). This prevents the weapon from stretching at the screen edges and stops it clipping into walls.
2. **Depth independence**: The view model is always on top. We clear the depth buffer before rendering it so it never hides behind world geometry.
3. **Screen space**: The model's position is relative to the screen, not the world. When the camera moves, the weapon moves with it automatically.

---

## The ViewModel Component

```cpp
// In components.h

enum class ViewModelState {
    Idle,
    Firing,
    Reloading,
    SwitchingOut,   // Lowering current weapon
    SwitchingIn     // Raising new weapon
};

struct ViewModel {
    // Visual
    std::string meshName;          // Current weapon mesh
    std::string textureName;       // Current weapon texture

    // Base position (rest position, bottom-right of screen)
    glm::vec3 basePosition = glm::vec3(0.3f, -0.3f, -0.5f);
    glm::vec3 baseRotation = glm::vec3(0.0f);

    // Current animated position/rotation (base + all effects combined)
    glm::vec3 currentPosition = glm::vec3(0.3f, -0.3f, -0.5f);
    glm::vec3 currentRotation = glm::vec3(0.0f);

    // Animation state
    ViewModelState state = ViewModelState::Idle;
    float stateTimer = 0.0f;

    // Procedural effects
    float bobTimer = 0.0f;
    float recoilAmount = 0.0f;     // Current recoil offset (springs back to 0)
    float recoilVelocity = 0.0f;   // For spring physics
    float swayTimer = 0.0f;

    // Weapon switch
    std::string pendingWeaponMesh;
    std::string pendingWeaponTexture;
};
```

---

## Keyframe Animation

Each weapon animation (fire, reload, switch) is a sequence of keyframes — snapshots of where the weapon should be at specific times. We interpolate between them.

### Keyframe Data

```cpp
struct Keyframe {
    glm::vec3 position;
    glm::vec3 rotation;    // Euler angles in degrees
    float time;            // Time in seconds from animation start
};

struct WeaponAnimation {
    std::vector<Keyframe> frames;

    float getDuration() const {
        return frames.empty() ? 0.0f : frames.back().time;
    }
};
```

### Defining Animations

Animations are data — just arrays of keyframes. Different weapons have different animations:

```cpp
// Shotgun fire animation: kick back, then return
WeaponAnimation shotgunFire;
shotgunFire.frames = {
    // Start position
    { glm::vec3(0.0f),             glm::vec3(0.0f),           0.0f   },
    // Kick back and up (peak recoil)
    { glm::vec3(0.0f, 0.05f, 0.15f), glm::vec3(-10.0f, 0.0f, 0.0f), 0.05f  },
    // Settle forward slightly
    { glm::vec3(0.0f, 0.02f, 0.05f), glm::vec3(-3.0f, 0.0f, 0.0f),  0.15f  },
    // Return to rest
    { glm::vec3(0.0f),             glm::vec3(0.0f),           0.35f  },
};

// Weapon lower (switching out)
WeaponAnimation weaponLower;
weaponLower.frames = {
    { glm::vec3(0.0f),              glm::vec3(0.0f),            0.0f  },
    { glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(15.0f, 0.0f, 0.0f), 0.25f },
};

// Weapon raise (switching in)
WeaponAnimation weaponRaise;
weaponRaise.frames = {
    { glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(15.0f, 0.0f, 0.0f), 0.0f  },
    { glm::vec3(0.0f),              glm::vec3(0.0f),            0.25f },
};
```

### Interpolating Between Keyframes

```cpp
void evaluateAnimation(const WeaponAnimation& anim, float time,
                        glm::vec3& outPosition, glm::vec3& outRotation) {
    if (anim.frames.empty()) return;

    // Clamp time to animation duration
    time = glm::clamp(time, 0.0f, anim.getDuration());

    // Find the two keyframes to interpolate between
    for (size_t i = 0; i < anim.frames.size() - 1; i++) {
        const auto& a = anim.frames[i];
        const auto& b = anim.frames[i + 1];

        if (time >= a.time && time <= b.time) {
            float range = b.time - a.time;
            float t = (range > 0.001f) ? (time - a.time) / range : 0.0f;

            // Smooth interpolation (not linear — easeInOut feels better for weapons)
            t = t * t * (3.0f - 2.0f * t);  // smoothstep

            outPosition = glm::mix(a.position, b.position, t);
            outRotation = glm::mix(a.rotation, b.rotation, t);
            return;
        }
    }

    // Past the end — hold last frame
    outPosition = anim.frames.back().position;
    outRotation = anim.frames.back().rotation;
}
```

---

## Procedural Effects

These are layered **on top of** keyframe animation. Each one adds an offset to the weapon's position/rotation every frame.

### View Bob (Movement Sway)

The weapon sways in a figure-8 pattern when the player moves:

```cpp
glm::vec3 calculateBob(float bobTimer, float moveSpeed) {
    // Horizontal: sin wave
    // Vertical: cos at double frequency (figure-8)
    float bobFrequency = 8.0f;
    float bobAmplitude = 0.005f * moveSpeed;

    float x = sin(bobTimer * bobFrequency) * bobAmplitude;
    float y = -abs(cos(bobTimer * bobFrequency)) * bobAmplitude * 0.5f;

    return glm::vec3(x, y, 0.0f);
}
```

`bobTimer` increments only while the player is moving and on the ground.

### Recoil Kick (Spring Physics)

When the weapon fires, it kicks back and springs to rest. This uses a damped spring — the same pattern from Chapter 20:

```cpp
void updateRecoil(float& recoilAmount, float& recoilVelocity, float dt) {
    // Spring constants
    float stiffness = 200.0f;   // How fast it returns
    float damping = 15.0f;      // How quickly oscillation dies

    // Spring force: F = -kx - cv
    float force = -stiffness * recoilAmount - damping * recoilVelocity;
    recoilVelocity += force * dt;
    recoilAmount += recoilVelocity * dt;

    // Kill tiny oscillations
    if (abs(recoilAmount) < 0.001f && abs(recoilVelocity) < 0.01f) {
        recoilAmount = 0.0f;
        recoilVelocity = 0.0f;
    }
}

// When weapon fires:
// recoilAmount = 0.0f;
// recoilVelocity = 3.0f;  // Initial kick impulse
```

### Idle Sway

A subtle circular drift when standing still — makes the weapon feel alive:

```cpp
glm::vec3 calculateIdleSway(float swayTimer) {
    float swaySpeed = 1.2f;
    float swayAmount = 0.002f;

    float x = sin(swayTimer * swaySpeed) * swayAmount;
    float y = sin(swayTimer * swaySpeed * 0.7f) * swayAmount * 0.5f;

    return glm::vec3(x, y, 0.0f);
}
```

The different frequencies on X and Y (1.0 vs 0.7) create a Lissajous curve — a natural-looking drift pattern rather than a simple circle.

---

## The View Model System

A stateless free function that processes all ViewModel components:

### src/engine/ecs/systems/view_model_system.h

```cpp
#pragma once

#include <entt/entt.hpp>

void viewModelSystem(entt::registry& registry, float dt);
```

### src/engine/ecs/systems/view_model_system.cpp

```cpp
#include "engine/ecs/systems/view_model_system.h"
#include "engine/ecs/components.h"

// Animation data (would normally be loaded from file or defined per weapon)
extern WeaponAnimation shotgunFire;
extern WeaponAnimation weaponLower;
extern WeaponAnimation weaponRaise;

void viewModelSystem(entt::registry& registry, float dt) {
    auto view = registry.view<ViewModel, Velocity, OnGround>();

    for (auto [entity, vm, vel, ground] : view.each()) {

        // ─── Update state timer ─────────────────────────────────
        vm.stateTimer += dt;

        // ─── Keyframe animation based on state ─────────────────
        glm::vec3 animPos(0.0f);
        glm::vec3 animRot(0.0f);

        switch (vm.state) {
            case ViewModelState::Idle:
                // No keyframe animation — just procedural
                break;

            case ViewModelState::Firing:
                evaluateAnimation(shotgunFire, vm.stateTimer, animPos, animRot);
                if (vm.stateTimer >= shotgunFire.getDuration()) {
                    vm.state = ViewModelState::Idle;
                    vm.stateTimer = 0.0f;
                }
                break;

            case ViewModelState::SwitchingOut:
                evaluateAnimation(weaponLower, vm.stateTimer, animPos, animRot);
                if (vm.stateTimer >= weaponLower.getDuration()) {
                    // Swap the weapon mesh
                    vm.meshName = vm.pendingWeaponMesh;
                    vm.textureName = vm.pendingWeaponTexture;
                    vm.state = ViewModelState::SwitchingIn;
                    vm.stateTimer = 0.0f;
                }
                break;

            case ViewModelState::SwitchingIn:
                evaluateAnimation(weaponRaise, vm.stateTimer, animPos, animRot);
                if (vm.stateTimer >= weaponRaise.getDuration()) {
                    vm.state = ViewModelState::Idle;
                    vm.stateTimer = 0.0f;
                }
                break;

            case ViewModelState::Reloading:
                // Similar pattern — evaluate reload animation
                break;
        }

        // ─── Procedural: view bob ───────────────────────────────
        float speed = glm::length(glm::vec2(vel.value.x, vel.value.z));
        glm::vec3 bob(0.0f);

        if (ground.value && speed > 0.5f) {
            vm.bobTimer += dt * speed * 0.5f;
            bob = calculateBob(vm.bobTimer, speed);
        } else {
            // Smoothly reduce bob when not moving
            vm.bobTimer = 0.0f;
        }

        // ─── Procedural: recoil ─────────────────────────────────
        updateRecoil(vm.recoilAmount, vm.recoilVelocity, dt);
        glm::vec3 recoilOffset(0.0f, 0.0f, vm.recoilAmount);
        glm::vec3 recoilRotation(-vm.recoilAmount * 15.0f, 0.0f, 0.0f); // Pitch up

        // ─── Procedural: idle sway ──────────────────────────────
        vm.swayTimer += dt;
        glm::vec3 sway = calculateIdleSway(vm.swayTimer);

        // ─── Combine all effects ────────────────────────────────
        vm.currentPosition = vm.basePosition + animPos + bob + recoilOffset + sway;
        vm.currentRotation = vm.baseRotation + animRot + recoilRotation;
    }
}
```

### Triggering Fire

From the combat system (Chapter 12), when the weapon fires:

```cpp
// In combatSystem, after firing:
if (registry.all_of<ViewModel>(entity)) {
    auto& vm = registry.get<ViewModel>(entity);
    vm.state = ViewModelState::Firing;
    vm.stateTimer = 0.0f;
    vm.recoilVelocity = 3.0f;  // Kick impulse
}
```

### Triggering Weapon Switch

```cpp
void switchWeapon(entt::registry& registry, entt::entity player,
                   const std::string& newMesh, const std::string& newTexture) {
    auto& vm = registry.get<ViewModel>(player);

    if (vm.state != ViewModelState::Idle) return;  // Can't switch mid-animation

    vm.pendingWeaponMesh = newMesh;
    vm.pendingWeaponTexture = newTexture;
    vm.state = ViewModelState::SwitchingOut;
    vm.stateTimer = 0.0f;
}
```

The flow: `SwitchingOut` (lower) → mesh swap at bottom → `SwitchingIn` (raise) → `Idle`.

---

## Rendering the View Model

### The Render Pass

```cpp
void renderViewModel(entt::registry& registry, const Shader& shader,
                      float aspectRatio) {
    auto view = registry.view<ViewModel>();

    for (auto [entity, vm] : view.each()) {
        // 1. Clear depth buffer — view model always on top
        glClear(GL_DEPTH_BUFFER_BIT);

        // 2. Narrow FOV projection
        glm::mat4 projection = glm::perspective(
            glm::radians(55.0f),  // Narrower than world FOV
            aspectRatio,
            0.01f,                // Very near clip plane
            10.0f                 // Doesn't need to see far
        );

        // 3. View matrix is identity (weapon is in screen space)
        glm::mat4 viewMat = glm::mat4(1.0f);

        // 4. Model matrix positions the weapon
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, vm.currentPosition);
        model = glm::rotate(model, glm::radians(vm.currentRotation.x),
                             glm::vec3(1, 0, 0));
        model = glm::rotate(model, glm::radians(vm.currentRotation.y),
                             glm::vec3(0, 1, 0));
        model = glm::rotate(model, glm::radians(vm.currentRotation.z),
                             glm::vec3(0, 0, 1));

        // 5. Set uniforms and draw
        shader.use();
        shader.setMat4("projection", projection);
        shader.setMat4("view", viewMat);
        shader.setMat4("model", model);

        // Bind weapon mesh and texture, draw
        // mesh.draw();
    }
}
```

### Updated Render Order

```
1. Skybox (depth write off)
2. Opaque level geometry (depth test on, depth write on)
3. Opaque entities (enemies, items, projectiles)
4. Transparent / additive (particles, effects)
5. View model (clear depth, narrow FOV)     ← NEW
6. HUD (orthographic, depth test off)
```

---

## C++ Concept: `enum class` vs Plain `enum`

```cpp
// Plain enum — values leak into the surrounding scope
enum Colour { Red, Green, Blue };
enum TrafficLight { Red, Yellow, Green };  // ERROR: Red and Green already defined!

// enum class — values are scoped
enum class Colour { Red, Green, Blue };
enum class TrafficLight { Red, Yellow, Green };  // Fine — no conflict

Colour c = Colour::Red;          // Must use scope prefix
TrafficLight t = TrafficLight::Red;  // Different type entirely

// int x = Colour::Red;           // ERROR: no implicit conversion to int
int x = static_cast<int>(Colour::Red);  // Explicit conversion required
```

`enum class` is preferred in modern C++ because:
- Values don't pollute the surrounding namespace
- No implicit conversion to `int` (prevents accidental misuse)
- Different enum classes can have the same value names

We use `enum class` for `ViewModelState`, `AIState`, `MoverState`, `WeaponType`, and every other enum in QEngine.

---

## What's Next

In **Chapter 26**, we'll build a boss fight — a multi-phase encounter that demonstrates how complex gameplay emerges from combining existing ECS systems. No new engine features required.
