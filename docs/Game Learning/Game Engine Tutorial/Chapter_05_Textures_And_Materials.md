# Chapter 5: Textures & Materials

## What You'll Learn
- How textures work (images mapped onto geometry)
- Loading images with stb_image
- UV coordinates — how the GPU knows which pixel goes where
- Texture filtering and wrapping
- Building a Texture class
- Updating shaders and components for textured rendering

---

## What Is a Texture?

A texture is an image mapped onto the surface of geometry. Every wall, floor, enemy skin, and HUD element in Quake is a texture. Without textures, everything would be flat-coloured triangles.

The GPU needs to know two things:
1. **The image data** (pixels, uploaded to GPU memory)
2. **UV coordinates** (per vertex — which part of the image maps to which triangle)

---

## UV Coordinates

Every vertex has a **UV coordinate** — a 2D position on the texture where `(0,0)` is the bottom-left and `(1,1)` is the top-right:

```
(0,1) ─────────── (1,1)
  │                  │
  │    Texture       │
  │    Image         │
  │                  │
(0,0) ─────────── (1,0)
```

The GPU interpolates UVs across the triangle (just like it interpolated colours in Chapter 2) and samples the texture at each pixel.

---

## Setting Up stb_image

stb_image is a single-header library. We need to create one `.cpp` file that contains the implementation.

### src/engine/renderer/stb_image_impl.cpp

```cpp
// This file exists solely to compile the stb_image implementation.
// stb_image is a header-only library — you #include it everywhere,
// but the IMPLEMENTATION (actual code) must be compiled exactly once.
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
```

Add this to CMakeLists.txt in the source list:
```cmake
    src/engine/renderer/stb_image_impl.cpp
```

### C++ Concept: Header-Only Libraries and Implementation Macros

stb_image uses a common pattern for single-header C libraries:
- `#include "stb_image.h"` normally just gives you function **declarations**
- `#define STB_IMAGE_IMPLEMENTATION` before the include tells it to also include the function **definitions** (actual code)
- You do this in exactly **one** `.cpp` file. If you do it in two, you get "multiple definition" linker errors.

---

## Building a Texture Class

### src/engine/renderer/texture.h

```cpp
#pragma once

#include <glad/glad.h>
#include <string>

class Texture {
public:
    Texture(const std::string& path);
    ~Texture();

    // Bind this texture to a texture unit (0, 1, 2, etc.)
    void bind(unsigned int unit = 0) const;

    unsigned int getId() const { return m_textureId; }
    int getWidth() const { return m_width; }
    int getHeight() const { return m_height; }

private:
    unsigned int m_textureId;
    int m_width;
    int m_height;
    int m_channels;
};
```

### src/engine/renderer/texture.cpp

```cpp
#include "engine/renderer/texture.h"
#include "stb_image.h"
#include <iostream>

Texture::Texture(const std::string& path)
    : m_textureId(0), m_width(0), m_height(0), m_channels(0)
{
    // stb_image loads with (0,0) at top-left, OpenGL expects bottom-left
    stbi_set_flip_vertically_on_load(true);

    // Load the image
    unsigned char* data = stbi_load(path.c_str(), &m_width, &m_height, &m_channels, 0);
    if (!data) {
        std::cerr << "ERROR: Failed to load texture: " << path << std::endl;
        return;
    }

    // Determine format from channel count
    GLenum format = GL_RGB;
    if (m_channels == 1) format = GL_RED;
    else if (m_channels == 3) format = GL_RGB;
    else if (m_channels == 4) format = GL_RGBA;

    // Create OpenGL texture
    glGenTextures(1, &m_textureId);
    glBindTexture(GL_TEXTURE_2D, m_textureId);

    // Set wrapping behaviour (what happens when UVs go outside 0-1)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);  // horizontal
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);  // vertical

    // Set filtering (how to sample when texture is scaled)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Upload pixel data to GPU
    glTexImage2D(GL_TEXTURE_2D, 0, format, m_width, m_height, 0,
                 format, GL_UNSIGNED_BYTE, data);

    // Generate mipmaps (smaller versions for distant rendering)
    glGenerateMipmap(GL_TEXTURE_2D);

    // Free CPU-side image data — it's on the GPU now
    stbi_image_free(data);

    std::cout << "Loaded texture: " << path
              << " (" << m_width << "x" << m_height
              << ", " << m_channels << " channels)" << std::endl;
}

Texture::~Texture() {
    if (m_textureId) {
        glDeleteTextures(1, &m_textureId);
    }
}

void Texture::bind(unsigned int unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);  // Activate texture unit
    glBindTexture(GL_TEXTURE_2D, m_textureId);
}
```

---

## Texture Concepts Explained

### Wrapping Modes

When UV coordinates go outside the 0-1 range:

| Mode | Behaviour |
|------|-----------|
| `GL_REPEAT` | Tiles the texture (most common for game surfaces) |
| `GL_CLAMP_TO_EDGE` | Stretches the edge pixels |
| `GL_MIRRORED_REPEAT` | Tiles but mirrors every other repetition |

For a Quake-like game, `GL_REPEAT` is what you want — walls and floors tile their textures.

### Filtering Modes

When a texture is displayed larger or smaller than its actual pixel size:

| Mode | When | Result |
|------|------|--------|
| `GL_NEAREST` | Magnified | Pixelated look (retro/Doom style) |
| `GL_LINEAR` | Magnified | Smooth/blurry (modern look) |
| `GL_LINEAR_MIPMAP_LINEAR` | Minified | Smooth with mipmaps (best quality) |

For a Quake-style game, you might prefer `GL_NEAREST` for that classic pixelated look:
```cpp
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
```

### Mipmaps

Mipmaps are pre-generated smaller versions of the texture:

```
256x256 → 128x128 → 64x64 → 32x32 → 16x16 → 8x8 → 4x4 → 2x2 → 1x1
```

When a textured surface is far away and only covers a few pixels on screen, the GPU uses a smaller mipmap instead of sampling the full-resolution texture. This prevents shimmering/aliasing artifacts and is faster.

`glGenerateMipmap` creates all levels automatically.

### `unsigned char*` — Raw Pixel Data

```cpp
unsigned char* data = stbi_load(...);
```

`unsigned char` is an 8-bit value (0-255). The image data is a flat array of bytes:

```
For an RGB image:
[R, G, B, R, G, B, R, G, B, ...]
 pixel 0   pixel 1   pixel 2

For an RGBA image:
[R, G, B, A, R, G, B, A, ...]
 pixel 0      pixel 1
```

`stbi_load` allocates this memory. We must free it with `stbi_image_free` after uploading to the GPU.

### Texture Units

OpenGL can have multiple textures active simultaneously (for multi-texturing, lightmaps, etc.). Each "slot" is a **texture unit**:

```cpp
texture.bind(0);   // Bind to unit 0 (GL_TEXTURE0)
lightmap.bind(1);  // Bind to unit 1 (GL_TEXTURE1)
```

The shader accesses them by unit number. We'll use this for lightmaps in the lighting chapter.

---

## Updating the Shader for Textures

### assets/shaders/textured.vert

```glsl
#version 460 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;  // Changed: UV instead of colour

out vec2 TexCoord;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main() {
    gl_Position = projection * view * model * vec4(aPos, 1.0);
    TexCoord = aTexCoord;
}
```

### assets/shaders/textured.frag

```glsl
#version 460 core

in vec2 TexCoord;
out vec4 FragColor;

uniform sampler2D textureSampler;  // The texture, accessed by unit number

void main() {
    FragColor = texture(textureSampler, TexCoord);
}
```

`sampler2D` is a GLSL type representing a 2D texture. The `texture()` function samples it at the given UV coordinate.

---

## Updating Components

Add a texture ID to the `MeshRenderer` component in `components.h`:

```cpp
struct MeshRenderer {
    unsigned int vao = 0;
    unsigned int vertexCount = 0;
    unsigned int shaderId = 0;
    unsigned int textureId = 0;     // NEW: 0 means no texture
    bool useIndices = false;        // NEW: for indexed drawing (Chapter 6)
    unsigned int indexCount = 0;    // NEW: number of indices
};
```

---

## Updating the Render System

Update `render_system.cpp` to bind textures before drawing:

```cpp
void renderSystem(entt::registry& registry, const Camera& camera,
                  float aspectRatio) {

    glm::mat4 view = camera.getViewMatrix();
    glm::mat4 projection = camera.getProjectionMatrix(aspectRatio);

    auto viewGroup = registry.view<Position, MeshRenderer>();

    for (auto [entity, pos, mesh] : viewGroup.each()) {
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

        glUseProgram(mesh.shaderId);

        GLint modelLoc = glGetUniformLocation(mesh.shaderId, "model");
        GLint viewLoc  = glGetUniformLocation(mesh.shaderId, "view");
        GLint projLoc  = glGetUniformLocation(mesh.shaderId, "projection");
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, &model[0][0]);
        glUniformMatrix4fv(viewLoc,  1, GL_FALSE, &view[0][0]);
        glUniformMatrix4fv(projLoc,  1, GL_FALSE, &projection[0][0]);

        // Bind texture if present
        if (mesh.textureId != 0) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, mesh.textureId);
            glUniform1i(glGetUniformLocation(mesh.shaderId, "textureSampler"), 0);
        }

        glBindVertexArray(mesh.vao);

        if (mesh.useIndices) {
            glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, 0);
        } else {
            glDrawArrays(GL_TRIANGLES, 0, mesh.vertexCount);
        }
    }
}
```

---

## Updating main.cpp

All of the following changes go in `main.cpp`. We'll add a textured quad alongside the existing coloured triangle, so you can see both rendering systems working together.

### Include the Texture Header

At the top of `main.cpp`, add the new include alongside the existing ones:

```cpp
#include "engine/renderer/texture.h"   // NEW
```

### Add the Textured Shader

In `main.cpp`, right after the existing `basicShader` declaration, add the textured shader:

```cpp
// ─── Shaders ─────────────────────────────────────────────────
Shader basicShader(
    "assets/shaders/basic.vert",
    "assets/shaders/basic.frag"
);

Shader texturedShader(                                      // NEW
    "assets/shaders/textured.vert",                         // NEW
    "assets/shaders/textured.frag"                          // NEW
);                                                          // NEW
```

### Add the Texture

Right after the shader declarations, load the texture:

```cpp
// ─── Textures ────────────────────────────────────────────────
Texture wallTexture("assets/textures/wall.png");            // NEW
```

Place any PNG or JPG image at `assets/textures/wall.png` relative to your project root. The folder structure should look like:

```
QEngine/
  assets/
    shaders/
      basic.vert
      basic.frag
      textured.vert    ← new
      textured.frag    ← new
    textures/
      wall.png         ← any image (PNG or JPG)
```

### Quad Vertex Data with UVs

A quad (rectangle) is the basic surface for walls and floors. It's two triangles:

```
(0,1)───────(1,1)
  │ ╲         │
  │   ╲       │
  │     ╲     │
  │       ╲   │
  │         ╲ │
(0,0)───────(1,0)
```

Add this vertex data right after your existing triangle `vertices[]` array:

```cpp
// ─── Quad vertex data (textured) ─────────────────────────────
float quadVertices[] = {
    // Positions          // UV coords
    -0.5f, -0.5f, 0.0f,  0.0f, 0.0f,  // Bottom-left
     0.5f, -0.5f, 0.0f,  1.0f, 0.0f,  // Bottom-right
     0.5f,  0.5f, 0.0f,  1.0f, 1.0f,  // Top-right

    -0.5f, -0.5f, 0.0f,  0.0f, 0.0f,  // Bottom-left
     0.5f,  0.5f, 0.0f,  1.0f, 1.0f,  // Top-right
    -0.5f,  0.5f, 0.0f,  0.0f, 1.0f   // Top-left
};
```

Notice: 6 vertices for 2 triangles, but 2 vertices are duplicated (bottom-left and top-right appear twice). This wastes memory. In Chapter 6, we'll use **index buffers** to fix this.

### Setting Up the Quad VAO

Add this right after the existing triangle VAO/VBO setup (after `glBindVertexArray(0)`):

```cpp
// ─── Create quad VAO and VBO ─────────────────────────────────
unsigned int quadVAO, quadVBO;
glGenVertexArrays(1, &quadVAO);
glGenBuffers(1, &quadVBO);

glBindVertexArray(quadVAO);
glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

// Position: 3 floats, stride = 5 floats, offset = 0
glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
glEnableVertexAttribArray(0);

// UV: 2 floats, stride = 5 floats, offset = 3 floats
glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                      (void*)(3 * sizeof(float)));
glEnableVertexAttribArray(1);

glBindVertexArray(0);
```

### Creating the Textured Entity

Add this in the ECS entity creation section, after the existing triangle entities:

```cpp
// create a textured wall quad
auto wall = registry.create();
registry.emplace<Position>(wall, glm::vec3(0.0f, 0.0f, -2.0f));
registry.emplace<MeshRenderer>(wall, quadVAO, 6u, texturedShader.getId(),
                                wallTexture.getId(), false, 0u);
```

### Cleaning Up the Quad Buffers

At the end of `main()`, alongside the existing cleanup, add:

```cpp
glDeleteVertexArrays(1, &quadVAO);   // NEW
glDeleteBuffers(1, &quadVBO);         // NEW
```

---

## C++ Concept: enum class

We used `GLenum` (OpenGL's enum type) throughout this chapter. C++ has its own enum system:

```cpp
// Old C-style enum (values leak into surrounding scope)
enum Format { RED, RGB, RGBA };

// Modern C++ enum class (scoped — must use Format::RGB)
enum class Format { Red, RGB, RGBA };

Format f = Format::RGB;  // Clear which "RGB" this is
```

`enum class` is preferred in modern C++ because the values don't pollute the namespace. OpenGL uses C-style enums (`GL_RGB`) because it's a C API.

---

## Texture Tiling for Quake-Style Surfaces

For walls and floors in a Quake-like game, you'll want textures to tile. If a wall is 4 units wide and the texture covers 1 unit, the UVs should go from 0 to 4:

```cpp
float wallVertices[] = {
    // Positions            // UVs (tile 4x across, 2x vertically)
    -2.0f, -1.0f, 0.0f,    0.0f, 0.0f,
     2.0f, -1.0f, 0.0f,    4.0f, 0.0f,   // U = 4.0 → repeats 4 times
     2.0f,  1.0f, 0.0f,    4.0f, 2.0f,   // V = 2.0 → repeats 2 times
    // ... second triangle
};
```

With `GL_REPEAT` wrapping mode, this tiles the texture seamlessly — exactly how Quake's walls work.

---

## What's Next

In **Chapter 6**, we'll load real 3D models — reading OBJ files from disk, using index buffers to avoid duplicate vertices, and building a mesh loading system. We'll go from flat quads to actual 3D objects.
