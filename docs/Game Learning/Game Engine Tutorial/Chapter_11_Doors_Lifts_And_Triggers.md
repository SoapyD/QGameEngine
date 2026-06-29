# Chapter 11: Doors, Lifts & Triggers

## What You'll Learn
- State machines — how interactive objects track what they're doing
- Trigger volumes — detecting when a player enters an area
- Doors that open and close
- Lifts (elevators) that carry the player
- Timed events — delays, auto-close, sequences
- How Quake defines these entities

---

## How Quake Did It

In Quake, every interactive level element is a **brush entity** — a piece of level geometry with special behaviour defined by key-value properties. A door might look like:

```
{
  "classname" "func_door"
  "angle" "90"
  "speed" "100"
  "wait" "3"
  "lip" "8"
}
```

The engine sees `func_door` and knows: this geometry should move, in a direction, at a speed, wait some seconds, then return. No scripting language needed — just data.

We'll build the same approach using ECS components.

---

## State Machines

Interactive objects exist in a state. A door is either closed, opening, open, or closing. A state machine manages these transitions:

```
        ┌─── triggered ───┐
        ▼                  │
    ┌────────┐         ┌────────┐
    │ CLOSED │────────▶│OPENING │
    └────────┘         └───┬────┘
        ▲                  │
        │           reached end
        │                  ▼
    ┌────────┐         ┌────────┐
    │CLOSING │◀────────│  OPEN  │
    └────────┘  timer  └────────┘
                expired
```

### The State Component

```cpp
// Add to components.h

enum class MoverState {
    Idle,       // At start position
    Moving,     // Moving to end position
    Waiting,    // At end position, waiting before returning
    Returning   // Moving back to start position
};

struct Mover {
    glm::vec3 startPos;          // Where it starts (closed position)
    glm::vec3 endPos;            // Where it ends (open position)
    float speed = 2.0f;          // Units per second
    float waitTime = 3.0f;       // Seconds to stay open
    float timer = 0.0f;          // Current timer
    float progress = 0.0f;       // 0.0 = start, 1.0 = end
    MoverState state = MoverState::Idle;
    bool requiresTrigger = true; // Must be triggered to start
};
```

### C++ Concept: `enum class` for State Machines

`enum class` is ideal for state machines:

```cpp
enum class MoverState { Idle, Moving, Waiting, Returning };

MoverState state = MoverState::Idle;

switch (state) {
    case MoverState::Idle:      /* ... */ break;
    case MoverState::Moving:    /* ... */ break;
    case MoverState::Waiting:   /* ... */ break;
    case MoverState::Returning: /* ... */ break;
}
```

The compiler can warn you if your `switch` doesn't handle all cases. With raw integers or old-style enums, typos and missing cases are silent bugs.

---

## Trigger Volumes

A trigger is an invisible box in the world. When the player enters it, something happens. Add to `components.h`:

```cpp
enum class TriggerAction {
    ActivateMover,     // Open a door, start a lift
    Teleport,          // Move the player somewhere
    Damage,            // Hurt the player (lava, spikes)
    Heal,              // Heal zone
    ChangeLevel,       // Load next level
    Message            // Display text to the player
};

struct TriggerVolume {
    TriggerAction action = TriggerAction::ActivateMover;
    entt::entity target = entt::null;  // Entity to activate (for ActivateMover)
    glm::vec3 destination;              // For teleport
    float value = 0.0f;                 // Damage/heal amount
    std::string message;                // For Message action
    bool onlyOnce = false;              // Fire once then disable
    bool triggered = false;             // Has been triggered (for onlyOnce)
    float cooldown = 0.0f;             // Minimum time between triggers
    float cooldownTimer = 0.0f;
};
```

Since `TriggerAction::Damage` and `TriggerAction::Heal` modify an entity's health, we also need a `Health` component:

```cpp
// Add to components.h

struct Health {
    float current;
    float max;
};
```

A trigger entity has `Position`, `AABBCollider` (with `isTrigger = true`), and `TriggerVolume`.

---

## The Trigger System

### src/engine/ecs/systems/trigger_system.h

```cpp
#pragma once

#include <entt/entt.hpp>

void triggerSystem(entt::registry& registry);
```

### src/engine/ecs/systems/trigger_system.cpp

```cpp
#include "engine/ecs/systems/trigger_system.h"
#include "engine/ecs/components.h"
#include "engine/physics/aabb.h"
#include "engine/physics/physics_config.h"

void triggerSystem(entt::registry& registry) {
    const auto& config = registry.ctx().get<PhysicsConfig>();
    float dt = config.fixedDeltaTime;
    auto triggerView = registry.view<Position, AABBCollider, TriggerVolume>();

    for (auto [trigEntity, trigPos, trigCol, trigger] : triggerView.each()) {
        // Skip disabled triggers
        if (trigger.onlyOnce && trigger.triggered) continue;

        // Cooldown
        if (trigger.cooldownTimer > 0.0f) {
            trigger.cooldownTimer -= dt;
            continue;
        }

        AABB triggerBox = AABB::fromCenterSize(trigPos.value, trigCol.halfExtents);

        // Only the player can activate triggers (for now)
        auto entityView = registry.view<Position, AABBCollider, TagPlayer>();

        for (auto [entity, entPos, entCol] : entityView.each()) {

            AABB entityBox = AABB::fromCenterSize(entPos.value, entCol.halfExtents);

            if (!triggerBox.intersects(entityBox)) continue;

            // ─── Trigger activated! ──────────────────────────────
            switch (trigger.action) {
                case TriggerAction::ActivateMover: {
                    if (trigger.target != entt::null &&
                        registry.valid(trigger.target) &&
                        registry.all_of<Mover>(trigger.target)) {
                        auto& mover = registry.get<Mover>(trigger.target);
                        if (mover.state == MoverState::Idle) {
                            mover.state = MoverState::Moving;
                        }
                    }
                    break;
                }

                case TriggerAction::Teleport: {
                    entPos.value = trigger.destination;
                    // Reset velocity to prevent flying out of the teleporter
                    if (registry.all_of<Velocity>(entity)) {
                        registry.get<Velocity>(entity).value = glm::vec3(0.0f);
                    }
                    break;
                }

                case TriggerAction::Damage: {
                    if (registry.all_of<Health>(entity)) {
                        registry.get<Health>(entity).current -= trigger.value * dt;
                    }
                    break;
                }

                case TriggerAction::Heal: {
                    if (registry.all_of<Health>(entity)) {
                        auto& health = registry.get<Health>(entity);
                        health.current = std::min(
                            health.current + trigger.value * dt, health.max);
                    }
                    break;
                }

                case TriggerAction::ChangeLevel: {
                    // Set a flag or event — the game layer handles level loading
                    // We won't implement this here yet
                    break;
                }

                case TriggerAction::Message: {
                    // TODO: Display trigger.message on HUD (Chapter 15)
                    break;
                }
            }

            // Mark as triggered and start cooldown
            trigger.triggered = true;
            trigger.cooldownTimer = trigger.cooldown;

            if (trigger.onlyOnce) break;
        }
    }
}
```

> **Why `TagPlayer` and not all colliders?**
>
> Without this filter, the trigger system checks *every* entity that has `Position` + `AABBCollider` — including physics demo cubes, static props, and anything else with a collider. A cube sliding past a door trigger would open it. Filtering by `TagPlayer` ensures only the player can activate triggers. In a future chapter we'll generalise this with collision layers or a `TagTriggerable` tag so NPCs and projectiles can interact with triggers too.

---

## The Mover System

This system handles the actual movement of doors, lifts, and any other moving geometry.

> **Mover vs Movement — what's the difference?**
>
> The existing `movementSystem` is a generic velocity integrator — it queries every entity with `Position` + `Velocity` and applies `pos += vel * dt` each frame. It drives the player, projectiles, falling objects, and anything else that moves via physics.
>
> The new `moverSystem` is completely different. It drives **scripted, state-machine-controlled motion** for doors and lifts. Instead of using `Velocity`, it interpolates a `Position` between two fixed points (`startPos` and `endPos`) based on a progress value and a 4-state cycle: Idle → Moving → Waiting → Returning. A door entity has a `Mover` component but no `Velocity` — it doesn't participate in physics, it just slides between two known positions.

### src/engine/ecs/systems/mover_system.h

```cpp
#pragma once

#include <entt/entt.hpp>

void moverSystem(entt::registry& registry);
```

### src/engine/ecs/systems/mover_system.cpp

```cpp
#include "engine/ecs/systems/mover_system.h"
#include "engine/ecs/components.h"
#include "engine/physics/physics_config.h"

void moverSystem(entt::registry& registry) {
    const auto& config = registry.ctx().get<PhysicsConfig>();
    float dt = config.fixedDeltaTime;
    auto view = registry.view<Position, Mover>();

    for (auto [entity, pos, mover] : view.each()) {
        switch (mover.state) {

            case MoverState::Idle:
                // Sitting at start position, waiting to be triggered
                break;

            case MoverState::Moving: {
                // Move toward end position
                float distance = glm::length(mover.endPos - mover.startPos);
                float step = (mover.speed / distance) * dt;
                mover.progress += step;

                if (mover.progress >= 1.0f) {
                    mover.progress = 1.0f;
                    mover.state = MoverState::Waiting;
                    mover.timer = mover.waitTime;
                }

                // Interpolate position
                pos.value = glm::mix(mover.startPos, mover.endPos, mover.progress);
                break;
            }

            case MoverState::Waiting: {
                // At end position, counting down
                mover.timer -= dt;

                if (mover.timer <= 0.0f) {
                    mover.state = MoverState::Returning;
                }
                break;
            }

            case MoverState::Returning: {
                // Move back to start
                float distance = glm::length(mover.endPos - mover.startPos);
                float step = (mover.speed / distance) * dt;
                mover.progress -= step;

                if (mover.progress <= 0.0f) {
                    mover.progress = 0.0f;
                    mover.state = MoverState::Idle;
                }

                pos.value = glm::mix(mover.startPos, mover.endPos, mover.progress);
                break;
            }
        }
    }
}
```

### `glm::mix` — Linear Interpolation

```cpp
glm::mix(a, b, t)  // Returns a + (b - a) * t
```

When `t = 0.0`, returns `a`. When `t = 1.0`, returns `b`. Values between smoothly blend. This is also called **lerp** (linear interpolation) — one of the most fundamental operations in game development.

---

## Creating a Player Entity

Up to now we've been using a fly camera that exists entirely outside the ECS — it has no `Position`, no collider, no tags. The trigger system needs something in the registry to detect, so we need a **player entity** that follows the camera around.

Add this to `src/engine/ecs/scene_setup.cpp` inside `setupScene()`:

```cpp
// ─── Player entity ──────────────────────────────────────────
// A minimal entity that represents the player in the ECS world.
// Its Position is synced to the camera each frame in main.cpp.
auto player = registry.create();
registry.emplace<Position>(player, glm::vec3(10.0f, 1.7f, 3.0f));
registry.emplace<AABBCollider>(player, glm::vec3(0.3f, 0.85f, 0.3f), false);
registry.emplace<TagPlayer>(player);
```

The collider half-extents `(0.3, 0.85, 0.3)` give the player a roughly human-sized box: 0.6 units wide, 1.7 units tall, 0.6 units deep.

### Syncing the Player to the Camera

The player entity needs to track the camera's position each frame. In `main.cpp`, after camera input and before the physics tick, sync them:

```cpp
// Sync player entity position to camera
auto playerView = registry.view<Position, TagPlayer>();
for (auto [entity, pos] : playerView.each()) {
    pos.value = camera.getPosition();
}
```

This is a one-way sync: the camera drives the player entity, not the other way around. In a future chapter when we add proper player physics (gravity, jumping, collision response), we'll reverse this — the physics system will drive the `Position` and the camera will read from it.

---

## Building a Door

A door is an entity with:
- `Position` (its current world position)
- `MeshRenderer` (so it's visible)
- `Mover` (defines start/end positions and behaviour)
- `AABBCollider` (so the player can't walk through it when closed)

Plus a trigger in front of the door.

We don't need a special door model — a cube mesh scaled to fill the doorway works perfectly. In the showcase level, the doorway between the Main Hall and the Physics Lab sits on the z=12 wall, spanning x=12 to x=16, with a lintel at y=3. That's a 4-unit wide, 3-unit tall opening.

A unit cube has half-extents of 0.5 in each axis. To fill the doorway we scale it to `(4.0, 3.0, 0.2)` — 4 wide (X), 3 tall (Y), and 0.2 deep (Z) so it looks like a thin slab. Its closed position sits centred in the opening, and its open position slides upward by 3 units so it disappears behind the lintel.

Add this entity creation code to `src/engine/ecs/scene_setup.cpp` inside `setupScene()`:

```cpp
// ─── Door: Main Hall → Physics Lab ──────────────────────────
// Doorway is at z=12, x=12..16, y=0..3 (lintel at y=3..4)
// Door is a cube scaled to fill the opening: 4 wide, 3 tall, 0.2 deep
auto door = registry.create();
glm::vec3 closedPos(14.0f, 1.5f, 12.0f); // centred in the doorway
glm::vec3 openPos(14.0f, 4.5f, 12.0f);   // slides up behind the lintel

registry.emplace<Position>(door, closedPos);
registry.emplace<Scale>(door, glm::vec3(4.0f, 3.0f, 0.2f));
registry.emplace<MeshRenderer>(door, cubeMesh->getVAO(), 0u,
                                litShader->getId(), gridGrey->getId(),
                                true, cubeMesh->getIndexCount());
registry.emplace<Mover>(door, closedPos, openPos, 3.0f, 4.0f, 0.0f, 0.0f,
                          MoverState::Idle, true);
registry.emplace<AABBCollider>(door, glm::vec3(2.0f, 1.5f, 0.1f), false);

// Trigger zone centred on the door (activates from both sides)
auto doorTrigger = registry.create();
registry.emplace<Position>(doorTrigger, glm::vec3(14.0f, 1.5f, 12.0f));
registry.emplace<AABBCollider>(doorTrigger, glm::vec3(2.5f, 1.5f, 2.0f), true);
registry.emplace<TriggerVolume>(doorTrigger,
    TriggerAction::ActivateMover,
    door,                      // target: the door entity
    glm::vec3(0.0f),           // destination (unused for mover)
    0.0f,                      // value (unused)
    "",                        // message (unused)
    false,                     // not once-only
    false,                     // not yet triggered
    1.0f,                      // 1 second cooldown between triggers
    0.0f                       // cooldown timer starts at 0
);
```

The door uses the same `cubeMesh` and `litShader` that the rest of the showcase level already loads — no new resources needed. The `Scale` component stretches the unit cube to fill the doorway, and the `AABBCollider` half-extents match so the player can't walk through it when closed.

The trigger zone is centred on the door at `z=12.0` rather than offset to one side. With a z half-extent of `2.0`, it extends 2 units into the Main Hall and 2 units into the Physics Lab, so the door opens when the player approaches from either direction.

When the player walks into the trigger zone, the door slides up behind the lintel. After 4 seconds, it slides back down.

### Debug Wireframe for Trigger Zones

Trigger zones are invisible — you can't tell where they are while testing. We can fix that by creating a visible debug cube at the trigger's position and rendering it in wireframe mode.

**Step 1: Add a tag to `components.h`**

```cpp
struct TagDebugWireframe {};
```

**Step 2: Create a debug entity for the trigger in `scene_setup.cpp`**

Place this right after the door trigger code above:

```cpp
// Debug wireframe cube showing the trigger zone
auto debugTrig = registry.create();
registry.emplace<Position>(debugTrig, glm::vec3(14.0f, 1.5f, 12.0f));
registry.emplace<Scale>(debugTrig, glm::vec3(5.0f, 3.0f, 4.0f));
registry.emplace<MeshRenderer>(debugTrig, cubeMesh->getVAO(), 0u,
                                litShader->getId(), gridGreen->getId(),
                                true, cubeMesh->getIndexCount());
registry.emplace<TagDebugWireframe>(debugTrig);
```

The `Position` matches the trigger entity. The `Scale` is the trigger's half-extents doubled — the unit cube goes from -0.5 to +0.5 (1 unit per axis), so a scale of 5.0 makes it 5 units wide, matching a half-extent of 2.5.

**Step 3: Switch to wireframe mode in `render_system.cpp`**

OpenGL has a function called `glPolygonMode` that controls how triangles are drawn. Normally it's set to `GL_FILL` — every pixel inside each triangle gets shaded. But you can switch it to `GL_LINE`, which tells OpenGL to only draw the edges of each triangle. The mesh geometry is identical — same vertices, same triangles — but only the outlines are visible.

In the render system's draw loop, wrap the draw call with a wireframe check:

```cpp
// Switch to wireframe if this is a debug entity
bool wireframe = registry.all_of<TagDebugWireframe>(entity);
if (wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

glBindVertexArray(mesh.vao);

if (mesh.useIndices)
{
    glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, 0);
}
else
{
    glDrawArrays(GL_TRIANGLES, 0, mesh.vertexCount);
}

// Switch back to filled rendering for the next entity
if (wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
```

The key insight: `glPolygonMode` is **global OpenGL state**. Once you set it to `GL_LINE`, *everything* after it draws as wireframe until you set it back to `GL_FILL`. That's why we check the tag before drawing and restore it immediately after — we only want the debug entities to be wireframe, not the whole scene.

**Step 4: Add a flat colour override to the lit shader**

The wireframe works, but the lines are coloured by the full lighting calculation — they'll look dark and muddy in shadowed areas. For debug visualisation we want a bright, flat colour that's always visible regardless of lighting.

Add a new uniform to `assets/shaders/lit.frag`, near the other uniforms:

```glsl
uniform vec4 colorOverride;  // (0,0,0,0) = normal lighting, anything else = flat colour
```

Then replace the final line of `main()`:

```glsl
// Replace:
//   FragColor = vec4(result, 1.0);
// With:
if (colorOverride.a > 0.0)
    FragColor = colorOverride;
else
    FragColor = vec4(result, 1.0);
```

When `colorOverride.a` is 0 (the default for uninitialised uniforms), the shader works exactly as before. When it's non-zero, the shader skips all lighting and outputs the flat colour directly.

**Step 5: Set the override in `render_system.cpp`**

In the render system, set the uniform before each draw call — right before the wireframe check:

```cpp
// Flat colour override for debug wireframes
loc = glGetUniformLocation(mesh.shaderId, "colorOverride");
if (registry.all_of<TagDebugWireframe>(entity))
    glUniform4f(loc, 0.0f, 1.0f, 0.0f, 1.0f);  // bright green
else
    glUniform4f(loc, 0.0f, 0.0f, 0.0f, 0.0f);   // disabled (normal lighting)
```

Now your trigger zones show up as bright green wireframe boxes, clearly visible against any background. You can change the colour per-entity by using different RGB values — `(1,0,0,1)` for red, `(0,0,1,1)` for blue, etc.

---

## Building a Lift

A lift is a floor surface that carries the player up or down. Same Mover component, different axis.

We don't need a special platform model — a cube scaled flat works fine. A scale of `(3.0, 0.2, 3.0)` gives a 3-unit wide, 0.2-unit thick platform. Also in `setupScene()`:

```cpp
auto lift = registry.create();
glm::vec3 bottomPos(0.0f, 0.0f, -8.0f);
glm::vec3 topPos(0.0f, 6.0f, -8.0f);

registry.emplace<Position>(lift, bottomPos);
registry.emplace<Scale>(lift, glm::vec3(3.0f, 0.2f, 3.0f));
registry.emplace<MeshRenderer>(lift, cubeMesh->getVAO(), 0u,
                                litShader->getId(), gridGreen->getId(),
                                true, cubeMesh->getIndexCount());
registry.emplace<Mover>(lift, bottomPos, topPos, 2.0f, 2.0f, 0.0f, 0.0f,
                          MoverState::Idle, true);
registry.emplace<AABBCollider>(lift, glm::vec3(1.5f, 0.1f, 1.5f), false);

// Trigger on the lift platform itself (step on it to activate)
auto liftTrigger = registry.create();
registry.emplace<Position>(liftTrigger, glm::vec3(0.0f, 0.3f, -8.0f));
registry.emplace<AABBCollider>(liftTrigger, glm::vec3(1.5f, 0.3f, 1.5f), true);
registry.emplace<TriggerVolume>(liftTrigger,
    TriggerAction::ActivateMover, lift,
    glm::vec3(0.0f), 0.0f, "", false, false, 0.5f, 0.0f);

// Debug wireframe cube showing the lift trigger zone
auto debugLift = registry.create();
registry.emplace<Position>(debugLift, glm::vec3(0.0f, 0.3f, -8.0f));
registry.emplace<Scale>(debugLift, glm::vec3(3.0f, 0.6f, 3.0f));
registry.emplace<MeshRenderer>(debugLift, cubeMesh->getVAO(), 0u,
                                litShader->getId(), gridGreen->getId(),
                                true, cubeMesh->getIndexCount());
registry.emplace<TagDebugWireframe>(debugLift);
```

### Carrying the Player

When a lift moves, the player standing on it needs to move too. There are two approaches:

**Approach A: Parent the player to the lift** — track which entity the player is standing on and add the lift's movement delta to the player each frame.

**Approach B: Let physics handle it** — the lift's collider moves upward, pushing the player up via collision response.

Approach B is simpler and works with our existing collision system. The player stands on the lift's AABB; when the lift moves up, the collision system prevents the player from falling through, effectively carrying them.

---

## A Teleporter

```cpp
auto teleportTrigger = registry.create();
registry.emplace<Position>(teleportTrigger, glm::vec3(8.0f, 0.5f, 3.0f));
registry.emplace<AABBCollider>(teleportTrigger, glm::vec3(1.0f, 1.5f, 1.0f), true);
registry.emplace<TriggerVolume>(teleportTrigger,
    TriggerAction::Teleport,
    entt::null,                         // no target entity
    glm::vec3(-8.0f, 1.0f, -3.0f),    // destination
    0.0f, "", false, false, 1.0f, 0.0f);

// Debug wireframe cube showing the teleporter trigger zone
auto debugTeleport = registry.create();
registry.emplace<Position>(debugTeleport, glm::vec3(8.0f, 0.5f, 3.0f));
registry.emplace<Scale>(debugTeleport, glm::vec3(2.0f, 3.0f, 2.0f));
registry.emplace<MeshRenderer>(debugTeleport, cubeMesh->getVAO(), 0u,
                                litShader->getId(), gridGreen->getId(),
                                true, cubeMesh->getIndexCount());
registry.emplace<TagDebugWireframe>(debugTeleport);
```

Walk into it and you're instantly at the destination.

### Making Teleportation Move the Camera

There's a subtle problem. The trigger system sets `entPos.value` to the destination, but remember — in `main.cpp` we sync the camera position *to* the player entity every frame:

```cpp
pos.value = camera.getPosition();  // camera overwrites player position!
```

So the teleporter sets the player's Position, but next frame the camera (which hasn't moved) overwrites it right back. We need a **two-way sync**: camera drives the player normally, but if something else moves the player (like a teleporter), the camera should follow.

First, add a `setPosition` method to `Camera` in `src/engine/renderer/camera.h`:

```cpp
glm::vec3 getPosition() const { return m_position; }
void setPosition(const glm::vec3& pos) { m_position = pos; }
```

Then in `main.cpp`, after the physics tick loop, sync the player's position back to the camera:

```cpp
// After the fixedTimestep while loop:

// Sync player position back to camera (handles teleportation)
for (auto [entity, pos] : playerView.each()) {
    if (pos.value != camera.getPosition()) {
        camera.setPosition(pos.value);
    }
}
```

The `if` check avoids unnecessary writes — it only fires when something in the ECS has moved the player to a different position than where the camera currently sits. Without this, the teleporter would set the position, the physics loop would finish, and then the next frame's `pos.value = camera.getPosition()` would undo it before the camera ever knew.

---

## A Lava Pool (Damage Zone)

The lava pool needs two things: a trigger volume for damage, and a visible red surface so the player can see the danger.

```cpp
// Visible lava surface — a flat red cube
auto lavaSurface = registry.create();
registry.emplace<Position>(lavaSurface, glm::vec3(0.0f, -0.5f, 10.0f));
registry.emplace<Scale>(lavaSurface, glm::vec3(10.0f, 0.2f, 10.0f));
registry.emplace<MeshRenderer>(lavaSurface, cubeMesh->getVAO(), 0u,
                                litShader->getId(), gridRed->getId(),
                                true, cubeMesh->getIndexCount());

// Damage trigger — same position, slightly taller so it catches the player above the surface
auto lava = registry.create();
registry.emplace<Position>(lava, glm::vec3(0.0f, -0.5f, 10.0f));
registry.emplace<AABBCollider>(lava, glm::vec3(5.0f, 0.5f, 5.0f), true);
registry.emplace<TriggerVolume>(lava,
    TriggerAction::Damage,
    entt::null,
    glm::vec3(0.0f),
    25.0f,      // 25 damage per second
    "", false, false, 0.0f, 0.0f);  // No cooldown — continuous damage

// Debug wireframe cube showing the damage zone (red to signal danger)
auto debugLava = registry.create();
registry.emplace<Position>(debugLava, glm::vec3(0.0f, -0.5f, 10.0f));
registry.emplace<Scale>(debugLava, glm::vec3(10.0f, 1.0f, 10.0f));
registry.emplace<MeshRenderer>(debugLava, cubeMesh->getVAO(), 0u,
                                litShader->getId(), gridRed->getId(),
                                true, cubeMesh->getIndexCount());
registry.emplace<TagDebugWireframe>(debugLava);
```

The `lavaSurface` is a flat red cube (10 wide, 0.2 tall, 10 deep) that makes the lava visible. The debug wireframe is taller (1.0) to show the full trigger volume that deals damage — the player takes damage anywhere inside the wireframe, not just when touching the surface.

---

## Fixing Ground Detection

If you test the showcase level at this point, you'll notice objects behaving strangely near surfaces — cubes slowly sinking through platforms, or floating just above the floor before snapping into place. The `groundDetectionSystem` from Chapter 10 has two problems that become visible once we start stacking objects on top of each other.

### Problem 1: Objects slowly sink through entity colliders

The ground detection system only raycasts against **level surfaces** (sector geometry). It doesn't check entity colliders at all. A cube sitting on a shelf has `OnGround = false`, so gravity keeps pulling it down. The collision sweep blocks the velocity each frame, but can't fully prevent drift — the cube slowly sinks through.

We can't just add a raycast for entity colliders, because raycasting fails when the ray origin is exactly on or slightly inside the target AABB (common when objects are stacked). Instead, we use a **proximity + overlap check**: if the entity's feet are within `probeDistance` of another entity's top face, and they overlap horizontally, that counts as ground.

### Problem 2: Objects float above level surfaces

Even for level geometry, the original ground detection only sets `OnGround = true` — it doesn't correct the position. The collision sweep blocks velocity when approaching a surface, but doesn't push the entity to the exact resting point. This creates a "Zeno's paradox" effect: each frame the object gets slightly closer but never quite arrives, producing a visible slow-fall before landing.

The fix: when ground is detected, **snap** the entity's Y position so its feet sit exactly on the surface, and **kill downward velocity** so gravity doesn't immediately fight the snap.

### Updated `groundDetectionSystem`

In `src/engine/ecs/systems/physics_system.cpp`, update the `groundDetectionSystem` function:

```cpp
void groundDetectionSystem(entt::registry& registry, const Level& level)
{
    auto view = registry.view<Position, AABBCollider, OnGround>();

    for (auto [entity, pos, col, ground] : view.each())
    {
        // Cast a short ray downward from the bottom of the collider
        glm::vec3 feetPos = pos.value - glm::vec3(0.0f, col.halfExtents.y, 0.0f);
        float probeDistance = 0.1f;

        Ray downRay;
        downRay.origin = feetPos;
        downRay.direction = glm::vec3(0.0f, -1.0f, 0.0f);

        ground.value = false;

        // Check against level geometry
        for (const auto& sector : level.sectors)
        {
            for (const auto& surface : sector.surfaces)
            {
                if (surface.normal.y < 0.7f) continue;

                // The actual surface Y (before slab inflation)
                float surfaceY = std::max({
                    surface.vertices[0].y, surface.vertices[1].y,
                    surface.vertices[2].y, surface.vertices[3].y
                });

                AABB surfBox;
                surfBox.min = glm::min(
                    glm::min(surface.vertices[0], surface.vertices[1]),
                    glm::min(surface.vertices[2], surface.vertices[3]));
                surfBox.max = glm::max(
                    glm::max(surface.vertices[0], surface.vertices[1]),
                    glm::max(surface.vertices[2], surface.vertices[3]));
                surfBox.min.y -= 0.05f;
                surfBox.max.y += 0.05f;

                auto hit = rayIntersectionsAABB(downRay, surfBox);
                if (hit.has_value() && hit.value() <= probeDistance)
                {
                    // Snap to sit exactly on the surface
                    pos.value.y = surfaceY + col.halfExtents.y;
                    ground.value = true;
                    break;
                }
            }
            if (ground.value) break;
        }

        // Check against entity colliders (e.g. shelves, platforms)
        // Uses proximity + overlap instead of raycasting, because a
        // downward ray fails when the origin is exactly on or slightly
        // inside the other AABB (common with stacked objects).
        if (!ground.value)
        {
            AABB entityBox = AABB::fromCentreSize(pos.value, col.halfExtents);
            float feetY = entityBox.min.y;

            auto colliders = registry.view<Position, AABBCollider>();
            for (auto [other, otherPos, otherCol] : colliders.each())
            {
                if (other == entity) continue;
                if (otherCol.isTrigger) continue;

                AABB otherBox = AABB::fromCentreSize(otherPos.value,
                                                      otherCol.halfExtents);
                float otherTopY = otherBox.max.y;

                // Feet within probeDistance of the other entity's top face,
                // and horizontally overlapping
                if (feetY >= otherTopY - probeDistance &&
                    feetY <= otherTopY + probeDistance &&
                    entityBox.max.x > otherBox.min.x &&
                    entityBox.min.x < otherBox.max.x &&
                    entityBox.max.z > otherBox.min.z &&
                    entityBox.min.z < otherBox.max.z)
                {
                    // Snap to sit exactly on top
                    pos.value.y = otherTopY + col.halfExtents.y;
                    ground.value = true;
                    break;
                }
            }
        }

        // Kill downward velocity when grounded — prevents gravity
        // from fighting the position snap on the next frame
        if (ground.value && registry.all_of<Velocity>(entity))
        {
            auto& vel = registry.get<Velocity>(entity);
            if (vel.value.y < 0.0f) vel.value.y = 0.0f;
        }
    }
}
```

The three key additions:

1. **Surface Y snap** — when ground is detected on a level surface, the entity is snapped so its feet sit exactly on the real surface Y (not the inflated slab used for raycast detection).
2. **Entity collider ground detection** — instead of raycasting (which fails at boundaries), checks whether the entity's feet are within `probeDistance` of another entity's top face with horizontal overlap.
3. **Velocity kill** — zeroes any downward velocity when grounded, breaking the gravity-vs-sweep cycle that caused the slow-fall effect.

---

## Lambda Functions (Preview)

As your trigger system grows, you might want arbitrary behaviour:

```cpp
// C++ Concept: Lambda functions
auto myAction = [](entt::registry& reg, entt::entity triggerer) {
    // Do whatever you want
    if (reg.all_of<Health>(triggerer)) {
        reg.get<Health>(triggerer).current -= 10.0f;
    }
};

// Lambdas are anonymous functions you can store and call later
myAction(registry, playerEntity);
```

### C++ Concept: `std::function`

To store a lambda in a component:

```cpp
#include <functional>

struct CustomTrigger {
    std::function<void(entt::registry&, entt::entity)> callback;
};
```

`std::function` is a general-purpose wrapper that can hold any callable: lambdas, function pointers, functors. It has some overhead (heap allocation for large lambdas), but it's the easiest way to store arbitrary behaviour.

We won't use this approach heavily — it goes against the "data-only components" philosophy of ECS. But for special one-off level scripting, it's pragmatic.

---

## Updated Tick Order

```
1.  InputSystem
2.  CharacterMovementSystem
3.  HandleJump
4.  PhysicsSystem
5.  MoverSystem             ← NEW: update doors, lifts
6.  CollisionSystem
7.  MovementSystem
8.  GroundDetectionSystem
9.  TriggerSystem           ← NEW: detect trigger overlaps
10. DeathSystem
11. RenderSystem
```

The mover system runs before collision so that moving platforms push the player correctly. The trigger system runs after movement so it detects the player's final position.

---

## What's Next

In **Chapter 12**, we'll add weapons and projectiles — hitscan guns, rockets, damage dealing, and the foundation of FPS combat.
