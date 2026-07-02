# Chapter 4: 3D Transforms & Camera

## What You'll Learn
- How 3D objects are positioned, rotated, and scaled using matrices
- The Model → View → Projection pipeline
- How to build an FPS camera with mouse look and WASD movement
- Passing matrices to shaders via uniforms
- Why the triangle finally moves on screen

---

## The Problem

In Chapter 3, we gave our triangle a `Position` component and a `Velocity`, and the movement system updates the position every frame. But the triangle doesn't move on screen. Why?

Because the vertex shader hard-codes the output:

```glsl
gl_Position = vec4(aPos, 1.0);  // Uses raw vertex positions — ignores Position component
```

We need to tell the shader where the object is in the world, where the camera is, and how to project 3D onto a 2D screen. That's done with **three matrices**.

---

## The Three Matrices

Every vertex in the engine goes through three transformations:

```
Local Space ──▶ World Space ──▶ View Space ──▶ Clip Space ──▶ Screen
            Model           View          Projection
```

### 1. Model Matrix — "Where is this object in the world?"

Transforms a vertex from **local space** (relative to the object's origin) to **world space** (an absolute position in the game world).

Contains: position, rotation, and scale of the object.

```cpp
glm::mat4 model = glm::mat4(1.0f);  // Start with identity (no transform)
model = glm::translate(model, glm::vec3(2.0f, 0.0f, -5.0f));  // Move it
model = glm::rotate(model, glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f));  // Rotate
model = glm::scale(model, glm::vec3(0.5f));  // Scale to half size
```

### 2. View Matrix — "Where is the camera?"

Transforms from **world space** to **view space** (relative to the camera). Conceptually, it moves the entire world so the camera is at the origin looking down -Z.

```cpp
glm::mat4 view = glm::lookAt(
    glm::vec3(0.0f, 0.0f, 3.0f),   // Camera position
    glm::vec3(0.0f, 0.0f, 0.0f),   // What it's looking at
    glm::vec3(0.0f, 1.0f, 0.0f)    // Up direction
);
```

### 3. Projection Matrix — "How does 3D become 2D?"

Transforms from **view space** to **clip space**. This is what creates perspective — things further away appear smaller.

```cpp
glm::mat4 projection = glm::perspective(
    glm::radians(70.0f),            // Field of view (Quake used 90, modern games ~70)
    1280.0f / 720.0f,               // Aspect ratio (window width / height)
    0.1f,                           // Near clip plane (anything closer is invisible)
    1000.0f                         // Far clip plane (anything further is invisible)
);
```

### In the Shader

The vertex shader multiplies them together:

```glsl
gl_Position = projection * view * model * vec4(aPos, 1.0);
```

The order matters: model first (local → world), then view (world → camera), then projection (camera → screen). Matrix multiplication is right-to-left.

---

## Updating the Shader

### assets/shaders/basic.vert (updated)

```glsl
#version 460 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;

out vec3 vertexColor;

// NEW: transformation matrices, set from C++ code
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main() {
    gl_Position = projection * view * model * vec4(aPos, 1.0);
    vertexColor = aColor;
}
```

### What's a Uniform?

A **uniform** is a value you set from C++ that's the same for every vertex in a draw call. Unlike vertex attributes (which change per-vertex), uniforms are constant for the entire mesh.

- `model` — different per object (each object has its own position/rotation)
- `view` — same for all objects in a frame (one camera)
- `projection` — same for all objects (one screen)

---

## Setting Uniforms from C++

Add these helper methods to the Shader class.

### src/engine/renderer/shader.h (additions)

```cpp
#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <string>

class Shader {
public:
    Shader(const std::string& vertexPath, const std::string& fragmentPath);
    ~Shader();

    void use() const;
    unsigned int getId() const { return m_programId; }

    // Uniform setters
    void setMat4(const std::string& name, const glm::mat4& value) const;
    void setVec3(const std::string& name, const glm::vec3& value) const;
    void setFloat(const std::string& name, float value) const;
    void setInt(const std::string& name, int value) const;

private:
    unsigned int m_programId;

    std::string readFile(const std::string& path) const;
    unsigned int compileShader(const std::string& source, GLenum type) const;
    void checkErrors(unsigned int shader, const std::string& type) const;
};
```

### src/engine/renderer/shader.cpp (additions)

Add these implementations:

```cpp
void Shader::setMat4(const std::string& name, const glm::mat4& value) const {
    glUniformMatrix4fv(
        glGetUniformLocation(m_programId, name.c_str()),
        1,              // count: 1 matrix
        GL_FALSE,       // transpose: no
        glm::value_ptr(value)  // pointer to the matrix data
    );
}

void Shader::setVec3(const std::string& name, const glm::vec3& value) const {
    glUniform3fv(
        glGetUniformLocation(m_programId, name.c_str()),
        1,
        glm::value_ptr(value)
    );
}

void Shader::setFloat(const std::string& name, float value) const {
    glUniform1f(glGetUniformLocation(m_programId, name.c_str()), value);
}

void Shader::setInt(const std::string& name, int value) const {
    glUniform1i(glGetUniformLocation(m_programId, name.c_str()), value);
}
```

### C++ Concept: `const std::string&`

```cpp
void setMat4(const std::string& name, const glm::mat4& value) const;
```

Three uses of `const` here:

1. `const std::string& name` — we receive the string by **reference** (no copy) and promise not to modify it
2. `const glm::mat4& value` — same for the matrix (a 4x4 float matrix is 64 bytes — too large to copy needlessly)
3. `const` at the end — this method doesn't modify the Shader object

`glm::value_ptr()` returns a `float*` pointing to the first element of the matrix/vector. OpenGL's C API needs raw pointers, not GLM objects.

---

## Building an FPS Camera

An FPS camera needs:
- Position in the world
- Direction it's looking (controlled by mouse)
- WASD movement relative to where it's facing

### src/engine/renderer/camera.h

```cpp
#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Camera {
public:
    Camera(glm::vec3 position = glm::vec3(0.0f, 0.0f, 3.0f));

    // Get the view matrix for this frame
    glm::mat4 getViewMatrix() const;

    // Get the projection matrix
    glm::mat4 getProjectionMatrix(float aspectRatio) const;

    // Process input
    void processKeyboard(int direction, float deltaTime);
    void processMouse(float xOffset, float yOffset);

    glm::vec3 getPosition() const { return m_position; }
    glm::vec3 getFront() const { return m_front; }

    // Movement directions (used as the 'direction' parameter)
    static constexpr int FORWARD  = 0;
    static constexpr int BACKWARD = 1;
    static constexpr int LEFT     = 2;
    static constexpr int RIGHT    = 3;

    float fov = 70.0f;
    float moveSpeed = 5.0f;
    float mouseSensitivity = 0.1f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;

private:
    glm::vec3 m_position;
    glm::vec3 m_front;
    glm::vec3 m_up;
    glm::vec3 m_right;
    glm::vec3 m_worldUp;

    float m_yaw;    // Horizontal rotation (look left/right)
    float m_pitch;  // Vertical rotation (look up/down)

    void updateVectors();
};
```

### src/engine/renderer/camera.cpp

```cpp
#include "engine/renderer/camera.h"
#include <algorithm>  // for std::clamp

Camera::Camera(glm::vec3 position)
    : m_position(position)
    , m_front(glm::vec3(0.0f, 0.0f, -1.0f))
    , m_worldUp(glm::vec3(0.0f, 1.0f, 0.0f))
    , m_yaw(-90.0f)     // -90 so we start looking down -Z (into the screen)
    , m_pitch(0.0f)
{
    updateVectors();
}

glm::mat4 Camera::getViewMatrix() const {
    return glm::lookAt(m_position, m_position + m_front, m_up);
}

glm::mat4 Camera::getProjectionMatrix(float aspectRatio) const {
    return glm::perspective(glm::radians(fov), aspectRatio, nearPlane, farPlane);
}

void Camera::processKeyboard(int direction, float deltaTime) {
    float velocity = moveSpeed * deltaTime;

    // Movement is relative to where the camera is facing
    switch (direction) {
        case FORWARD:  m_position += m_front * velocity; break;
        case BACKWARD: m_position -= m_front * velocity; break;
        case LEFT:     m_position -= m_right * velocity; break;
        case RIGHT:    m_position += m_right * velocity; break;
    }
}

void Camera::processMouse(float xOffset, float yOffset) {
    xOffset *= mouseSensitivity;
    yOffset *= mouseSensitivity;

    m_yaw   += xOffset;
    m_pitch += yOffset;

    // Clamp pitch so you can't look past straight up/down
    m_pitch = std::clamp(m_pitch, -89.0f, 89.0f);

    updateVectors();
}

void Camera::updateVectors() {
    // Calculate new front vector from yaw and pitch
    glm::vec3 front;
    front.x = cos(glm::radians(m_yaw)) * cos(glm::radians(m_pitch));
    front.y = sin(glm::radians(m_pitch));
    front.z = sin(glm::radians(m_yaw)) * cos(glm::radians(m_pitch));
    m_front = glm::normalize(front);

    // Recalculate right and up vectors
    m_right = glm::normalize(glm::cross(m_front, m_worldUp));
    m_up    = glm::normalize(glm::cross(m_right, m_front));
}
```

### How the Camera Math Works

**Yaw and Pitch** are angles:
- **Yaw** = horizontal rotation. 0° = looking down +X. -90° = looking down -Z (into the screen, our starting direction).
- **Pitch** = vertical rotation. 0° = level. +89° = looking nearly straight up.

We convert these angles into a **front vector** using trigonometry:

```
front.x = cos(yaw) * cos(pitch)    ← horizontal component
front.y = sin(pitch)                ← vertical component
front.z = sin(yaw) * cos(pitch)    ← depth component
```

The **right vector** is perpendicular to front and world-up (calculated via cross product). The **up vector** is perpendicular to right and front. Together, front/right/up form the camera's local coordinate system.

`glm::lookAt` builds a view matrix from position, target (position + front), and up.

---

## Capturing the Mouse

For an FPS camera, we need to:
1. Hide the cursor and lock it to the window centre
2. Capture raw mouse movement (deltas)

Add this after creating the window in `main.cpp`:

```cpp
    // Capture and hide the mouse cursor
    glfwSetInputMode(window.getHandle(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
```

And set up the mouse callback. We'll use a global for now (we'll clean this up later when the input system is formalised):

```cpp
// At the top of main.cpp, outside main():
float lastMouseX = 640.0f;
float lastMouseY = 360.0f;
float mouseXOffset = 0.0f;
float mouseYOffset = 0.0f;
bool firstMouse = true;

void mouseCallback(GLFWwindow* window, double xpos, double ypos) {
    float x = static_cast<float>(xpos);
    float y = static_cast<float>(ypos);

    if (firstMouse) {
        lastMouseX = x;
        lastMouseY = y;
        firstMouse = false;
    }

    mouseXOffset = x - lastMouseX;
    mouseYOffset = lastMouseY - y;  // Reversed: y goes bottom-to-top in OpenGL
    lastMouseX = x;
    lastMouseY = y;
}
```

Register the callback after creating the window:

```cpp
    glfwSetCursorPosCallback(window.getHandle(), mouseCallback);
```

---

## Updating the Render System

The render system now needs to set the model matrix for each entity and the view/projection matrices from the camera.

### src/engine/ecs/systems/render_system.h (updated)

```cpp
#pragma once

#include <entt/entt.hpp>
#include "engine/renderer/camera.h"
#include "engine/renderer/shader.h"

void renderSystem(entt::registry& registry, const Camera& camera,
                  float aspectRatio);
```

### src/engine/ecs/systems/render_system.cpp (updated)

```cpp
#include "engine/ecs/systems/render_system.h"
#include "engine/ecs/components.h"
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

void renderSystem(entt::registry& registry, const Camera& camera,
                  float aspectRatio) {

    glm::mat4 view = camera.getViewMatrix();
    glm::mat4 projection = camera.getProjectionMatrix(aspectRatio);

    auto viewGroup = registry.view<Position, MeshRenderer>();

    for (auto [entity, pos, mesh] : viewGroup.each()) {
        // Build the model matrix from the entity's Position (and Rotation/Scale if present)
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, pos.value);

        if (registry.all_of<Rotation>(entity)) {
            auto& rot = registry.get<Rotation>(entity);
            model = glm::rotate(model, glm::radians(rot.euler.y), glm::vec3(0, 1, 0));
            model = glm::rotate(model, glm::radians(rot.euler.x), glm::vec3(1, 0, 0));
            model = glm::rotate(model, glm::radians(rot.euler.z), glm::vec3(0, 0, 1));
        }

        if (registry.all_of<Scale>(entity)) {
            auto& scl = registry.get<Scale>(entity);
            model = glm::scale(model, scl.value);
        }

        // Set uniforms and draw
        glUseProgram(mesh.shaderId);

        // We need to set uniforms via the raw OpenGL calls here
        // since we only have the shader ID, not the Shader object
        GLint modelLoc = glGetUniformLocation(mesh.shaderId, "model");
        GLint viewLoc  = glGetUniformLocation(mesh.shaderId, "view");
        GLint projLoc  = glGetUniformLocation(mesh.shaderId, "projection");

        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, &model[0][0]);
        glUniformMatrix4fv(viewLoc,  1, GL_FALSE, &view[0][0]);
        glUniformMatrix4fv(projLoc,  1, GL_FALSE, &projection[0][0]);

        glBindVertexArray(mesh.vao);
        glDrawArrays(GL_TRIANGLES, 0, mesh.vertexCount);
    }
}
```

---

## Updated main.cpp

```cpp
// src/main.cpp
#include "engine/core/window.h"
#include "engine/renderer/shader.h"
#include "engine/renderer/camera.h"
#include "engine/ecs/components.h"
#include "engine/ecs/systems/render_system.h"
#include "engine/ecs/systems/movement_system.h"

#include <entt/entt.hpp>
#include <iostream>

// ─── Mouse state (temporary globals) ─────────────────────────────
float lastMouseX = 640.0f;
float lastMouseY = 360.0f;
float mouseXOffset = 0.0f;
float mouseYOffset = 0.0f;
bool firstMouse = true;

void mouseCallback(GLFWwindow* window, double xpos, double ypos) {
    float x = static_cast<float>(xpos);
    float y = static_cast<float>(ypos);

    if (firstMouse) {
        lastMouseX = x;
        lastMouseY = y;
        firstMouse = false;
    }

    mouseXOffset = x - lastMouseX;
    mouseYOffset = lastMouseY - y;
    lastMouseX = x;
    lastMouseY = y;
}

int main() {
    Window window(1280, 720, "QEngine");

    glfwSetInputMode(window.getHandle(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSetCursorPosCallback(window.getHandle(), mouseCallback);

    // ─── Shader ──────────────────────────────────────────────────
    Shader basicShader("assets/shaders/basic.vert", "assets/shaders/basic.frag");

    // ─── Triangle mesh ───────────────────────────────────────────
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

    // ─── Camera ──────────────────────────────────────────────────
    Camera camera(glm::vec3(0.0f, 0.0f, 3.0f));

    // ─── ECS ─────────────────────────────────────────────────────
    entt::registry registry;

    // A triangle sitting at the origin
    auto triangle = registry.create();
    registry.emplace<Position>(triangle, glm::vec3(0.0f, 0.0f, 0.0f));
    registry.emplace<MeshRenderer>(triangle, VAO, 3u, basicShader.getId());

    // A second triangle off to the right, slowly rotating (we'll add rotation later)
    auto triangle2 = registry.create();
    registry.emplace<Position>(triangle2, glm::vec3(2.0f, 0.0f, -1.0f));
    registry.emplace<Rotation>(triangle2, glm::vec3(0.0f, 45.0f, 0.0f));
    registry.emplace<MeshRenderer>(triangle2, VAO, 3u, basicShader.getId());

    // ─── Game Loop ───────────────────────────────────────────────
    float deltaTime = 0.0f;
    float lastFrame = 0.0f;

    // Enable depth testing (so closer things draw in front of further things)
    glEnable(GL_DEPTH_TEST);

    while (!window.shouldClose()) {
        float currentFrame = (float)glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        window.pollEvents();

        // ─── Input ───────────────────────────────────────────────
        if (glfwGetKey(window.getHandle(), GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window.getHandle(), true);

        if (glfwGetKey(window.getHandle(), GLFW_KEY_W) == GLFW_PRESS)
            camera.processKeyboard(Camera::FORWARD, deltaTime);
        if (glfwGetKey(window.getHandle(), GLFW_KEY_S) == GLFW_PRESS)
            camera.processKeyboard(Camera::BACKWARD, deltaTime);
        if (glfwGetKey(window.getHandle(), GLFW_KEY_A) == GLFW_PRESS)
            camera.processKeyboard(Camera::LEFT, deltaTime);
        if (glfwGetKey(window.getHandle(), GLFW_KEY_D) == GLFW_PRESS)
            camera.processKeyboard(Camera::RIGHT, deltaTime);

        camera.processMouse(mouseXOffset, mouseYOffset);
        mouseXOffset = 0.0f;  // Reset after processing
        mouseYOffset = 0.0f;

        // ─── Systems ─────────────────────────────────────────────
        movementSystem(registry, deltaTime);

        // ─── Render ──────────────────────────────────────────────
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        float aspectRatio = (float)window.getWidth() / (float)window.getHeight();
        renderSystem(registry, camera, aspectRatio);

        window.swapBuffers();
    }

    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);

    return 0;
}
```

### Key Changes

1. **`glEnable(GL_DEPTH_TEST)`** — enables the Z-buffer. Without this, triangles draw in whatever order they're processed, regardless of distance. With it, closer fragments overwrite further ones.

2. **`GL_DEPTH_BUFFER_BIT`** — we now clear both the colour buffer and the depth buffer each frame.

3. **Camera input** — WASD moves, mouse looks. The camera position and rotation feed into the view matrix.

4. **Two triangles** — one at the origin, one offset and rotated. You can walk around and see them from different angles.

### Update CMakeLists.txt

```cmake
add_executable(QEngine
    src/main.cpp
    src/engine/core/window.cpp
    src/engine/renderer/shader.cpp
    src/engine/renderer/camera.cpp
    src/engine/ecs/systems/render_system.cpp
    src/engine/ecs/systems/movement_system.cpp
)
```

---

## Expected Result

A dark grey scene with two coloured triangles. You can:
- **WASD** to move around
- **Mouse** to look around
- **Escape** to close

The triangles exist in 3D space — walk past them and you'll see them from the side (they'll appear as thin lines since they're flat). Walk behind them and you'll see the back (or nothing, if backface culling is enabled — we'll cover that later).

This is the moment the engine goes from "2D demo" to "3D world you can explore."

---

## Understanding the Matrix Pipeline Visually

For a vertex at local position `(-0.5, -0.5, 0.0)` on a triangle at world position `(2, 0, -1)`:

```
Local:      (-0.5, -0.5, 0.0)
                    │
           Model (translate by 2, 0, -1)
                    │
                    ▼
World:      (1.5, -0.5, -1.0)
                    │
           View (camera at 0, 0, 3 looking at -Z)
                    │
                    ▼
View:       (1.5, -0.5, -4.0)     ← 4 units in front of camera
                    │
           Projection (perspective)
                    │
                    ▼
Clip:       (some normalised coords)
                    │
           GPU viewport transform
                    │
                    ▼
Screen:     (pixel coordinates)
```

---

## What's Next

In **Chapter 5**, we'll add textures — loading images from disk and mapping them onto surfaces. The coloured triangles will become textured surfaces, which is the foundation for every wall, floor, and object in the game.
