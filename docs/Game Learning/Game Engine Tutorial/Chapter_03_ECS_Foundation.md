# Chapter 3: ECS Foundation

## What You'll Learn
- How to use EnTT — a production-quality C++ ECS library
- Defining components as plain data structs
- Creating entities and attaching components
- Writing systems that query and operate on entities
- Refactoring the triangle to use ECS

---

## Why EnTT?

We're not building an ECS from scratch — that's a tutorial in itself and would distract from learning engine architecture. EnTT is:

- Header-only (just `#include` it, no compilation needed)
- The most widely used C++ ECS library
- Used in Minecraft Bedrock Edition (by Mojang)
- Extremely fast (cache-friendly archetype storage)
- Modern C++17

The core API is small. You'll learn it all in this chapter.

---

## The Three Concepts

Reminder from the README:

| Concept | What It Is | Rule |
|---------|-----------|------|
| **Entity** | A unique ID (just a number) | Has no data or behaviour |
| **Component** | A plain data struct | Has no behaviour (no methods) |
| **System** | A function that queries entities by their components | Has no state |

---

## EnTT Basics

### The Registry

Everything lives in an `entt::registry` — it's the world that holds all entities and components:

```cpp
#include <entt/entt.hpp>

entt::registry registry;
```

### Creating Entities

```cpp
entt::entity player = registry.create();
entt::entity enemy = registry.create();
```

An entity is just a number. It has no data until you attach components.

### Defining Components

Components are plain structs. No inheritance, no base class, no macros:

```cpp
struct Position {
    float x, y, z;
};

struct Velocity {
    float dx, dy, dz;
};

struct Health {
    float current;
    float max;
};
```

That's it. No `Component` base class. No registration. EnTT uses C++ templates to handle everything automatically.

### Attaching Components to Entities

```cpp
// emplace = "create this component and attach it to this entity"
registry.emplace<Position>(player, 0.0f, 0.0f, 0.0f);
registry.emplace<Velocity>(player, 1.0f, 0.0f, 0.0f);
registry.emplace<Health>(player, 100.0f, 100.0f);

registry.emplace<Position>(enemy, 10.0f, 0.0f, 5.0f);
registry.emplace<Health>(enemy, 50.0f, 50.0f);
// Note: enemy has no Velocity — it can't move (yet)
```

### Reading and Modifying Components

```cpp
// Get a reference to a component
auto& pos = registry.get<Position>(player);
pos.x += 5.0f;  // Modify it directly

// Check if an entity has a component
if (registry.all_of<Velocity>(enemy)) {
    // enemy has velocity
}
```

### Destroying Entities

```cpp
registry.destroy(enemy);
```

All components attached to that entity are automatically removed.

---

## Writing Systems

A system is just a function. It uses `registry.view<>()` to query for entities that have specific components:

```cpp
void movementSystem(entt::registry& registry, float dt) {
    // Get every entity that has BOTH Position AND Velocity
    auto view = registry.view<Position, Velocity>();

    for (auto [entity, pos, vel] : view.each()) {
        pos.x += vel.dx * dt;
        pos.y += vel.dy * dt;
        pos.z += vel.dz * dt;
    }
}
```

### C++ Concept: Structured Bindings

```cpp
auto [entity, pos, vel] : view.each()
```

This is a **structured binding** (C++17). It unpacks the tuple returned by `view.each()` into individual variables. Without it, you'd write:

```cpp
for (auto entity : view) {
    auto& pos = view.get<Position>(entity);
    auto& vel = view.get<Velocity>(entity);
    pos.x += vel.dx * dt;
    // ...
}
```

Both work. Structured bindings are cleaner.

### C++ Concept: Templates

```cpp
registry.view<Position, Velocity>()
registry.emplace<Health>(entity, 100.0f, 100.0f)
```

The `<Position, Velocity>` is a **template argument** — it tells EnTT which component types to work with at **compile time**. The compiler generates specialised, efficient code for each unique combination. You don't pay for runtime type checking.

### More Systems

```cpp
void deathSystem(entt::registry& registry) {
    auto view = registry.view<Health>();

    // Collect dead entities first (can't destroy while iterating)
    std::vector<entt::entity> dead;

    for (auto [entity, health] : view.each()) {
        if (health.current <= 0.0f) {
            dead.push_back(entity);
        }
    }

    for (auto entity : dead) {
        registry.destroy(entity);
    }
}

void gravitySystem(entt::registry& registry, float dt) {
    auto view = registry.view<Velocity>();

    for (auto [entity, vel] : view.each()) {
        vel.dy -= 9.81f * dt;  // Apply gravity
    }
}
```

**Important:** You cannot destroy entities while iterating over a view. Collect them first, then destroy after the loop. EnTT will invalidate iterators if you destroy mid-loop.

---

## Refactoring the Triangle to ECS

Now let's take the hard-coded triangle from Chapter 2 and make it an ECS entity.

### Step 1: Define Rendering Components

Create `src/engine/ecs/components.h`:

```cpp
#pragma once

#include <glm/glm.hpp>

// ─── Spatial Components ──────────────────────────────────────────

struct Position {
    glm::vec3 value = glm::vec3(0.0f);
};

struct Rotation {
    glm::vec3 euler = glm::vec3(0.0f);  // pitch, yaw, roll in degrees
};

struct Scale {
    glm::vec3 value = glm::vec3(1.0f);
};

struct Velocity {
    glm::vec3 value = glm::vec3(0.0f);
};

// ─── Rendering Components ────────────────────────────────────────

struct MeshRenderer {
    unsigned int vao = 0;        // Vertex Array Object handle
    unsigned int vertexCount = 0; // Number of vertices to draw
    unsigned int shaderId = 0;    // Shader program to use
};

struct Color {
    glm::vec4 value = glm::vec4(1.0f);  // RGBA
};

// ─── Tags ────────────────────────────────────────────────────────
// Tags are empty structs — they mark entities without adding data

struct TagPlayer {};
```

**Default member initialisers** (`= glm::vec3(0.0f)`) mean you can create a component without specifying every field. For example, `registry.emplace<Position>(entity)` gives you a position at the origin.

### Step 2: Create the Render System

Create `src/engine/ecs/systems/render_system.h`:

```cpp
#pragma once

#include <entt/entt.hpp>

void renderSystem(entt::registry& registry);
```

Create `src/engine/ecs/systems/render_system.cpp`:

```cpp
#include "engine/ecs/systems/render_system.h"
#include "engine/ecs/components.h"
#include <glad/glad.h>

void renderSystem(entt::registry& registry) {
    auto view = registry.view<MeshRenderer>();

    for (auto [entity, mesh] : view.each()) {
        glUseProgram(mesh.shaderId);
        glBindVertexArray(mesh.vao);
        glDrawArrays(GL_TRIANGLES, 0, mesh.vertexCount);
    }
}
```

This system does exactly what our hard-coded draw call did — but now it works for **any** entity that has a `MeshRenderer` component. One triangle, a hundred triangles, a thousand — the system handles them all.

### Step 3: Create a Movement System

Create `src/engine/ecs/systems/movement_system.h`:

```cpp
#pragma once

#include <entt/entt.hpp>

void movementSystem(entt::registry& registry, float dt);
```

Create `src/engine/ecs/systems/movement_system.cpp`:

```cpp
#include "engine/ecs/systems/movement_system.h"
#include "engine/ecs/components.h"

void movementSystem(entt::registry& registry, float dt) {
    auto view = registry.view<Position, Velocity>();

    for (auto [entity, pos, vel] : view.each()) {
        pos.value += vel.value * dt;
    }
}
```

### Step 4: Update main.cpp

```cpp
// src/main.cpp
#include "engine/core/window.h"
#include "engine/renderer/shader.h"
#include "engine/ecs/components.h"
#include "engine/ecs/systems/render_system.h"
#include "engine/ecs/systems/movement_system.h"

#include <entt/entt.hpp>
#include <iostream>

int main() {
    Window window(1280, 720, "QEngine");

    // ─── Shader ──────────────────────────────────────────────────
    Shader basicShader("assets/shaders/basic.vert", "assets/shaders/basic.frag");

    // ─── Triangle mesh setup (same as before) ────────────────────
    float vertices[] = {
        -0.5f, -0.5f, 0.0f,  1.0f, 0.0f, 0.0f,
         0.5f, -0.5f, 0.0f,  0.0f, 1.0f, 0.0f,
         0.0f,  0.5f, 0.0f,  0.0f, 0.0f, 1.0f
    };

    unsigned int VAO, VBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    // ─── ECS: Create the world ───────────────────────────────────
    entt::registry registry;

    // Create a triangle entity
    auto triangle = registry.create();
    registry.emplace<Position>(triangle, glm::vec3(0.0f, 0.0f, 0.0f));
    registry.emplace<Velocity>(triangle, glm::vec3(0.2f, 0.0f, 0.0f));
    registry.emplace<MeshRenderer>(triangle, VAO, 3u, basicShader.getId());

    // ─── Game Loop ───────────────────────────────────────────────
    float deltaTime = 0.0f;
    float lastFrame = 0.0f;

    while (!window.shouldClose()) {
        float currentFrame = (float)glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        window.pollEvents();

        if (glfwGetKey(window.getHandle(), GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window.getHandle(), true);
        }

        // ─── ECS Systems (tick order!) ───────────────────────────
        movementSystem(registry, deltaTime);   // Update positions
        // ... future systems go here ...

        // ─── Render ──────────────────────────────────────────────
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        renderSystem(registry);                // Draw everything

        window.swapBuffers();
    }

    // ─── Cleanup ─────────────────────────────────────────────────
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);

    return 0;
}
```

### Update CMakeLists.txt

```cmake
add_executable(QEngine
    src/main.cpp
    src/engine/core/window.cpp
    src/engine/renderer/shader.cpp
    src/engine/ecs/systems/render_system.cpp
    src/engine/ecs/systems/movement_system.cpp
)
```

---

## What Changed?

The triangle is now an **entity** in the ECS world. Compare:

**Before (hard-coded):**
```cpp
basicShader.use();
glBindVertexArray(VAO);
glDrawArrays(GL_TRIANGLES, 0, 3);
```

**After (ECS):**
```cpp
movementSystem(registry, deltaTime);  // Moves anything with Position + Velocity
renderSystem(registry);                // Draws anything with MeshRenderer
```

The triangle moves because it has `Position` + `Velocity`. It renders because it has `MeshRenderer`. The systems don't know or care that it's "a triangle" — they just process data.

> **Note:** The triangle won't visually move yet because our shader doesn't use the `Position` component to offset the vertices. We'll fix that in Chapter 4 when we add the model/view/projection matrix transforms. For now, the Position data is updating correctly in memory — we just can't see it on screen yet.

---

## Adding More Entities

The power of ECS becomes obvious when you add more entities. Try adding a second triangle entity after the first:

```cpp
    auto triangle2 = registry.create();
    registry.emplace<Position>(triangle2, glm::vec3(0.0f, 0.0f, 0.0f));
    registry.emplace<Velocity>(triangle2, glm::vec3(-0.1f, 0.15f, 0.0f));
    registry.emplace<MeshRenderer>(triangle2, VAO, 3u, basicShader.getId());
```

Both triangles share the same mesh data (same VAO) but have different velocities. The movement system moves both. The render system draws both. **No code changed in either system.**

Now try adding an entity with no `Velocity`:

```cpp
    auto staticThing = registry.create();
    registry.emplace<Position>(staticThing, glm::vec3(0.5f, 0.5f, 0.0f));
    registry.emplace<MeshRenderer>(staticThing, VAO, 3u, basicShader.getId());
```

This entity renders (it has `MeshRenderer`) but doesn't move (no `Velocity`). The movement system's query — `registry.view<Position, Velocity>()` — never returns it.

---

## The System Tick Order So Far

```cpp
// Game logic
movementSystem(registry, deltaTime);

// Rendering
glClear(GL_COLOR_BUFFER_BIT);
renderSystem(registry);
```

As we add more systems in future chapters, they'll slot into this order:

```
1. inputSystem          ← Chapter 4
2. movementSystem       ← This chapter
3. collisionSystem      ← Chapter 9
4. ...
5. renderSystem         ← This chapter (always last)
```

---

## Recap

| What We Did | Why |
|-------------|-----|
| Defined components as plain structs | Data with no behaviour |
| Created entities with `registry.create()` | Just IDs — meaningless until you attach data |
| Attached components with `registry.emplace<>()` | Composition: the entity "is" whatever data it has |
| Wrote systems as free functions | Logic with no state — queries data and operates on it |
| Used `registry.view<>()` to query | "Give me everything with these components" |
| Placed systems in tick order | Input → Logic → Render |

---

## What's Next

In **Chapter 4**, we'll add 3D transforms — model, view, and projection matrices using GLM. This is what lets us move the camera, position objects in 3D space, and have perspective (things get smaller as they get further away). The triangle will finally move on screen.
