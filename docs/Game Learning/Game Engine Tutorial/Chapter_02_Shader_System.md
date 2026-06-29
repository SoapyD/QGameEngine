# Chapter 2: Shader System

## What You'll Learn
- What shaders are and why they exist
- The OpenGL rendering pipeline
- Writing vertex and fragment shaders in GLSL
- Compiling and linking shaders in C++
- Rendering your first triangle
- Building a reusable Shader class

---

## What Is a Shader?

A shader is a small program that runs on the **GPU**, not the CPU. Your C++ code runs on the CPU and tells the GPU what to draw. Shaders tell the GPU **how** to draw it.

There are two shaders you'll write for almost everything:

| Shader | Runs on | Job |
|--------|---------|-----|
| **Vertex shader** | Every vertex | Position each corner of a triangle in screen space |
| **Fragment shader** | Every pixel (fragment) | Decide the colour of each pixel |

### The OpenGL Pipeline (Simplified)

```
Your C++ code sends vertex data to the GPU
          │
          ▼
   ┌──────────────┐
   │ Vertex Shader │  ← Runs once per vertex. Positions the vertex.
   └──────┬───────┘
          │
          ▼
   ┌──────────────┐
   │ Rasterisation │  ← GPU figures out which pixels the triangle covers
   └──────┬───────┘
          │
          ▼
   ┌────────────────┐
   │ Fragment Shader │  ← Runs once per pixel. Decides the colour.
   └──────┬─────────┘
          │
          ▼
     Screen output
```

You provide the vertex data (positions, colours, etc.) and the two shader programs. The GPU handles everything in between.

---

## Your First Shaders (GLSL)

Shaders are written in **GLSL** (OpenGL Shading Language) — a C-like language that runs on the GPU. Create two files:

### assets/shaders/basic.vert

```glsl
#version 460 core

// Input: vertex position (location 0 matches our C++ vertex data layout)
layout (location = 0) in vec3 aPos;

// Input: vertex colour (location 1)
layout (location = 1) in vec3 aColor;

// Output: pass colour to fragment shader
out vec3 vertexColor;

void main() {
    // gl_Position is a built-in variable — the final screen position
    // It's a vec4: x, y, z, w (w is for perspective division — 1.0 for now)
    gl_Position = vec4(aPos, 1.0);

    // Pass the colour through to the fragment shader
    vertexColor = aColor;
}
```

### assets/shaders/basic.frag

```glsl
#version 460 core

// Input: colour from vertex shader (interpolated across the triangle)
in vec3 vertexColor;

// Output: final pixel colour
out vec4 FragColor;

void main() {
    FragColor = vec4(vertexColor, 1.0);  // RGB from vertex, alpha = 1.0 (opaque)
}
```

### What's Happening

**Vertex shader:**
- Receives each vertex position (`aPos`) and colour (`aColor`) from your C++ code
- Outputs `gl_Position` (where on screen) and `vertexColor` (passed to fragment shader)
- `layout (location = 0)` matches the attribute index we'll set up in C++

**Fragment shader:**
- Receives `vertexColor` — but here's the magic: the GPU **interpolates** the colour across the triangle. If one vertex is red and another is green, pixels between them blend smoothly.
- Outputs `FragColor` — the final RGBA colour for that pixel

> **Note:** If your GPU only supports OpenGL 3.3, change `#version 460 core` to `#version 330 core` in both files.

---

## Building a Shader Class

We need C++ code to: read the shader files, compile them on the GPU, link them into a program, and activate them. Let's build a reusable class.

### src/engine/renderer/shader.h

```cpp
#pragma once

#include <glad/glad.h>
#include <string>

class Shader {
public:
    // Load, compile, and link a vertex + fragment shader pair
    Shader(const std::string& vertexPath, const std::string& fragmentPath);
    ~Shader();

    // Activate this shader for subsequent draw calls
    void use() const;

    // Get the OpenGL program ID (needed for setting uniforms later)
    unsigned int getId() const { return m_programId; }

private:
    unsigned int m_programId;

    // Helper: read a file into a string
    std::string readFile(const std::string& path) const;

    // Helper: compile a single shader and return its ID
    unsigned int compileShader(const std::string& source, GLenum type) const;

    // Helper: check for compilation/linking errors
    void checkErrors(unsigned int shader, const std::string& type) const;
};
```

### src/engine/renderer/shader.cpp

```cpp
#include "engine/renderer/shader.h"
#include <fstream>
#include <sstream>
#include <iostream>

Shader::Shader(const std::string& vertexPath, const std::string& fragmentPath) {
    // 1. Read shader source code from files
    std::string vertexSource = readFile(vertexPath);
    std::string fragmentSource = readFile(fragmentPath);

    // 2. Compile each shader
    unsigned int vertexShader = compileShader(vertexSource, GL_VERTEX_SHADER);
    unsigned int fragmentShader = compileShader(fragmentSource, GL_FRAGMENT_SHADER);

    // 3. Link them into a shader program
    m_programId = glCreateProgram();
    glAttachShader(m_programId, vertexShader);
    glAttachShader(m_programId, fragmentShader);
    glLinkProgram(m_programId);
    checkErrors(m_programId, "PROGRAM");

    // 4. Individual shaders are no longer needed after linking
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
}

Shader::~Shader() {
    glDeleteProgram(m_programId);
}

void Shader::use() const {
    glUseProgram(m_programId);
}

std::string Shader::readFile(const std::string& path) const {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "ERROR: Could not open shader file: " << path << std::endl;
        return "";
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

unsigned int Shader::compileShader(const std::string& source, GLenum type) const {
    unsigned int shader = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    std::string typeName = (type == GL_VERTEX_SHADER) ? "VERTEX" : "FRAGMENT";
    checkErrors(shader, typeName);

    return shader;
}

void Shader::checkErrors(unsigned int shader, const std::string& type) const {
    int success;
    char infoLog[1024];

    if (type == "PROGRAM") {
        glGetProgramiv(shader, GL_LINK_STATUS, &success);
        if (!success) {
            glGetProgramInfoLog(shader, 1024, nullptr, infoLog);
            std::cerr << "ERROR: Shader program linking failed\n" << infoLog << std::endl;
        }
    } else {
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(shader, 1024, nullptr, infoLog);
            std::cerr << "ERROR: " << type << " shader compilation failed\n"
                      << infoLog << std::endl;
        }
    }
}
```

### C++ Concepts Introduced

**`std::ifstream`** — a file input stream. Opens a file for reading. `file.rdbuf()` gives the entire file contents to the stringstream.

**`std::stringstream`** — a string that behaves like a stream. We use it to slurp the whole file into a `std::string` in one go.

**`const char*`** — a pointer to a C-style string (null-terminated character array). OpenGL's C API doesn't understand `std::string`, so we call `.c_str()` to get the raw pointer.

**`GLenum`** — an OpenGL-defined type alias for `unsigned int`. Used for constants like `GL_VERTEX_SHADER`.

---

## Rendering a Triangle

Now we have shaders. To actually draw something, we need to:
1. Define vertex data (positions + colours)
2. Upload it to the GPU
3. Tell OpenGL how to interpret it
4. Draw it

### The Vertex Data

A triangle has 3 vertices. Each vertex has a position (x, y, z) and a colour (r, g, b):

```cpp
float vertices[] = {
    // Positions          // Colours
    -0.5f, -0.5f, 0.0f,  1.0f, 0.0f, 0.0f,  // Bottom-left  (red)
     0.5f, -0.5f, 0.0f,  0.0f, 1.0f, 0.0f,  // Bottom-right (green)
     0.0f,  0.5f, 0.0f,  0.0f, 0.0f, 1.0f   // Top          (blue)
};
```

### VAO, VBO — What Are These?

To get vertex data onto the GPU, OpenGL uses:

- **VBO (Vertex Buffer Object)** — a chunk of GPU memory holding our vertex data
- **VAO (Vertex Array Object)** — a "configuration object" that remembers how to interpret the VBO data (what's a position, what's a colour, etc.)

Think of it like this:
- **VBO** = "here's a blob of numbers on the GPU"
- **VAO** = "here's how to read that blob: first 3 floats are position, next 3 are colour"

### Setting It Up

Add this to `main.cpp`, after creating the window but before the game loop:

```cpp
    // ─── Shader ──────────────────────────────────────────────────
    Shader basicShader("assets/shaders/basic.vert", "assets/shaders/basic.frag");

    // ─── Triangle vertex data ────────────────────────────────────
    float vertices[] = {
        // Positions          // Colours
        -0.5f, -0.5f, 0.0f,  1.0f, 0.0f, 0.0f,  // Bottom-left  (red)
         0.5f, -0.5f, 0.0f,  0.0f, 1.0f, 0.0f,  // Bottom-right (green)
         0.0f,  0.5f, 0.0f,  0.0f, 0.0f, 1.0f   // Top          (blue)
    };

    // ─── Create VAO and VBO ──────────────────────────────────────
    unsigned int VAO, VBO;

    glGenVertexArrays(1, &VAO);  // Generate 1 VAO
    glGenBuffers(1, &VBO);        // Generate 1 VBO

    // Bind the VAO first — it will "record" subsequent VBO and attribute config
    glBindVertexArray(VAO);

    // Bind the VBO and upload vertex data to GPU
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    // Tell OpenGL how to interpret the vertex data:

    // Attribute 0: Position (3 floats, starting at offset 0)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // Attribute 1: Colour (3 floats, starting at offset 3 floats in)
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // Unbind (optional, for safety)
    glBindVertexArray(0);
```

### Understanding glVertexAttribPointer

This is the most confusing OpenGL call for beginners. Let's break down the position attribute:

```cpp
glVertexAttribPointer(
    0,                    // Attribute index (matches layout(location = 0) in shader)
    3,                    // Number of components (vec3 = 3 floats)
    GL_FLOAT,             // Data type
    GL_FALSE,             // Normalise? No
    6 * sizeof(float),    // Stride: bytes between consecutive vertices (6 floats total)
    (void*)0              // Offset: where this attribute starts within each vertex
);
```

Visually, each vertex in the array looks like this:

```
[posX, posY, posZ, colR, colG, colB]
 ◄── position ──►  ◄── colour ──►
 offset: 0          offset: 3 floats
 ◄──────── stride: 6 floats ────────►
```

### Drawing in the Game Loop

Replace the clear/swap section in the game loop:

```cpp
        // Clear the screen
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // Draw the triangle
        basicShader.use();           // Activate our shader
        glBindVertexArray(VAO);      // Bind our vertex data
        glDrawArrays(GL_TRIANGLES, 0, 3);  // Draw 3 vertices as a triangle

        window.swapBuffers();
```

### Cleanup After the Game Loop

Add before `return 0`:

```cpp
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
```

### Updated CMakeLists.txt

```cmake
add_executable(QEngine
    src/main.cpp
    src/engine/core/window.cpp
    src/engine/renderer/shader.cpp
)
```

---

## The Full Updated main.cpp

```cpp
// src/main.cpp
#include "engine/core/window.h"
#include "engine/renderer/shader.h"
#include <iostream>

int main() {
    Window window(1280, 720, "QEngine");

    // ─── Shader ──────────────────────────────────────────────────
    Shader basicShader("assets/shaders/basic.vert", "assets/shaders/basic.frag");

    // ─── Triangle vertex data ────────────────────────────────────
    float vertices[] = {
        // Positions          // Colours
        -0.5f, -0.5f, 0.0f,  1.0f, 0.0f, 0.0f,
         0.5f, -0.5f, 0.0f,  0.0f, 1.0f, 0.0f,
         0.0f,  0.5f, 0.0f,  0.0f, 0.0f, 1.0f
    };

    // ─── Create VAO and VBO ──────────────────────────────────────
    unsigned int VAO, VBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    // Position attribute (location = 0)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // Colour attribute (location = 1)
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);

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

        // Clear
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // Draw
        basicShader.use();
        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        window.swapBuffers();
    }

    // ─── Cleanup ─────────────────────────────────────────────────
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);

    return 0;
}
```

---

## Expected Result

A window with a smooth-gradient triangle — red at the bottom-left, green at the bottom-right, blue at the top, with colours blending across the surface. The GPU is interpolating the vertex colours automatically.

This is the foundation of all rendering. Every 3D model, every level wall, every particle effect is ultimately just triangles with shaders.

---

## What's Next

In **Chapter 3**, we'll introduce the ECS — setting up EnTT, defining our first components, and writing our first systems. By the end of that chapter, the triangle will be an **entity** with `Position` and `MeshRenderer` components, rendered by a `RenderSystem` instead of hard-coded draw calls.
