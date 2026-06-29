# Chapter 10a: Game Loop & Physics Cleanup

> **Prerequisites:** Chapter 10 (Physics & Movement) completed. You should have a working game loop with a fixed timestep, gravity, friction, collision detection, and ground detection.

---

## Why This Chapter?

If you have been following along, your `main.cpp` is starting to groan under its own weight. The fixed timestep logic is a chunk of boilerplate sitting right in your game loop. Terminal velocity is `-50.0f` buried inside `physicsSystem`. The collision layers we previewed in Chapter 10 have not been implemented yet. And the order your systems run in? That is just "whatever order you happened to type the function calls."

None of this is *broken*. It all works. But it is the kind of code that quietly rots. You change a physics constant in one place and forget the other. You reorder a system call and spend an hour debugging why physics feels wrong. You come back in two weeks and cannot remember why `groundDetectionSystem` must run after `movementSystem`.

This chapter is a **cleanup pass** -- no new features, just organising what we have. We will:

1. **FixedTimestep** -- wrap the accumulator pattern in a reusable class
2. **PhysicsConfig** -- centralise magic numbers into a registry context variable
3. **CollisionLayers** -- implement the bitmask constants previewed in Chapter 10
4. **System phase ordering** -- document and formalise the execution order
5. **Multiple point lights** -- upgrade the renderer from one point light to many
6. **Test, then remove test entities** -- verify everything works before cleaning up

By the end, your game loop will be shorter, your physics parameters will live in one place, your lighting will actually work with multiple sources, and the next person to read your code (including future you) will thank you.

---

## The Problem: Before

Here is what your `main.cpp` game loop looks like after Chapter 10:

```cpp
// main.cpp - after Chapter 10

#include "engine/core/window.h"
#include "engine/core/input_manager.h"
#include "engine/core/resource_manager.h"
#include "engine/ecs/components.h"
#include "engine/ecs/scene_setup.h"
#include "engine/ecs/systems/render_system.h"
#include "engine/ecs/systems/collision_system.h"
#include "engine/ecs/systems/movement_system.h"
#include "engine/ecs/systems/physics_system.h"
#include "engine/physics/spatial_hash.h"
#include "engine/renderer/camera.h"

#include <entt/entt.hpp>

int main()
{
    Window window(1280, 720, "QEngine");

    InputManager input;
    input.init(window.getHandle());

    ResourceManager resources;

    // ... resource loading ...

    Camera camera(glm::vec3(0.0f, 1.7f, 3.0f));

    entt::registry registry;
    Level level = setupScene(registry, resources);
    SpatialHash spatialHash(4.0f);

    // Fixed timestep variables scattered in main
    constexpr float FIXED_TIMESTEP = 1.0f / 60.0f;
    float accumulator = 0.0f;
    float lastFrame = 0.0f;

    glEnable(GL_DEPTH_TEST);

    while (!window.shouldClose())
    {
        float currentFrame = (float)glfwGetTime();
        float frameTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        if (frameTime > 0.25f) frameTime = 0.25f;

        accumulator += frameTime;

        input.update();
        window.pollEvents();

        // ... camera input ...

        while (accumulator >= FIXED_TIMESTEP)
        {
            physicsSystem(registry, FIXED_TIMESTEP);
            collisionSystem(registry, spatialHash, level, FIXED_TIMESTEP);
            movementSystem(registry, FIXED_TIMESTEP);
            groundDetectionSystem(registry, level);

            accumulator -= FIXED_TIMESTEP;
        }

        // ... render ...

        window.swapBuffers();
    }

    resources.clear();
    return 0;
}
```

Count the problems:

- **The accumulator pattern** is ~15 lines of boilerplate that will be identical in every project. It has nothing to do with *your* game.
- **Magic numbers** are scattered: terminal velocity (`-50.0f`) is buried inside `physicsSystem`, the timestep is a `constexpr` in main, the spiral-of-death cap is `0.25f`.
- **No collision layers** yet. Chapter 10 introduced the concept but we have not applied it. Every entity collides with everything.
- **System ordering** is implicit. There is no documentation for *why* `physicsSystem` comes before `collisionSystem`, or why `groundDetectionSystem` must be last in the physics tick.
- **Only one point light works.** The render system `break`s after the first point light it finds. The `PointLight` component has `linear` and `quadratic` attenuation fields, but the shader ignores them and hardcodes `0.09` and `0.032`. Place six point lights in a scene and only one will render.
- **Test entities** (`cube` and `cube2`) are still in `scene_setup.cpp`. They were useful for testing but are no longer needed.

Let us fix each of these.

---

## 1. The FixedTimestep Class

### The Concept: Accumulator Pattern

The fixed timestep accumulator is a well-known pattern in game development. The idea is simple: real time marches forward in irregular chunks (your frame delta time), but physics needs to advance in fixed, deterministic steps. The accumulator bridges the gap -- it collects real time and dispenses it in fixed-size portions.

The three operations are always the same:

1. **Accumulate** -- add the frame's delta time to the accumulator (with a clamp to prevent the "spiral of death")
2. **Step** -- while the accumulator holds at least one fixed step's worth of time, consume it and run physics
3. **Get alpha** -- after stepping, the leftover fraction tells the renderer how far between the previous and current physics state we are, enabling interpolation for smooth visuals

This pattern never changes between projects. It is a perfect candidate for extraction into a class.

### The Code

Create `src/engine/core/fixed_timestep.h`:

```cpp
// engine/core/fixed_timestep.h
#pragma once

class FixedTimestep
{
public:
	explicit FixedTimestep(float timestep = 1.0f / 60.0f, float maxFrameTime = 0.25f)
		: m_timestep(timestep)
		, m_maxFrameTime(maxFrameTime)
		, m_accumulator(0.0f)
		, m_frameTime(0.0f)
		, m_lastTime(0.0f)
	{
	}

	// Call once per frame, at the top of the game loop.
	// Pass in the current time (e.g. from glfwGetTime()).
	void accumulate(float currentTime)
	{
		m_frameTime = currentTime - m_lastTime;
		m_lastTime = currentTime;

		// Clamp to prevent spiral of death.
		if (m_frameTime > m_maxFrameTime)
			m_frameTime = m_maxFrameTime;

		m_accumulator += m_frameTime;
	}

	// Call in a while loop. Returns true if there is enough accumulated time
	// for one fixed step, and consumes that step's worth of time.
	bool step()
	{
		if (m_accumulator >= m_timestep)
		{
			m_accumulator -= m_timestep;
			return true;
		}
		return false;
	}

	// Returns the interpolation factor (0.0 to 1.0) representing how far
	// between the previous and current physics states we are.
	// Use this to interpolate positions for smooth rendering.
	float getAlpha() const
	{
		return m_accumulator / m_timestep;
	}

	// The frame time from the most recent accumulate() call.
	// Use this for frame-rate-dependent things like camera input.
	float getFrameTime() const { return m_frameTime; }

	// Getters for the configuration values.
	float getTimestep() const { return m_timestep; }

private:
	float m_timestep;
	float m_maxFrameTime;
	float m_accumulator;
	float m_frameTime;
	float m_lastTime;
};
```

### Why This Design?

**Why a class and not free functions?** The accumulator and timing state must persist across frames. A class is the natural home for persistent state with a small interface.

**Why `explicit` on the constructor?** This prevents accidental implicit conversion from a `float` to a `FixedTimestep`. It is a good habit for any single-argument constructor.

**Why pass `currentTime` to `accumulate()`?** We are already using `glfwGetTime()` for timing. Rather than switching to `std::chrono` and introducing a different time source, we keep things consistent by passing in the GLFW time. This also makes the class testable -- you can feed it artificial time values.

**Why is `step()` a boolean and not a callback?** Keeping the loop in the caller's code means you can see exactly what runs at fixed rate. A callback-based API hides the loop body behind a `std::function` and makes debugging harder. Simplicity wins.

**What is the "spiral of death"?** If your physics step takes longer than the fixed timestep to compute, each frame adds more time to the accumulator than it drains. The accumulator grows without bound, and the game freezes as it tries to simulate an ever-increasing number of steps. The `maxFrameTime` clamp prevents this -- physics will slow down instead of locking up. 0.25 seconds (4 FPS equivalent) is a common clamp value.

---

## 2. The PhysicsConfig Struct

### The Concept: Registry Context

EnTT's registry has a feature called **context variables** -- arbitrary data you can attach to the registry itself rather than to any entity. This is perfect for global configuration that many systems need to read.

```cpp
// Emplacing a context variable (do this once, during setup)
registry.ctx().emplace<PhysicsConfig>();

// Accessing it from any system (read-only)
const auto& config = registry.ctx().get<PhysicsConfig>();

// Accessing it for modification
auto& config = registry.ctx().get<PhysicsConfig>();
config.terminalVelocity = 30.0f; // slower fall speed
```

Context variables are:

- **Singleton by type** -- there is exactly one `PhysicsConfig` in the registry
- **Accessed by type** -- no string keys, no IDs, just `get<T>()`
- **Lifetime-managed by the registry** -- they are destroyed when the registry is destroyed

### The Code

Our per-entity physics values already live in components (`Gravity`, `CharacterPhysics`). PhysicsConfig is for truly **global** values that don't vary per entity.

Create `src/engine/physics/physics_config.h`:

```cpp
// engine/physics/physics_config.h
#pragma once

struct PhysicsConfig
{
	// Maximum fall speed (units per second, positive value).
	// Currently a magic number (-50.0f) inside physicsSystem.
	float terminalVelocity = 50.0f;

	// Fixed physics timestep (seconds per tick).
	// Stored here so systems can read it from context instead of
	// receiving it as a parameter.
	float fixedDeltaTime = 1.0f / 60.0f;
};
```

### Why This Design?

**Why so few fields?** Per-entity physics values (gravity strength, friction, acceleration, jump force) already live where they belong -- in the `Gravity` and `CharacterPhysics` components. PhysicsConfig only holds values that are truly global to the simulation and don't vary per entity.

**Why a struct and not a class?** All members are public data with sensible defaults. There is no invariant to protect, no complex construction logic. A plain struct with aggregate initialisation is the right tool.

**Why default member initialisers?** They serve double duty: they document the expected value, and they let you construct a `PhysicsConfig` with zero arguments and get working defaults.

**Why not `constexpr`?** These values will likely be loaded from a config file or tweaked at runtime (think debug sliders). Making them `constexpr` would prevent that.

### Updating Your Systems

Open `src/engine/ecs/systems/physics_system.cpp`. Here is how `physicsSystem` changes.

Before:

```cpp
// src/engine/ecs/systems/physics_system.cpp (before)

void physicsSystem(entt::registry& registry, float dt)
{
    auto gravityView = registry.view<Velocity, Gravity, OnGround>();
    for (auto [entity, vel, grav, ground] : gravityView.each())
    {
        if (!ground.value)
        {
            vel.value.y -= grav.strength * dt;

            // Terminal velocity (cap fall speed)
            if (vel.value.y < -50.0f)     // magic number!
            {
                vel.value.y = -50.0f;
            }
        }
    }
    // ... friction code ...
}
```

After:

```cpp
// src/engine/ecs/systems/physics_system.cpp (after)

#include "engine/physics/physics_config.h"  // ← add this include

void physicsSystem(entt::registry& registry)
{
    const auto& config = registry.ctx().get<PhysicsConfig>();

    auto gravityView = registry.view<Velocity, Gravity, OnGround>();
    for (auto [entity, vel, grav, ground] : gravityView.each())
    {
        if (!ground.value)
        {
            vel.value.y -= grav.strength * config.fixedDeltaTime;

            if (vel.value.y < -config.terminalVelocity)
            {
                vel.value.y = -config.terminalVelocity;
            }
        }
    }
    // ... friction code (also replace dt with config.fixedDeltaTime) ...
}
```

Notice the system no longer takes `dt` as a parameter. It reads the timestep and terminal velocity from the registry context. Now apply the same treatment to `movementSystem` and `collisionSystem`.

### Movement System

`src/engine/ecs/systems/movement_system.cpp`

Before:

```cpp
// src/engine/ecs/systems/movement_system.cpp (before)

#include "engine/ecs/systems/movement_system.h"
#include "engine/ecs/components.h"

void movementSystem(entt::registry& registry, float dt)
{
	auto view = registry.view<Position, Velocity>();

	for (auto [entity, pos, vel] : view.each())
	{
		pos.value += vel.value * dt;
	}
}
```

After:

```cpp
// src/engine/ecs/systems/movement_system.cpp (after)

#include "engine/ecs/systems/movement_system.h"
#include "engine/ecs/components.h"
#include "engine/physics/physics_config.h"  // ← add this include

void movementSystem(entt::registry& registry)
{
	const auto& config = registry.ctx().get<PhysicsConfig>();

	auto view = registry.view<Position, Velocity>();

	for (auto [entity, pos, vel] : view.each())
	{
		pos.value += vel.value * config.fixedDeltaTime;
	}
}
```

Update the header to match:

Before:

```cpp
// src/engine/ecs/systems/movement_system.h (before)
#pragma once

#include <entt/entt.hpp>

void movementSystem(entt::registry& registry, float dt);
```

After:

```cpp
// src/engine/ecs/systems/movement_system.h (after)
#pragma once

#include <entt/entt.hpp>

void movementSystem(entt::registry& registry);
```

### Collision System

`src/engine/ecs/systems/collision_system.cpp` — the only change is the signature and how `movement` is calculated. The rest of the function stays the same.

Before:

```cpp
// src/engine/ecs/systems/collision_system.cpp (before — just the top of the function)

#include "engine/ecs/systems/collision_system.h"
#include "engine/ecs/components.h"
#include "engine/physics/aabb.h"
#include "engine/physics/collision.h"

void collisionSystem
(
	entt::registry& registry,
	SpatialHash& spatialHash,
	const Level& level,
	float dt
)
{
	// rebuild the spatial hash each frame
	spatialHash.clear();
	// ... insert entities into spatial hash ...

	auto movers = registry.view<Position, Velocity, AABBCollider>();

	for (auto [entity, pos, vel, col] : movers.each())
	{
		glm::vec3 movement = vel.value * dt;
		// ... rest of sweep logic unchanged ...
	}
}
```

After:

```cpp
// src/engine/ecs/systems/collision_system.cpp (after — just the top of the function)

#include "engine/ecs/systems/collision_system.h"
#include "engine/ecs/components.h"
#include "engine/physics/aabb.h"
#include "engine/physics/collision.h"
#include "engine/physics/physics_config.h"  // ← add this include

void collisionSystem
(
	entt::registry& registry,
	SpatialHash& spatialHash,
	const Level& level
)
{
	const auto& config = registry.ctx().get<PhysicsConfig>();

	// rebuild the spatial hash each frame
	spatialHash.clear();
	// ... insert entities into spatial hash ...

	auto movers = registry.view<Position, Velocity, AABBCollider>();

	for (auto [entity, pos, vel, col] : movers.each())
	{
		glm::vec3 movement = vel.value * config.fixedDeltaTime;
		// ... rest of sweep logic unchanged ...
	}
}
```

Update the header to match:

Before:

```cpp
// src/engine/ecs/systems/collision_system.h (before)
#pragma once

#include <entt/entt.hpp>
#include "engine/physics/spatial_hash.h"
#include "engine/level/level.h"

void collisionSystem
(
	entt::registry& registry,
	SpatialHash& spatialHash,
	const Level& level,
	float dt
);
```

After:

```cpp
// src/engine/ecs/systems/collision_system.h (after)
#pragma once

#include <entt/entt.hpp>
#include "engine/physics/spatial_hash.h"
#include "engine/level/level.h"

void collisionSystem
(
	entt::registry& registry,
	SpatialHash& spatialHash,
	const Level& level
);
```

### Physics System Header

Update `src/engine/ecs/systems/physics_system.h` to reflect the new signature (shown earlier in the `.cpp` before/after):

Before:

```cpp
// src/engine/ecs/systems/physics_system.h (before)
#pragma once

#include <entt/entt.hpp>

struct Level;

void physicsSystem(entt::registry& registry, float dt);
void groundDetectionSystem(entt::registry& registry, const Level& level);
```

After:

```cpp
// src/engine/ecs/systems/physics_system.h (after)
#pragma once

#include <entt/entt.hpp>

struct Level;

void physicsSystem(entt::registry& registry);
void groundDetectionSystem(entt::registry& registry, const Level& level);
```

Note that `groundDetectionSystem` keeps its `Level` parameter and `collisionSystem` keeps its `SpatialHash&` and `Level&` parameters — they need those objects directly, not from the registry context. The only parameter removed from each system is `float dt`.

---

## 3. The CollisionLayers Namespace

### The Concept: `constexpr` Bitmask Constants

In Chapter 10, we previewed collision layers as an illustrative concept. Now we implement them properly.

Collision layers are a classic bitmask use case. Each layer is a single bit, and an entity's collision mask is the bitwise OR of all layers it interacts with. The raw hex values are fine for the computer, but terrible for humans.

```cpp
// What does this mean? You tell me.
collider.mask = 0x03;

// Much better.
collider.mask = CollisionLayers::Player | CollisionLayers::World;
```

### The Code

Create `src/engine/physics/collision_layers.h`:

```cpp
// engine/physics/collision_layers.h
#pragma once

#include <cstdint>

namespace CollisionLayers
{
	// Each layer is a single bit. Use bitwise OR to combine.
	// An entity collides with another if (a.layer & b.mask) != 0.

	constexpr uint32_t None       = 0x00;
	constexpr uint32_t Player     = 0x01;
	constexpr uint32_t World      = 0x02;
	constexpr uint32_t Enemy      = 0x04;
	constexpr uint32_t Projectile = 0x08;
	constexpr uint32_t Trigger    = 0x10;

	// Common combined masks for convenience.
	constexpr uint32_t All        = 0xFFFFFFFF;
	constexpr uint32_t Solid      = Player | World | Enemy;
	constexpr uint32_t Shootable  = Enemy | World;
}
```

### Why This Design?

**Why a namespace and not an `enum class`?** An `enum class` would require a `static_cast` every time you do bitwise operations, because `enum class` deliberately prevents implicit conversion to integer. That makes the calling code ugly:

```cpp
// With enum class -- verbose and noisy
collider.mask = static_cast<uint32_t>(CollisionLayer::Player)
              | static_cast<uint32_t>(CollisionLayer::World);

// With namespace constants -- clean and readable
collider.mask = CollisionLayers::Player | CollisionLayers::World;
```

**Why `constexpr`?** These values are compile-time constants. `constexpr` tells the compiler they can be used in constant expressions (template arguments, array sizes, `switch` cases) and guarantees zero runtime cost.

**Why combined masks?** `Solid` and `Shootable` encode common collision rules that would otherwise be duplicated everywhere. If you add a new solid layer, you update `Solid` in one place.

### Updating AABBCollider

Add `layer` and `mask` fields to your `AABBCollider` component in `src/engine/ecs/components.h`:

```cpp
#include "engine/physics/collision_layers.h"

struct AABBCollider
{
    glm::vec3 halfExtents = glm::vec3(0.5f);
    bool isTrigger = false;
    uint32_t layer = CollisionLayers::World;     // what layer am I on?
    uint32_t mask  = CollisionLayers::All;        // what layers do I collide with?
};
```

Then update `src/engine/ecs/systems/collision_system.cpp` to use these fields. There are two places where collision is checked — entity-vs-level and entity-vs-entity. The layer check only applies to the **entity-vs-entity** path, since level geometry is always solid.

Find the entity-vs-entity section inside the sector loop. Before:

```cpp
// check against other entities
auto nearby = spatialHash.query(pos.value, col.halfExtents + glm::vec3(2.0f));
for (auto other : nearby)
{
    if (other == entity) continue;
    if (!registry.all_of<Position, AABBCollider>(other)) continue;

    auto& otherPos = registry.get<Position>(other);
    auto& otherCol = registry.get<AABBCollider>(other);
    AABB otherBox = AABB::fromCentreSize(otherPos.value, otherCol.halfExtents);

    // if it's a trigger, don't resolve - just detect
    if (otherCol.isTrigger)
    {
        // ...
        continue;
    }

    SweepResult hit = sweepAABB(entityBox, movement, otherBox);
    // ...
}
```

After — add the layer check right after the trigger check:

```cpp
// check against other entities
auto nearby = spatialHash.query(pos.value, col.halfExtents + glm::vec3(2.0f));
for (auto other : nearby)
{
    if (other == entity) continue;
    if (!registry.all_of<Position, AABBCollider>(other)) continue;

    auto& otherPos = registry.get<Position>(other);
    auto& otherCol = registry.get<AABBCollider>(other);
    AABB otherBox = AABB::fromCentreSize(otherPos.value, otherCol.halfExtents);

    // if it's a trigger, don't resolve - just detect
    if (otherCol.isTrigger)
    {
        // ...
        continue;
    }

    // ← NEW: check collision layers
    bool shouldCollide = (col.layer & otherCol.mask) != 0 &&
                         (otherCol.layer & col.mask) != 0;
    if (!shouldCollide) continue;

    SweepResult hit = sweepAABB(entityBox, movement, otherBox);
    // ...
}
```

The layer check is **bidirectional**: entity A must be on a layer that B cares about, *and* B must be on a layer that A cares about. This prevents asymmetric collisions where one entity blocks another but not the reverse.

---

## 4. System Phase Ordering

### The Problem

Right now, system execution order is implicit -- it is just the order you wrote the function calls. There is no indication of *why* that order matters, or what would break if you changed it.

We are not going to build a full scheduler here. That is a significant piece of architecture that we do not need yet, and premature abstraction is just as dangerous as premature optimisation. Instead, we are going to **document the phases** with clear comments and a simple enum.

### The Phase Enum

Create `src/engine/core/system_phase.h`:

```cpp
// engine/core/system_phase.h
#pragma once

// Defines the conceptual phases of the game loop.
// Systems should run in this order. This enum exists for documentation
// and future use (e.g. a scheduler), not for runtime dispatch.
//
// Phase order:
//   1. Input       - Poll events, read input state.
//                    Must run before anything reads input.
//
//   2. Physics     - Fixed timestep. Gravity, friction, collision detection
//                    and response, movement, ground detection.
//                    Runs 0-N times per frame inside the accumulator loop.
//
//   3. GameLogic   - Gameplay rules that respond to physics results.
//                    Health, scoring, state machines, AI decisions.
//                    Runs once per frame, after all physics steps.
//
//   4. LateUpdate  - Post-logic cleanup. Camera follow, animation blending,
//                    transform hierarchy propagation.
//
//   5. Render      - Read positions, submit draw calls. Must be last.

enum class SystemPhase
{
	Input,
	Physics,
	GameLogic,
	LateUpdate,
	Render
};
```

### Why This Ordering?

This is not arbitrary. Each phase has a reason for its position:

**Input first** because every other system might read input. If you run physics before input, you are simulating with *last frame's* input -- one frame of latency that players can feel.

**Physics second** because it runs at fixed rate inside the accumulator and must produce a consistent simulation state before game logic reacts to it. Within the physics phase, the order matters too:

```
physicsSystem          ← apply gravity and friction (modify velocities)
collisionSystem        ← sweep and resolve against world + entities (correct velocities)
movementSystem         ← apply final velocities to positions
groundDetectionSystem  ← probe downward to update OnGround for next frame
```

**GameLogic third** because it needs the results of physics (who collided with what, where is everything) to make decisions.

**LateUpdate fourth** because it needs the final game state. The camera should follow the player's *final* position this frame, not the position before game logic moved them.

**Render last** because it is read-only. It should never modify game state.

---

## 5. Multiple Point Lights

### The Problem

Open `src/engine/ecs/systems/render_system.cpp` and look at the point light section:

```cpp
auto pointView = registry.view<Position, PointLight>();

for (auto [entity, pos, light] : pointView.each())
{
    pointLightPos = pos.value;
    pointLightColor = light.color;
    pointAmbient = light.ambientStrength;
    hasPointLight = true;
    break;  // <── only the first point light is ever used
}
```

And in `assets/shaders/lit.frag`, the attenuation values are hardcoded:

```glsl
float attenuation = 1.0 / (1.0 + 0.09 * distance + 0.032 * distance * distance);
```

The `PointLight` component has `linear` and `quadratic` fields that are never sent to the shader. This means every point light has identical falloff regardless of what values you set in the component.

### The Fix: Shader

We need the fragment shader to accept an **array** of point lights. GLSL supports this with structs and uniform arrays. Replace the point light section of `assets/shaders/lit.frag`:

```glsl
#version 460 core

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoord;

out vec4 FragColor;

// Material
uniform sampler2D textureSampler;
uniform float shininess;

// Directional light
uniform vec3 dirLightDir;
uniform vec3 dirLightColor;
uniform float dirLightAmbient;
uniform bool hasDirLight;

// Point lights — supports up to MAX_POINT_LIGHTS simultaneously
#define MAX_POINT_LIGHTS 8

struct PointLightData {
    vec3 position;
    vec3 color;
    float ambient;
    float linear;
    float quadratic;
};

uniform int numPointLights;
uniform PointLightData pointLights[MAX_POINT_LIGHTS];

// Camera
uniform vec3 viewPos;

// ─── Helper: calculate one point light's contribution ──────────
vec3 calcPointLight(PointLightData light, vec3 normal, vec3 fragPos, vec3 viewDir, vec3 texColor)
{
    vec3 lDir = normalize(light.position - fragPos);

    float distance = length(light.position - fragPos);
    float attenuation = 1.0 / (1.0 + light.linear * distance + light.quadratic * distance * distance);

    vec3 ambient = light.ambient * light.color;

    float diff = max(dot(normal, lDir), 0.0);
    vec3 diffuse = diff * light.color;

    vec3 reflectDir = reflect(-lDir, normal);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), shininess);
    vec3 specular = spec * light.color;

    return (ambient + (diffuse + specular) * attenuation) * texColor;
}

void main() {
    vec3 texColor = texture(textureSampler, TexCoord).rgb;
    vec3 norm = normalize(Normal);
    vec3 viewDir = normalize(viewPos - FragPos);

    vec3 result = vec3(0.0);

    // ─── Directional light contribution ──────────────────────────
    if (hasDirLight) {
        vec3 lDir = normalize(-dirLightDir);

        vec3 ambient = dirLightAmbient * dirLightColor;

        float diff = max(dot(norm, lDir), 0.0);
        vec3 diffuse = diff * dirLightColor;

        vec3 reflectDir = reflect(-lDir, norm);
        float spec = pow(max(dot(viewDir, reflectDir), 0.0), shininess);
        vec3 specular = spec * dirLightColor;

        result += (ambient + diffuse + specular) * texColor;
    }

    // ─── Point light contributions (all of them) ─────────────────
    for (int i = 0; i < numPointLights; i++) {
        result += calcPointLight(pointLights[i], norm, FragPos, viewDir, texColor);
    }

    FragColor = vec4(result, 1.0);
}
```

### Why This Design?

**Why `#define MAX_POINT_LIGHTS 8`?** GLSL requires a compile-time constant for array sizes. 8 is enough for most scenes and keeps uniform buffer usage reasonable. If you need more, just increase it — the only cost is GPU register pressure.

**Why a helper function?** The per-light calculation is identical for every point light. Extracting it into `calcPointLight()` keeps the main function readable and avoids a giant nested loop body.

**Why pass `linear` and `quadratic` per-light?** Different lights should have different falloff ranges. A torch illuminates a few metres; a floodlight illuminates a room. The `PointLight` component already has these fields — now they actually reach the shader.

### The Fix: Render System

There are two changes in `src/engine/ecs/systems/render_system.cpp`: the light collection at the top of the function, and the uniform upload inside the draw loop.

You will also need to add `#include <string>` at the top of the file for `std::to_string`.

**Change 1: Light collection.** Find the point light collection block near the top of `renderSystem()`.

Before:

```cpp
// src/engine/ecs/systems/render_system.cpp — light collection (before)

// point light (use first one found)
glm::vec3 pointLightPos(0.0f);
glm::vec3 pointLightColor(0.0f);
float pointAmbient = 0.05f;
bool hasPointLight = false;

auto pointView = registry.view<Position, PointLight>();

for (auto [entity, pos, light] : pointView.each())
{
    pointLightPos = pos.value;
    pointLightColor = light.color;
    pointAmbient = light.ambientStrength;
    hasPointLight = true;
    break;
}
```

After:

```cpp
// src/engine/ecs/systems/render_system.cpp — light collection (after)

// ─── Collect point lights ────────────────────────────────────
struct PointLightGPU {
    glm::vec3 position;
    glm::vec3 color;
    float ambient;
    float linear;
    float quadratic;
};

constexpr int MAX_POINT_LIGHTS = 8;
PointLightGPU pointLightsData[MAX_POINT_LIGHTS];
int numPointLights = 0;

auto pointView = registry.view<Position, PointLight>();
for (auto [entity, pos, light] : pointView.each())
{
    if (numPointLights >= MAX_POINT_LIGHTS) break;

    pointLightsData[numPointLights] = {
        pos.value,
        light.color,
        light.ambientStrength,
        light.linear,
        light.quadratic
    };
    numPointLights++;
}
```

The four single-light variables (`pointLightPos`, `pointLightColor`, `pointAmbient`, `hasPointLight`) are gone, replaced by an array that collects up to 8 lights.

**Change 2: Uniform upload.** Inside the draw loop, find the block that sends point light uniforms to the shader.

Before:

```cpp
// src/engine/ecs/systems/render_system.cpp — draw loop uniforms (before)

loc = glGetUniformLocation(mesh.shaderId, "hasPointLight");
glUniform1i(loc, hasPointLight ? 1 : 0);

// ... (hasDirLight block stays the same) ...

if (hasPointLight)
{
    loc = glGetUniformLocation(mesh.shaderId, "pointLightPos");
    glUniform3fv(loc, 1, &pointLightPos[0]);
    loc = glGetUniformLocation(mesh.shaderId, "pointLightColor");
    glUniform3fv(loc, 1, &pointLightColor[0]);
    loc = glGetUniformLocation(mesh.shaderId, "pointLightAmbient");
    glUniform1f(loc, pointAmbient);
}
```

After:

```cpp
// src/engine/ecs/systems/render_system.cpp — draw loop uniforms (after)

// ... (hasDirLight block stays the same) ...

// Point lights
loc = glGetUniformLocation(mesh.shaderId, "numPointLights");
glUniform1i(loc, numPointLights);

for (int i = 0; i < numPointLights; i++)
{
    std::string prefix = "pointLights[" + std::to_string(i) + "].";

    loc = glGetUniformLocation(mesh.shaderId, (prefix + "position").c_str());
    glUniform3fv(loc, 1, &pointLightsData[i].position[0]);

    loc = glGetUniformLocation(mesh.shaderId, (prefix + "color").c_str());
    glUniform3fv(loc, 1, &pointLightsData[i].color[0]);

    loc = glGetUniformLocation(mesh.shaderId, (prefix + "ambient").c_str());
    glUniform1f(loc, pointLightsData[i].ambient);

    loc = glGetUniformLocation(mesh.shaderId, (prefix + "linear").c_str());
    glUniform1f(loc, pointLightsData[i].linear);

    loc = glGetUniformLocation(mesh.shaderId, (prefix + "quadratic").c_str());
    glUniform1f(loc, pointLightsData[i].quadratic);
}
```

Remove the `hasPointLight` uniform line entirely — the shader now uses `numPointLights` (which is 0 when there are no lights) instead of a boolean flag.

### Performance Note

Calling `glGetUniformLocation` with string concatenation in a loop every frame is not ideal. For a learning project this is fine — the cost is negligible compared to the draw calls themselves. In a production engine, you would cache the uniform locations once after shader compilation. We will revisit this in a later cleanup chapter.

---

## 6. Test, Then Remove Test Entities

Before removing anything, **verify that all the changes in this chapter work correctly**. The test cubes are your best diagnostic tool — leave them in and rebuild:

- **`cube`** (the sliding cube at `(-3, 0.5, -3)`) has `Velocity` and `AABBCollider`. It should slide along +X and collide with the wall. This confirms `movementSystem` and `collisionSystem` are receiving time correctly via `PhysicsConfig`.
- **`cube2`** (the falling cube at `(2, 4, 0)`) has `Gravity`, `OnGround`, and `AABBCollider`. It should fall under gravity and land on the floor. This confirms `physicsSystem` and `groundDetectionSystem` are working with the new signatures.
- **The point light** (the torch at `(3, 2, -1)`) should cast a visible warm glow on nearby surfaces. This confirms the multi-point-light shader and render system changes are working.

If any of those are not working, you have a bug in one of the earlier sections — go back and check before continuing.

Once everything works, the test cubes have served their purpose. They were useful for verifying:

- Rendering works (Chapter 5)
- Collision works (Chapter 9 -- `cube` gained `Velocity` and `AABBCollider`)
- Physics works (Chapter 10 -- `cube2` gained `Gravity` and `OnGround`)
- **This chapter's cleanup** -- all systems still work after removing `float dt` and switching to `PhysicsConfig`

From Chapter 11 onward, we will create proper interactive entities (doors, lifts, pickups, enemies). The test cubes would just be clutter.

Remove both test cube blocks from `setupScene()` in `src/engine/ecs/scene_setup.cpp`. Your `setupScene` function should now only contain:

- Level geometry creation (`createTestLevel()` and sector entity loop)
- Lights (directional sun + point light torch)

```cpp
Level setupScene(entt::registry& registry, const ResourceManager& resources)
{
    auto litShader = resources.getShader("lit");
    auto wallTexture = resources.getTexture("wall");

    // ─── Create the level geometry ───────────────────────────────
    Level level = createTestLevel();

    for (const auto& sector : level.sectors)
    {
        if (!sector.mesh) continue;

        auto sectorEntity = registry.create();
        registry.emplace<Position>(sectorEntity, glm::vec3(0.0f));
        registry.emplace<MeshRenderer>
        (
            sectorEntity,
            sector.mesh->getVAO(),
            0u,
            litShader->getId(),
            wallTexture->getId(),
            true,
            sector.mesh->getIndexCount()
        );
    }

    // ─── Lights ──────────────────────────────────────────────────
    auto sun = registry.create();
    registry.emplace<DirectionalLight>
    (
        sun, glm::vec3(-0.2f, -1.0f, -0.3f),
        glm::vec3(1.0f, 0.95f, 0.8f), 0.1f
    );

    auto torch = registry.create();
    registry.emplace<Position>(torch, glm::vec3(3.0f, 2.0f, -1.0f));
    registry.emplace<PointLight>
    (
        torch, glm::vec3(2.0f, 1.4f, 0.6f),
        0.15f, 0.045f, 0.0075f
    );

    return level;
}
```

You can also remove the `cubeMesh` resource load from `setupScene` since no entities use it anymore. The `cube.obj` mesh asset stays in your project -- later chapters will use it for interactive objects.

---

## The Solution: After

Now let us put it all together. Here is the refactored game loop:

```cpp
// main.cpp - after cleanup

#include "engine/core/window.h"
#include "engine/core/input_manager.h"
#include "engine/core/resource_manager.h"
#include "engine/core/fixed_timestep.h"
#include "engine/ecs/components.h"
#include "engine/ecs/scene_setup.h"
#include "engine/ecs/systems/render_system.h"
#include "engine/ecs/systems/collision_system.h"
#include "engine/ecs/systems/movement_system.h"
#include "engine/ecs/systems/physics_system.h"
#include "engine/physics/spatial_hash.h"
#include "engine/physics/physics_config.h"
#include "engine/renderer/camera.h"

#include <entt/entt.hpp>

int main()
{
    Window window(1280, 720, "QEngine");

    InputManager input;
    input.init(window.getHandle());

    ResourceManager resources;

    // ... resource loading (shaders, textures) ...

    Camera camera(glm::vec3(0.0f, 1.7f, 3.0f));

    entt::registry registry;

    // --- Configuration ---
    auto& physicsConfig = registry.ctx().emplace<PhysicsConfig>();
    // Override defaults if desired:
    // physicsConfig.terminalVelocity = 30.0f;  // slower falling

    // --- Scene ---
    Level level = setupScene(registry, resources);
    SpatialHash spatialHash(4.0f);

    // --- Game loop ---
    FixedTimestep fixedTimestep(physicsConfig.fixedDeltaTime);

    glEnable(GL_DEPTH_TEST);

    while (!window.shouldClose())
    {
        fixedTimestep.accumulate((float)glfwGetTime());
        float frameTime = fixedTimestep.getFrameTime();

        // -- Phase: Input --
        input.update();
        window.pollEvents();

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

        // -- Phase: Physics (fixed timestep) --
        while (fixedTimestep.step())
        {
            physicsSystem(registry);
            collisionSystem(registry, spatialHash, level);
            movementSystem(registry);
            groundDetectionSystem(registry, level);
        }

        // -- Phase: GameLogic --
        // (future: scoring, health, AI, state machines)

        // -- Phase: Render --
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        float aspectRatio = (float)window.getWidth() / (float)window.getHeight();
        renderSystem(registry, camera, aspectRatio);

        window.swapBuffers();
    }

    resources.clear();
    return 0;
}
```

Compare this with the "before" version. The game loop is now:

- **Shorter** -- the accumulator boilerplate is gone, replaced by three method calls
- **Self-documenting** -- phase comments make the execution order explicit
- **Magic-number-free** -- terminal velocity and timestep live in `PhysicsConfig`, collision layers have names
- **Consistent** -- every physics system reads the same config struct
- **Properly lit** -- all point lights render, with per-light attenuation
- **Clean** -- test entities removed, ready for real game content

And we did not add any frameworks, any virtual dispatch, any complex abstractions. Just a small class, a struct, a namespace, some shader work, and some comments.

---

## Project Structure

After this cleanup, your project tree should look like this (showing only the files we added or modified):

```
assets/
└── shaders/
    └── lit.frag                    ← MODIFIED: multi-point-light support
src/
├── engine/
│   ├── core/
│   │   ├── fixed_timestep.h        ← NEW: accumulator pattern
│   │   └── system_phase.h          ← NEW: phase ordering documentation
│   ├── ecs/
│   │   ├── components.h            ← MODIFIED: AABBCollider gains layer/mask
│   │   ├── scene_setup.cpp         ← MODIFIED: test cubes removed
│   │   └── systems/
│   │       ├── render_system.cpp   ← MODIFIED: passes all point lights to shader
│   │       ├── physics_system.h    ← MODIFIED: dt removed from signatures
│   │       ├── physics_system.cpp  ← MODIFIED: reads PhysicsConfig from context
│   │       ├── collision_system.cpp← MODIFIED: uses CollisionLayers, reads config
│   │       └── movement_system.cpp ← MODIFIED: reads PhysicsConfig from context
│   └── physics/
│       ├── physics_config.h        ← NEW: centralised physics parameters
│       └── collision_layers.h      ← NEW: named bitmask constants
└── main.cpp                        ← MODIFIED: uses FixedTimestep, phase comments
```

All new C++ files are header-only. No additional `.cpp` files, no build system changes. Include and go.

---

## Exercises

1. **Add a debug overlay** that prints `PhysicsConfig` values to the console on a keypress. Bonus: make them adjustable with keyboard shortcuts so you can tweak physics feel at runtime.

2. **Add a `CollisionLayers::Pickup` layer** (value `0x20`). Add it to a `Collectible` combined mask alongside `Player`.

3. **Add a manual `accumulate(float deltaTime)` overload** to `FixedTimestep` for testing. Verify that accumulating exactly 3 timesteps worth of time makes `step()` return `true` exactly 3 times.

4. **Create a "moon gravity" mode** toggled by a key. Halve `PhysicsConfig::terminalVelocity` and modify the `Gravity` component on all entities. Think about where the toggle logic belongs (hint: Phase 1, Input).

---

## Key Takeaways

- **The accumulator pattern is always the same.** Extract it once, use it forever. The `FixedTimestep` class is maybe 50 lines and eliminates a whole class of copy-paste bugs.

- **Registry context is your friend for global config.** `registry.ctx().emplace<T>()` gives you a typed singleton attached to the registry's lifetime. No globals, no singletons, no service locators -- just a struct that lives where your entities live.

- **Per-entity values belong in components; global values belong in context.** Gravity strength varies per entity (a balloon vs a crate), so it lives in the `Gravity` component. Terminal velocity is a simulation-wide constant, so it lives in `PhysicsConfig`.

- **Named constants are not optional.** `CollisionLayers::Player | CollisionLayers::World` is code that explains itself. `0x03` is code that demands a comment, and comments go stale.

- **Document your system order even if you do not enforce it.** A `// -- Phase: Physics --` comment costs nothing and saves real debugging time.

- **If you have data in a component, use it.** The `PointLight` component had `linear` and `quadratic` fields from day one, but the shader hardcoded its own values. Unused component fields are a code smell -- either the component is over-designed, or the system is under-implemented. In this case, it was the latter.

- **Clean up after yourself.** Removing the test cubes is not busywork -- it prevents confusion. Every entity in your scene should have a reason to exist. From Chapter 11 onward, every entity will be a real part of the game.

---

*Next up: **Chapter 11 -- Doors, Lifts & Triggers**, where we will build interactive level elements and find out why our new phase ordering makes it trivial to decide where trigger logic belongs.*
