# Chapter 13: Player Body & Debug HUD

## What You'll Learn
- Giving the player a physical body that interacts with the world
- Reversing the camera-player relationship: physics drives position, camera follows
- Wiring WASD input through the ECS velocity/acceleration pipeline
- Jumping, gravity, and ground detection for the player
- Clamping health at zero so the player can actually die
- A simple bitmap debug HUD showing health and FPS

---

## The Problem

Right now, the player is a **floating camera**. The camera's `processKeyboard()` moves its position directly, and `main.cpp` copies that position into the player entity. This means:

- **Lifts don't carry the player** — the camera doesn't know about movers
- **Lava doesn't feel dangerous** — health decreases but nothing stops you
- **Gravity doesn't exist for the player** — you can fly anywhere
- **Collision is one-way** — the entity has a collider but the camera ignores it

The fix is to **reverse the relationship**: input sets a desired movement direction, the physics system applies acceleration/friction/gravity, the collision system resolves collisions, and then the camera reads the resulting position.

```
BEFORE (Ch 12):   Camera → Position  (camera drives everything)
AFTER  (Ch 13):   Input → Velocity → Physics → Position → Camera
```

---

## Step 1: Give the Player a Physical Body

The player entity already has `Position`, `AABBCollider`, `Health`, `PlayerInput`, and `TagPlayer`. We need to add the physics components.

### Update `scene_setup.cpp`

Find the player entity section and add `Velocity`, `Gravity`, `OnGround`, and `CharacterPhysics`:

```cpp
// ─── Player entity ────────────────────────────────────────
auto player = registry.create();
registry.emplace<Position>(player, glm::vec3(15.0f, 1.7f, 15.0f));
registry.emplace<Velocity>(player);                        // NEW
registry.emplace<AABBCollider>(player, glm::vec3(0.3f, 0.85f, 0.3f), false);
registry.emplace<Gravity>(player);                         // NEW
registry.emplace<OnGround>(player);                        // NEW
registry.emplace<CharacterPhysics>(player);                // NEW
registry.emplace<Health>(player, 100.0f, 100.0f);
registry.emplace<PlayerInput>(player);
registry.emplace<TagPlayer>(player);
```

That's it for the entity setup. All four components already exist in `components.h` from chapters 9-10 — we just never attached them to the player before.

With the default `CharacterPhysics` values, the player gets:
- `groundFriction = 6.0f` — snappy ground stopping
- `airFriction = 0.1f` — Quake-style air control
- `jumpForce = 8.0f` — a solid jump
- `groundAcceleration = 10.0f` — responsive ground movement
- `maxGroundSpeed = 7.0f` — sensible top speed

---

## Step 2: Expand PlayerInput

The `PlayerInput` component needs to carry movement wishes and a jump flag, not just fire/weaponSwitch.

### Update `components.h`

```cpp
// Input state for the player — set each frame from InputManager
struct PlayerInput
{
    bool fire = false;
    int weaponSwitch = -1;  // -1 = no switch, 0+ = weapon slot

    // Movement (NEW)
    glm::vec3 wishDir = glm::vec3(0.0f);  // desired move direction (normalised)
    bool jump = false;
};
```

`wishDir` is the direction the player wants to move, in **world space**, built from WASD + the camera's facing direction. It's normalised so diagonal movement isn't faster than cardinal movement.

---

## Step 3: Rewrite the Input Section in `main.cpp`

This is the big change. Instead of calling `camera.processKeyboard()`, we build a `wishDir` from the camera's facing vectors and store it in `PlayerInput`. The camera only handles mouse look — it no longer moves itself.

### Replace the entire input and sync section

Remove all of the following from the old input/sync section:

1. The four `camera.processKeyboard()` calls (W/A/S/D)
2. The `playerView` sync loop that copies the camera position into the player entity:
   ```cpp
   // Sync player entity position to camera
   auto playerView = registry.view<Position, TagPlayer>();
   for (auto [entity, pos] : playerView.each()) {
       pos.value = camera.getPosition();
   }
   ```

This loop is no longer needed — in fact, it's the exact thing we're reversing. Previously the camera moved itself and we copied that position to the player entity. Now, WASD input drives the player's `Velocity` through the physics pipeline, and the camera reads the player's `Position` afterward (see Step 5).

Here's the full replacement:

```cpp
// ─── Input ───────────────────────────────────────────────
if (input.isKeyPressed(GLFW_KEY_ESCAPE))
    glfwSetWindowShouldClose(window.getHandle(), true);

// Mouse look — camera still handles this directly
camera.processMouse(input.getMouseXOffset(), input.getMouseYOffset());

// Build movement wish direction from WASD + camera facing
glm::vec3 front = camera.getFront();
glm::vec3 right = glm::normalize(glm::cross(front, glm::vec3(0, 1, 0)));

// Flatten to horizontal plane (don't fly when looking up/down)
front.y = 0.0f;
front = glm::normalize(front);
right.y = 0.0f;
right = glm::normalize(right);

glm::vec3 wishDir(0.0f);
if (input.isKeyPressed(GLFW_KEY_W)) wishDir += front;
if (input.isKeyPressed(GLFW_KEY_S)) wishDir -= front;
if (input.isKeyPressed(GLFW_KEY_A)) wishDir -= right;
if (input.isKeyPressed(GLFW_KEY_D)) wishDir += right;

// Normalise to prevent faster diagonal movement
if (glm::length(wishDir) > 0.0f)
    wishDir = glm::normalize(wishDir);

// Write camera direction into registry context (for combat system)
registry.ctx().insert_or_assign<glm::vec3>(camera.getFront());

// ─── Populate PlayerInput from GLFW ──────────────────────
auto inputView = registry.view<PlayerInput>();
for (auto [entity, playerInput] : inputView.each())
{
    playerInput.wishDir = wishDir;
    playerInput.jump = input.isKeyPressed(GLFW_KEY_SPACE);
    playerInput.fire = (glfwGetMouseButton(window.getHandle(),
        GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS);
    playerInput.weaponSwitch = -1;
    if (input.isKeyPressed(GLFW_KEY_1)) playerInput.weaponSwitch = 0;
    if (input.isKeyPressed(GLFW_KEY_2)) playerInput.weaponSwitch = 1;
}
```

Notice what's **gone**: `camera.processKeyboard()` and the `pos.value = camera.getPosition()` sync before physics. The camera no longer moves itself — it just tracks where the player ends up.

---

## Step 4: The Player Movement System

We need a new system that reads `PlayerInput` and applies acceleration to `Velocity`. This is the Quake-style movement from Chapter 10, but now actually used by the player.

### New file: `src/engine/ecs/systems/player_movement_system.h`

```cpp
#pragma once

#include <entt/entt.hpp>
#include "engine/level/level.h"

void playerMovementSystem(entt::registry& registry);
```

### New file: `src/engine/ecs/systems/player_movement_system.cpp`

```cpp
#include "engine/ecs/systems/player_movement_system.h"
#include "engine/ecs/components.h"
#include "engine/physics/physics_config.h"

void playerMovementSystem(entt::registry& registry)
{
    const auto& config = registry.ctx().get<PhysicsConfig>();
    float dt = config.fixedDeltaTime;

    auto view = registry.view<PlayerInput, Velocity, OnGround, CharacterPhysics>();

    for (auto [entity, input, vel, ground, phys] : view.each())
    {
        // ─── Jumping ─────────────────────────────────────────
        if (input.jump && ground.value)
        {
            vel.value.y = phys.jumpForce;
            ground.value = false;  // leave the ground immediately
        }

        // ─── Horizontal acceleration (Quake-style) ──────────
        glm::vec3 wishDir = input.wishDir;
        float wishSpeed;
        float accel;

        if (ground.value)
        {
            wishSpeed = phys.maxGroundSpeed;
            accel = phys.groundAcceleration;
        }
        else
        {
            wishSpeed = phys.maxAirSpeed;
            accel = phys.airAcceleration;
        }

        if (glm::length(wishDir) < 0.01f)
            continue;  // no input, let friction handle deceleration

        // Project current velocity onto wish direction
        float currentSpeed = glm::dot(
            glm::vec3(vel.value.x, 0.0f, vel.value.z), wishDir
        );
        float addSpeed = wishSpeed - currentSpeed;

        if (addSpeed <= 0.0f)
            continue;  // already at or above wish speed in this direction

        float accelSpeed = accel * wishSpeed * dt;
        if (accelSpeed > addSpeed)
            accelSpeed = addSpeed;

        vel.value.x += wishDir.x * accelSpeed;
        vel.value.z += wishDir.z * accelSpeed;
    }
}
```

This is the classic Quake acceleration algorithm:
1. Project current horizontal velocity onto the wish direction
2. Calculate how much speed we need to add to reach `wishSpeed`
3. Cap the acceleration so we never exceed `wishSpeed` in one frame
4. On the ground: high max speed, high acceleration = snappy
5. In the air: low max speed, same acceleration = allows air strafing but limits top speed

The key insight is that air acceleration with a low `maxAirSpeed` (1.0) is what enables **bunny hopping** — you can't accelerate much in a straight line in the air, but strafing perpendicular to your velocity adds speed without being capped.

---

## Step 5: Sync Camera to Player (After Physics)

Now the camera needs to **follow** the player body, not drive it. Replace the old two-way sync with a one-way read after physics.

### In `main.cpp`, after the fixed timestep loop

Remove the old sync-back code and replace with:

```cpp
// ─── Camera follows player body ──────────────────────────
auto playerView = registry.view<Position, TagPlayer>();
for (auto [entity, pos] : playerView.each())
{
    camera.setPosition(pos.value);
}
```

That's it. The camera reads wherever the physics system put the player. If the player gets teleported, pushed by an explosion, or carried by a lift — the camera follows automatically.

### The eye height question

You might notice the player spawns at `y=1.7` and the collider has `halfExtents.y = 0.85`. That means the collider spans from `y=0.85` to `y=2.55` — the Position is at the **centre** of the collider, not at eye level.

For a first-person camera, we want the camera at eye height — near the top of the collider. Add an offset:

```cpp
// ─── Camera follows player body ──────────────────────────
auto playerView = registry.view<Position, AABBCollider, TagPlayer>();
for (auto [entity, pos, col] : playerView.each())
{
    // Camera sits near the top of the collider (eye height)
    glm::vec3 eyePos = pos.value;
    eyePos.y += col.halfExtents.y * 0.7f;  // 70% up from centre
    camera.setPosition(eyePos);
}
```

With `halfExtents.y = 0.85`, this puts the camera `0.85 * 0.7 = 0.595` units above centre, which is `1.7 + 0.595 = 2.295` units above the floor — a reasonable eye height for a ~1.7m tall character.

### Update fire origin in combat system

The combat system in `combat_system.cpp` currently uses `pos.value` as the fire origin. Since the player Position is now at the body centre (not eye level), we need the combat system to account for this. The simplest approach is to use the camera direction context value that's already in the registry, and fire from the camera position.

In `main.cpp`, store the camera position in context alongside the direction:

```cpp
// Write camera direction and position into registry context
registry.ctx().insert_or_assign<glm::vec3>(camera.getFront());
```

The combat system already reads `pos.value` for the fire origin. Since we sync the camera *after* physics, the combat system runs during the fixed timestep with the player's body position. We need to offset the fire origin to eye height in `combat_system.cpp`.

Find the fire origin line in `combatSystem()`:

```cpp
glm::vec3 fireOrigin = pos.value; // Already at eye height (synced from camera)
```

Replace with:

```cpp
// Fire from eye height — position is at body centre, offset upward
float eyeOffset = 0.0f;
if (registry.all_of<AABBCollider>(entity))
{
    eyeOffset = registry.get<AABBCollider>(entity).halfExtents.y * 0.7f;
}
glm::vec3 fireOrigin = pos.value + glm::vec3(0.0f, eyeOffset, 0.0f);
```

---

## Step 6: Update the System Tick Order

Add `playerMovementSystem` to the fixed timestep loop. It must run **before** `physicsSystem` (which applies gravity and friction to the velocity that `playerMovementSystem` just set).

### In `main.cpp`

```cpp
#include "engine/ecs/systems/player_movement_system.h"  // NEW
```

```cpp
while (fixedTimestep.step())
{
    weaponSwitchSystem(registry);
    playerMovementSystem(registry);              // NEW — apply input to velocity
    physicsSystem(registry);                     // gravity, friction
    moverSystem(registry);                       // update doors, lifts
    collisionSystem(registry, spatialHash, level);
    movementSystem(registry);                    // apply velocities to positions
    groundDetectionSystem(registry, level);
    combatSystem(registry, level);
    lifetimeSystem(registry);
    triggerSystem(registry);
    demoResetSystem(registry);
}

// Sync camera to player position (after physics)
auto playerView = registry.view<Position, AABBCollider, TagPlayer>();
for (auto [entity, pos, col] : playerView.each())
{
    glm::vec3 eyePos = pos.value;
    eyePos.y += col.halfExtents.y * 0.7f;  // eye height
    camera.setPosition(eyePos);
}
```

The camera sync sits **outside** the fixed timestep loop — it runs once per frame, reading whatever position the physics pipeline produced. This is the same code from Step 5, shown here so you can see the complete block in context.

### Add to `CMakeLists.txt`

Add the new source file to the executable:

```cmake
src/engine/ecs/systems/player_movement_system.cpp
```

---

## Step 7: Clamp Health at Zero

Right now, health can go negative indefinitely. Lava does 25 damage per second, but nothing stops the counter going to -500 if you stand in it. We need to clamp health at zero in two places:

### 1. The trigger system (lava, damage zones)

In `trigger_system.cpp`, find the `TriggerAction::Damage` case:

```cpp
case TriggerAction::Damage:
{
    if (registry.all_of<Health>(entity))
    {
        auto& health = registry.get<Health>(entity);
        health.current -= trigger.value * dt;
        if (health.current < 0.0f) health.current = 0.0f;  // NEW
    }
    break;
}
```

### 2. The combat system (weapon damage)

In `combat_system.cpp`, there are three places where damage is applied. Add the clamp after each one.

**Hitscan hit** (in `fireHitscan`):

```cpp
if (registry.all_of<Health>(entityHit->entity))
{
    auto& health = registry.get<Health>(entityHit->entity);
    health.current -= weapon.damage;
    if (health.current < 0.0f) health.current = 0.0f;  // NEW
}
```

**Direct projectile hit** (in projectile collision section):

```cpp
if (registry.all_of<Health>(target))
{
    auto& health = registry.get<Health>(target);
    health.current -= proj.damage;
    if (health.current < 0.0f) health.current = 0.0f;  // NEW
}
```

**Splash damage** (in `applySplashDamage`):

```cpp
float damage = maxDamage * scale;
health.current -= damage;
if (health.current < 0.0f) health.current = 0.0f;  // NEW
```

Now health will never go below zero. In Chapter 14 (platforming) we'll add death and respawn behaviour when health reaches zero.

---

## Step 8: Debug Text HUD

We need to see what's happening — health, FPS, and eventually more debug info. We'll build a simple bitmap font renderer that draws text in screen space.

### The Approach

OpenGL doesn't have built-in text rendering. The common approaches are:

1. **FreeType** — full font loading, complex but flexible
2. **Bitmap font atlas** — pre-rendered character grid, simple and fast
3. **stb_easy_font** — part of the stb library collection, extremely simple

We'll use **stb_easy_font** since we already have the stb library in our project. It generates vertex data for ASCII text with zero setup — no textures, no font files, just vertices.

### Get stb_easy_font.h

Download `stb_easy_font.h` from the stb repository and place it in your `extern/stb/` directory alongside `stb_image.h`. It's a single header file.

You can get it from: the stb GitHub repository, file `stb_easy_font.h`.

### HUD Config Component

First, add a context struct to `components.h` so the HUD system knows which shader to use:

```cpp
// Config for the debug HUD overlay (stored in registry context)
struct HudConfig
{
    unsigned int shaderId = 0;
};
```

This avoids storing a raw `unsigned int` in registry context (which would collide with any other `unsigned int` context value).

### The Debug HUD System

This system renders text using `stb_easy_font`, which generates 2D quad vertices. Since OpenGL core profile doesn't support `GL_QUADS`, we convert the quads to triangles using an index buffer.

### New file: `src/engine/ecs/systems/debug_hud_system.h`

```cpp
#pragma once

#include <entt/entt.hpp>

void debugHudSystem(entt::registry& registry, int windowWidth,
    int windowHeight, float fps);
```

### New file: `src/engine/ecs/systems/debug_hud_system.cpp`

```cpp
#include "engine/ecs/systems/debug_hud_system.h"
#include "engine/ecs/components.h"
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <stb_easy_font.h>
#include <cstdio>
#include <vector>

// ─── Internal: draw a string at screen position (x, y) ──────────
// stb_easy_font outputs quads as 4 vertices each, with a stride of
// 16 bytes per vertex (x, y, z, colour as 4 floats).
// Core profile doesn't support GL_QUADS, so we convert to triangles
// using an index buffer: each quad becomes 2 triangles (6 indices).

static void drawText(float x, float y, const char* text,
    unsigned int shaderId, const glm::mat4& projection, float scale,
    const glm::vec3& color)
{
    static char vertexBuffer[4096 * 16];
    int numQuads = stb_easy_font_print(x, y, const_cast<char*>(text),
        nullptr, vertexBuffer, sizeof(vertexBuffer));

    if (numQuads <= 0) return;

    // Build index buffer: convert quads to triangles
    // Quad vertices: 0,1,2,3 → triangles: (0,1,2) and (0,2,3)
    std::vector<unsigned int> indices;
    indices.reserve(numQuads * 6);
    for (int i = 0; i < numQuads; i++)
    {
        unsigned int base = i * 4;
        indices.push_back(base + 0);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base + 0);
        indices.push_back(base + 2);
        indices.push_back(base + 3);
    }

    unsigned int vao, vbo, ebo;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);

    glBindVertexArray(vao);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, numQuads * 64, vertexBuffer, GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
        indices.size() * sizeof(unsigned int),
        indices.data(), GL_DYNAMIC_DRAW);

    // stb_easy_font vertex layout: x, y, z, colour (4 floats per vertex)
    // We only need x and y — stride is 16 bytes
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, (void*)0);

    // Set up shader
    glUseProgram(shaderId);

    GLint loc = glGetUniformLocation(shaderId, "projection");
    glUniformMatrix4fv(loc, 1, GL_FALSE, &projection[0][0]);

    loc = glGetUniformLocation(shaderId, "textColor");
    glUniform3fv(loc, 1, &color[0]);

    glDrawElements(GL_TRIANGLES, (int)indices.size(), GL_UNSIGNED_INT, 0);

    glBindVertexArray(0);
    glDeleteBuffers(1, &ebo);
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
}

void debugHudSystem(entt::registry& registry, int windowWidth,
    int windowHeight, float fps)
{
    auto* hudConfig = registry.ctx().find<HudConfig>();
    if (!hudConfig || hudConfig->shaderId == 0) return;

    // ─── Set up orthographic projection for 2D rendering ─────
    // Origin at top-left, Y increases downward (screen space)
    glm::mat4 ortho = glm::ortho(0.0f, (float)windowWidth,
        (float)windowHeight, 0.0f, -1.0f, 1.0f);

    // disable depth testing and face culling for HUD overlay
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    // ─── Gather debug info ───────────────────────────────────
    float health = 0.0f;
    float maxHealth = 0.0f;

    auto playerView = registry.view<Health, TagPlayer>();
    for (auto [entity, hp] : playerView.each())
    {
        health = hp.current;
        maxHealth = hp.max;
    }

    // ─── Build and draw text strings ─────────────────────────
    float textScale = 2.0f;  // stb_easy_font is tiny — scale it up
    unsigned int shader = hudConfig->shaderId;

    // FPS — always white
    char fpsText[64];
    snprintf(fpsText, sizeof(fpsText), "FPS: %.0f", fps);
    drawText(5.0f, 5.0f, fpsText, shader, ortho, textScale,
        glm::vec3(1.0f));

    // Health — yellow when low, red when critical
    char healthText[64];
    snprintf(healthText, sizeof(healthText), "HP: %.0f / %.0f",
        health, maxHealth);

    glm::vec3 healthColor(1.0f);  // white
    if (health <= 0.0f)
        healthColor = glm::vec3(1.0f, 0.0f, 0.0f);  // red — dead
    else if (health < 30.0f)
        healthColor = glm::vec3(1.0f, 1.0f, 0.0f);  // yellow — danger

    drawText(5.0f, 20.0f, healthText, shader, ortho, textScale,
        healthColor);

    // Re-enable depth testing and face culling for the next frame's 3D rendering
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
}
```

> **Why disable face culling?** `stb_easy_font` generates quads with clockwise winding order, which is the opposite of OpenGL's default counter-clockwise front-face convention. With `GL_CULL_FACE` enabled (as we set up in `window.cpp`), these quads get back-face culled and nothing renders. Disabling it for the HUD pass is the simplest fix.

### The HUD Shader

We need a minimal shader that takes vertex positions and draws them in a flat colour. This is much simpler than our lit shader. The input is declared as `vec3` to match `stb_easy_font`'s output format (x, y, z per vertex), though we only upload 2 floats per vertex via `glVertexAttribPointer` — OpenGL fills the missing z with 0.0 automatically. No model matrix is needed since positions are already in screen space.

### New file: `assets/shaders/hud.vert`

```glsl
#version 460 core

// input: vertex position (location 0 matches our C++ vertex data layout)
layout (location = 0) in vec3 aPos;

// transformation matrix, set from C++ code
uniform mat4 projection;

void main()
{
    // gl_Position is a built-in variable - the final screen position
    // No model matrix needed — positions are already in screen space
    gl_Position = projection * vec4(aPos, 1.0);
}
```

### New file: `assets/shaders/hud.frag`

```glsl
#version 460 core

out vec4 FragColor;

uniform vec3 textColor;

void main()
{
    FragColor = vec4(textColor, 1.0);
}
```

### Register the HUD Shader

In `main.cpp`, load the HUD shader and store its config in the registry context:

```cpp
// After loading other shaders
auto hudShader = resources.getShader("hud",
    "assets/shaders/hud.vert",
    "assets/shaders/hud.frag"
);
```

After creating the registry and `PhysicsConfig`:

```cpp
// Register HUD config in context for the debug HUD system
auto& hudConfig = registry.ctx().emplace<HudConfig>();
hudConfig.shaderId = hudShader->getId();
```

### FPS Calculation

We need to track FPS. Add a simple counter in `main.cpp`:

```cpp
// Before the game loop
float fpsTimer = 0.0f;
int frameCount = 0;
float currentFps = 0.0f;
```

Inside the game loop, before rendering:

```cpp
// ─── FPS counter ─────────────────────────────────────────
frameCount++;
fpsTimer += frameTime;
if (fpsTimer >= 1.0f)
{
    currentFps = (float)frameCount / fpsTimer;
    frameCount = 0;
    fpsTimer = 0.0f;
}
```

### Call the Debug HUD

In `main.cpp`, after `renderSystem` and before `window.swapBuffers()`:

```cpp
#include "engine/ecs/systems/debug_hud_system.h"  // at the top
```

```cpp
// After renderSystem
debugHudSystem(registry, window.getWidth(), window.getHeight(), currentFps);

window.swapBuffers();
```

### Add to `CMakeLists.txt`

```cmake
src/engine/ecs/systems/debug_hud_system.cpp
```

---

## The Full Updated `main.cpp`

Here's the complete file after all changes. Compare with your current version to see what moved:

```cpp
#include "engine/core/window.h"
#include "engine/core/input_manager.h"
#include "engine/core/resource_manager.h"
#include "engine/core/fixed_timestep.h"
#include "engine/ecs/components.h"
#include "engine/ecs/scene_setup.h"
#include "engine/ecs/systems/collision_system.h"
#include "engine/ecs/systems/combat_system.h"
#include "engine/ecs/systems/debug_hud_system.h"
#include "engine/ecs/systems/demo_reset_system.h"
#include "engine/ecs/systems/lifetime_system.h"
#include "engine/ecs/systems/movement_system.h"
#include "engine/ecs/systems/mover_system.h"
#include "engine/ecs/systems/physics_system.h"
#include "engine/ecs/systems/player_movement_system.h"
#include "engine/ecs/systems/render_system.h"
#include "engine/ecs/systems/trigger_system.h"
#include "engine/ecs/systems/weapon_switch_system.h"
#include "engine/physics/spatial_hash.h"
#include "engine/physics/physics_config.h"
#include "engine/renderer/camera.h"

#include <entt/entt.hpp>

int main()
{
    // ─── Core systems ────────────────────────────────────────
    Window window(1280, 720, "QEngine");

    InputManager input;
    input.init(window.getHandle());

    ResourceManager resources;

    // ─── Load resources ──────────────────────────────────────
    auto basicShader = resources.getShader("basic",
        "assets/shaders/basic.vert",
        "assets/shaders/basic.frag"
    );

    auto texturedShader = resources.getShader("textured",
        "assets/shaders/textured.vert",
        "assets/shaders/textured.frag"
    );

    auto litShader = resources.getShader("lit",
        "assets/shaders/lit.vert",
        "assets/shaders/lit.frag"
    );

    auto hudShader = resources.getShader("hud",             // NEW
        "assets/shaders/hud.vert",
        "assets/shaders/hud.frag"
    );

    auto wallTexture = resources.getTexture("wall", "assets/textures/wall.png");
    auto gridGrey   = resources.getTexture("grid_grey",   "assets/textures/grid_grey.png");
    auto gridOrange = resources.getTexture("grid_orange", "assets/textures/grid_orange.png");
    auto gridBlue   = resources.getTexture("grid_blue",   "assets/textures/grid_blue.png");
    auto gridGreen  = resources.getTexture("grid_green",  "assets/textures/grid_green.png");
    auto gridRed    = resources.getTexture("grid_red",    "assets/textures/grid_red.png");

    auto cubeMesh = resources.getMesh("cube", "assets/models/cube.obj");

    // ─── Camera ──────────────────────────────────────────────
    Camera camera(glm::vec3(15.0f, 1.7f, 15.0f));

    // ─── ECS: Create the world ───────────────────────────────
    entt::registry registry;

    auto& physicsConfig = registry.ctx().emplace<PhysicsConfig>();
    auto& hudConfig = registry.ctx().emplace<HudConfig>();     // NEW: HUD config
    hudConfig.shaderId = hudShader->getId();
    Level level = setupScene(registry, resources);
    SpatialHash spatialHash(4.0f);

    // ─── FPS tracking ────────────────────────────────────────  NEW
    float fpsTimer = 0.0f;
    int frameCount = 0;
    float currentFps = 0.0f;

    // ─── Game Loop ───────────────────────────────────────────
    FixedTimestep fixedTimestep(physicsConfig.fixedDeltaTime);

    glEnable(GL_DEPTH_TEST);

    while (!window.shouldClose())
    {
        fixedTimestep.accumulate((float)glfwGetTime());
        float frameTime = fixedTimestep.getFrameTime();

        input.update();
        window.pollEvents();

        // ─── FPS counter ─────────────────────────────────────  NEW
        frameCount++;
        fpsTimer += frameTime;
        if (fpsTimer >= 1.0f)
        {
            currentFps = (float)frameCount / fpsTimer;
            frameCount = 0;
            fpsTimer = 0.0f;
        }

        // ─── Input ───────────────────────────────────────────
        if (input.isKeyPressed(GLFW_KEY_ESCAPE))
            glfwSetWindowShouldClose(window.getHandle(), true);

        // Mouse look only — camera no longer moves itself
        camera.processMouse(input.getMouseXOffset(), input.getMouseYOffset());

        // Build movement wish direction from WASD + camera facing
        glm::vec3 front = camera.getFront();
        glm::vec3 right = glm::normalize(
            glm::cross(front, glm::vec3(0, 1, 0)));

        // Flatten to horizontal plane
        front.y = 0.0f;
        front = glm::normalize(front);
        right.y = 0.0f;
        right = glm::normalize(right);

        glm::vec3 wishDir(0.0f);
        if (input.isKeyPressed(GLFW_KEY_W)) wishDir += front;
        if (input.isKeyPressed(GLFW_KEY_S)) wishDir -= front;
        if (input.isKeyPressed(GLFW_KEY_A)) wishDir -= right;
        if (input.isKeyPressed(GLFW_KEY_D)) wishDir += right;

        if (glm::length(wishDir) > 0.0f)
            wishDir = glm::normalize(wishDir);

        // Write camera direction into registry context
        registry.ctx().insert_or_assign<glm::vec3>(camera.getFront());

        // ─── Populate PlayerInput from GLFW ──────────────────
        auto inputView = registry.view<PlayerInput>();
        for (auto [entity, playerInput] : inputView.each())
        {
            playerInput.wishDir = wishDir;
            playerInput.jump = input.isKeyPressed(GLFW_KEY_SPACE);
            playerInput.fire = (glfwGetMouseButton(window.getHandle(),
                GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS);
            playerInput.weaponSwitch = -1;
            if (input.isKeyPressed(GLFW_KEY_1)) playerInput.weaponSwitch = 0;
            if (input.isKeyPressed(GLFW_KEY_2)) playerInput.weaponSwitch = 1;
        }

        // ─── ECS Systems (tick order!) ───────────────────────
        while (fixedTimestep.step())
        {
            weaponSwitchSystem(registry);
            playerMovementSystem(registry);              // NEW
            physicsSystem(registry);
            moverSystem(registry);
            collisionSystem(registry, spatialHash, level);
            movementSystem(registry);
            groundDetectionSystem(registry, level);
            combatSystem(registry, level);
            lifetimeSystem(registry);
            triggerSystem(registry);
            demoResetSystem(registry);
        }

        // ─── Camera follows player body ──────────────────────  CHANGED
        auto playerView = registry.view<Position, AABBCollider, TagPlayer>();
        for (auto [entity, pos, col] : playerView.each())
        {
            glm::vec3 eyePos = pos.value;
            eyePos.y += col.halfExtents.y * 0.7f;
            camera.setPosition(eyePos);
        }

        // ─── Render ──────────────────────────────────────────
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        float aspectRatio = (float)window.getWidth() / (float)window.getHeight();
        renderSystem(registry, camera, aspectRatio);

        // ─── Debug HUD ───────────────────────────────────────  NEW
        debugHudSystem(registry, window.getWidth(),
            window.getHeight(), currentFps);

        window.swapBuffers();
    }

    resources.clear();
    return 0;
}
```

---

## Step 9: Stair-Stepping — Walking Over Low Obstacles

Now that the player has a physical body, you'll notice a problem: try walking onto the lift, and you're blocked. The player's AABB collides with the lift's thin collider (only 0.2 units tall) sideways — the sweep detects a horizontal hit and cancels your velocity, even though the obstacle is low enough to step over.

This is the stair-stepping problem previewed in Chapter 10. The full Quake-style approach (try up, move forward, try down) is complex. We'll use a simpler method: if a horizontal collision hits an obstacle that's short enough relative to the player's feet, skip the collision entirely and let gravity + ground detection handle the vertical adjustment.

### Update `collision_system.cpp`

In the entity-vs-entity collision section, wrap the existing sweep response with a height check. Replace the existing `sweepAABB` hit block:

```cpp
SweepResult hit = sweepAABB(entityBox, movement, otherBox);
if (hit.hit)
{
    // stair-step check: if the obstacle is short enough, walk over it
    float stepHeight = 0.3f;
    float playerBottom = pos.value.y - col.halfExtents.y;
    float obstacleTop = otherPos.value.y + otherCol.halfExtents.y;

    if (hit.normal.y == 0.0f && (obstacleTop - playerBottom) <= stepHeight)
    {
        // low obstacle — skip the collision, let the player walk over
        continue;
    }

    float dot = glm::dot(vel.value, hit.normal);
    if (dot < 0.0f)
    {
        vel.value -= hit.normal * dot;
    }
}
```

The key details:

- **`hit.normal.y == 0.0f`** — only skip horizontal collisions. If the normal points up or down, it's a floor/ceiling hit and we must resolve it normally.
- **`stepHeight = 0.3f`** — generous enough for the lift (0.2 units tall) but small enough not to let the player walk through waist-high obstacles.
- After skipping the collision, `groundDetectionSystem` will snap the player down onto the obstacle's top surface on the next frame, producing a smooth step-up effect.

---

## What Changed — Summary

| File | Change |
|------|--------|
| `components.h` | Added `wishDir` and `jump` to `PlayerInput`, added `HudConfig` struct |
| `scene_setup.cpp` | Added `Velocity`, `Gravity`, `OnGround`, `CharacterPhysics` to player |
| `main.cpp` | Removed camera movement, added wish direction, reversed camera sync, added FPS counter, added HUD shader and debug HUD call |
| `player_movement_system.h/cpp` | **New** — Quake-style acceleration from input |
| `debug_hud_system.h/cpp` | **New** — bitmap font debug overlay |
| `assets/shaders/hud.vert` | **New** — 2D projection shader |
| `assets/shaders/hud.frag` | **New** — flat colour fragment shader |
| `collision_system.cpp` | Added stair-stepping for entity-vs-entity collisions |
| `combat_system.cpp` | Fire origin offset to eye height |
| `trigger_system.cpp` | Health clamped to zero on damage |
| `CMakeLists.txt` | Added two new .cpp files |

---

## What You Should See

After building and running:

1. **WASD moves the player through the physics system** — you'll feel momentum, friction stops you when you let go of keys
2. **Spacebar jumps** — the player leaves the ground, gravity pulls them back
3. **You can walk onto the lift** — the stair-step fix prevents the lift's thin collider from blocking your path
4. **Lava hurts and stops at zero** — walk into the red pool and watch the HUD health tick down to 0
5. **FPS and health display** in the top-left corner as simple white text
6. **Health turns yellow** when below 30
7. **Weapons still work** — hitscan tracers and projectiles fire from eye height

### Troubleshooting

**Player falls through the floor:**
- Check that `OnGround` and `Gravity` are both on the player entity
- Ensure `groundDetectionSystem` runs after `movementSystem` in the tick order
- Verify the player's collider halfExtents match the spawn height

**Movement feels floaty or unresponsive:**
- The default `CharacterPhysics` values are tuned for Quake-style movement. If it feels too fast, reduce `maxGroundSpeed`. If too slow, increase `groundAcceleration`.
- Ground friction of 6.0 provides snappy stopping. Lower values = more sliding.

**Camera jitters:**
- Make sure the camera sync happens *outside* the fixed timestep loop, in the per-frame section

**HUD text not visible:**
- Check the HUD shader loaded successfully (no compilation errors in console)
- Ensure `stb_easy_font.h` is in the `extern/stb/` directory
- Verify `glDisable(GL_DEPTH_TEST)` is called before HUD drawing

---

## New C++ Concept: Quake-Style Acceleration

The acceleration algorithm in `playerMovementSystem` deserves a closer look. It's deceptively simple but creates nuanced movement:

```cpp
float currentSpeed = glm::dot(horizontalVel, wishDir);
float addSpeed = wishSpeed - currentSpeed;
float accelSpeed = accel * wishSpeed * dt;
if (accelSpeed > addSpeed) accelSpeed = addSpeed;
```

The `dot` product projects your current velocity onto the wish direction. If you're moving perpendicular to where you want to go (strafing), `currentSpeed` is near zero — so you get full acceleration even if you're already going fast. This is the mathematical basis of bunny hopping and strafe jumping.

In the air, `maxAirSpeed = 1.0` means you can barely accelerate forward — but strafe perpendicular to your velocity and the dot product allows it, adding speed to your total velocity vector even though the component in the wish direction is capped.

This isn't a bug — it's arguably the most influential "accidental feature" in FPS history.

---

## What's Next

The player now has a real body that obeys physics. In **Chapter 14**, we'll tune the physics for satisfying low-gravity platforming, add variable jump height, coyote time, and build a proper platforming test arena with floating platforms to jump between.
