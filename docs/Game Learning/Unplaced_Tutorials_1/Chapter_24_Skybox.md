# Chapter 24: Skybox

## What You'll Learn
- What a cubemap texture is and how it differs from a 2D texture
- Loading six images into a single `GL_TEXTURE_CUBE_MAP` with stb_image
- Building a unit cube mesh that uses vertex positions as texture coordinates
- Writing shaders that strip translation from the view matrix so the sky never moves
- The depth trick that places the skybox behind all other geometry
- Packaging it all into a reusable `Skybox` class
- Where the skybox fits in QEngine's render order

---

## Why a Skybox?

Right now QEngine clears to a flat colour every frame. The world ends abruptly at the edges of your level geometry. A **skybox** wraps the entire scene in a textured cube that represents the distant environment — mountains, clouds, stars, whatever fits your game. It's one of the highest visual-impact features for the least amount of code.

The idea is simple: render a cube around the camera, texture it with a panoramic image, and make sure it's always behind everything else.

```
        +-------+
       /|      /|
      / |  +Y / |       The camera sits at the centre.
     +-------+  |       Each face of the cube shows a
     |  +----|-+        different part of the sky.
     | /  -Z | /
     |/      |/         No matter which way you look,
     +-------+          you see sky.
```

---

## Cubemap Textures

A regular 2D texture is a flat image sampled with `(u, v)` coordinates. A **cubemap** is six 2D textures arranged as the faces of a cube, sampled with a **3D direction vector** instead of UV coordinates.

### The Six Faces

| OpenGL Target                  | Face | Direction  |
|-------------------------------|------|------------|
| `GL_TEXTURE_CUBE_MAP_POSITIVE_X` | Right  | +X |
| `GL_TEXTURE_CUBE_MAP_NEGATIVE_X` | Left   | -X |
| `GL_TEXTURE_CUBE_MAP_POSITIVE_Y` | Top    | +Y |
| `GL_TEXTURE_CUBE_MAP_NEGATIVE_Y` | Bottom | -Y |
| `GL_TEXTURE_CUBE_MAP_POSITIVE_Z` | Front  | +Z |
| `GL_TEXTURE_CUBE_MAP_NEGATIVE_Z` | Back   | -Z |

When you sample a cubemap with a direction vector `(x, y, z)`, OpenGL finds which face the vector points at, then looks up the texel on that face. This is exactly what we need — a vertex on our skybox cube has a position like `(1, 0.5, -0.3)`, and that position *is* the direction from the centre. We use it directly as the texture coordinate.

```
           +------+
           |  +Y  |
           | (top)|
    +------+------+------+------+
    |  -X  |  +Z  |  +X  |  -Z  |
    |(left)|(front)|(right)|(back)|
    +------+------+------+------+
           |  -Y  |
           |(bot) |
           +------+

   Standard cubemap unfolded layout (cross shape)
```

### Cubemap vs 2D Texture

| Property | 2D Texture | Cubemap |
|----------|-----------|---------|
| Binding target | `GL_TEXTURE_2D` | `GL_TEXTURE_CUBE_MAP` |
| Coordinate type | `vec2 (u, v)` | `vec3 (direction)` |
| Number of images | 1 | 6 |
| GLSL sampler | `sampler2D` | `samplerCube` |
| GLSL lookup | `texture(tex, uv)` | `texture(tex, direction)` |
| Typical use | Model surfaces | Skyboxes, reflections |

---

## Loading a Cubemap

We need to load six images and upload them to a single cubemap texture object. The faces must be loaded in the order OpenGL expects.

### src/engine/renderer/cubemap_loader.h

```cpp
#pragma once

#include <glad/glad.h>
#include <array>
#include <string>

// Load six images into a cubemap texture.
// Order: +X, -X, +Y, -Y, +Z, -Z
// Returns the OpenGL texture ID, or 0 on failure.
GLuint loadCubemap(const std::array<std::string, 6>& faces);
```

### src/engine/renderer/cubemap_loader.cpp

```cpp
#include "engine/renderer/cubemap_loader.h"

#include <stb_image.h>
#include <iostream>

GLuint loadCubemap(const std::array<std::string, 6>& faces) {
    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);

    // Each face target in the order OpenGL defines them.
    // GL_TEXTURE_CUBE_MAP_POSITIVE_X is the base value;
    // adding 0..5 gives +X, -X, +Y, -Y, +Z, -Z.
    for (unsigned int i = 0; i < 6; i++) {
        int width, height, channels;
        unsigned char* data = stbi_load(
            faces[i].c_str(), &width, &height, &channels, 0);

        if (!data) {
            std::cerr << "ERROR: Failed to load cubemap face: "
                      << faces[i] << std::endl;
            stbi_image_free(data);
            glDeleteTextures(1, &textureID);
            return 0;
        }

        GLenum format = (channels == 4) ? GL_RGBA : GL_RGB;

        glTexImage2D(
            GL_TEXTURE_CUBE_MAP_POSITIVE_X + i,  // Target face
            0,                                     // Mip level
            format,                                // Internal format
            width, height,
            0,                                     // Border (must be 0)
            format,                                // Source format
            GL_UNSIGNED_BYTE,                      // Source data type
            data                                   // Pixel data
        );

        stbi_image_free(data);
    }

    // Filtering — linear gives smooth blending between texels
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Clamp to edge — prevents seams where cube faces meet
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    return textureID;
}
```

### Key Details

- **Face ordering**: `GL_TEXTURE_CUBE_MAP_POSITIVE_X + i` gives us all six targets in sequence. This is guaranteed by the OpenGL spec — the six enum values are consecutive.
- **GL_CLAMP_TO_EDGE**: Without this, you get visible seams at the cube edges where texture filtering bleeds in the border colour.
- **GL_TEXTURE_WRAP_R**: Cubemaps have a third texture coordinate axis (R), so we clamp that too.
- **stbi_load**: We use `0` for the desired channels parameter, letting stb_image detect the format. Most skybox textures are RGB (3 channels), but we handle RGBA (4 channels) just in case.

---

## The Skybox Mesh

The skybox is a unit cube centred at the origin. We need 36 vertices (6 faces, 2 triangles each, 3 vertices per triangle). We do **not** need UV coordinates — the vertex position itself is the direction vector we pass to the cubemap sampler.

### Vertex Data

Each vertex is just a 3D position. No normals, no UVs. Add this to `src/engine/renderer/skybox.cpp` (or wherever you define the Skybox class):

```cpp
// 36 vertices — a unit cube from (-1,-1,-1) to (1,1,1)
// Winding order is inward-facing (we're inside the cube looking out)
constexpr float skyboxVertices[] = {
    // Back face (-Z)
    -1.0f,  1.0f, -1.0f,
    -1.0f, -1.0f, -1.0f,
     1.0f, -1.0f, -1.0f,
     1.0f, -1.0f, -1.0f,
     1.0f,  1.0f, -1.0f,
    -1.0f,  1.0f, -1.0f,

    // Front face (+Z)
    -1.0f, -1.0f,  1.0f,
    -1.0f,  1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,
     1.0f, -1.0f,  1.0f,
    -1.0f, -1.0f,  1.0f,

    // Left face (-X)
    -1.0f,  1.0f,  1.0f,
    -1.0f,  1.0f, -1.0f,
    -1.0f, -1.0f, -1.0f,
    -1.0f, -1.0f, -1.0f,
    -1.0f, -1.0f,  1.0f,
    -1.0f,  1.0f,  1.0f,

    // Right face (+X)
     1.0f,  1.0f, -1.0f,
     1.0f,  1.0f,  1.0f,
     1.0f, -1.0f,  1.0f,
     1.0f, -1.0f,  1.0f,
     1.0f, -1.0f, -1.0f,
     1.0f,  1.0f, -1.0f,

    // Top face (+Y)
    -1.0f,  1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,
     1.0f,  1.0f, -1.0f,
     1.0f,  1.0f, -1.0f,
    -1.0f,  1.0f, -1.0f,
    -1.0f,  1.0f,  1.0f,

    // Bottom face (-Y)
    -1.0f, -1.0f, -1.0f,
    -1.0f, -1.0f,  1.0f,
     1.0f, -1.0f,  1.0f,
     1.0f, -1.0f,  1.0f,
     1.0f, -1.0f, -1.0f,
    -1.0f, -1.0f, -1.0f,
};
```

### Why Inward-Facing?

Normal cubes have outward-facing triangles — you look at the outside. The skybox is different: the camera is **inside** the cube looking out. So the winding order is reversed compared to a normal cube, making the front faces point inward.

---

## Skybox Shaders

The skybox needs two tricks in its shaders:

1. **Strip translation from the view matrix** — so the skybox moves with the camera (the player can never walk towards the sky)
2. **Force maximum depth** — so the skybox is always behind every other object

### assets/shaders/skybox.vert

```glsl
#version 460 core

layout (location = 0) in vec3 aPos;

out vec3 TexCoords;

uniform mat4 projection;
uniform mat4 view;

void main() {
    // Pass position as the cubemap sample direction
    TexCoords = aPos;

    // Remove translation from the view matrix.
    // The upper-left 3x3 contains rotation only.
    // Converting to mat3 strips the 4th column (translation),
    // then converting back to mat4 fills it with zeros.
    mat4 rotationOnly = mat4(mat3(view));

    vec4 pos = projection * rotationOnly * vec4(aPos, 1.0);

    // Force z = w so that after perspective divide z/w = 1.0
    // This places the skybox at the maximum depth value.
    gl_Position = pos.xyww;
}
```

### assets/shaders/skybox.frag

```glsl
#version 460 core

in vec3 TexCoords;

out vec4 FragColour;

uniform samplerCube skybox;

void main() {
    FragColour = texture(skybox, TexCoords);
}
```

### Why `mat4(mat3(view))`?

The view matrix encodes two things: the camera's rotation and its translation (position in the world). If we kept the translation, moving the camera would move the skybox too — you could walk past the clouds. By converting to `mat3` (which drops the 4th column and row), we keep only the rotation. Converting back to `mat4` pads with zeros and a `1.0` in the bottom-right corner.

```
Full view matrix:          After mat4(mat3(view)):

┌ R R R Tx ┐              ┌ R R R 0 ┐
│ R R R Ty │      →       │ R R R 0 │
│ R R R Tz │              │ R R R 0 │
└ 0 0 0  1 ┘              └ 0 0 0 1 ┘

R = rotation values          Translation removed
Tx,Ty,Tz = camera position   Skybox stays centred on camera
```

### Why `pos.xyww`?

After the vertex shader, OpenGL performs the **perspective divide**: it divides `gl_Position.xyz` by `gl_Position.w`. The resulting `z` value becomes the depth buffer entry. By setting `gl_Position.z = gl_Position.w`, the divide produces `z/w = w/w = 1.0` — the maximum depth value in OpenGL's `[0, 1]` range. Every other object in the scene will have a smaller depth, so the skybox is always behind everything.

---

## The Depth Trick

We render the skybox **first**, before any opaque geometry. But we need to make sure it doesn't block anything drawn after it. There are two common approaches:

### Approach 1: Render First with `GL_LEQUAL` (Our Approach)

1. Render the skybox with `gl_Position = pos.xyww` (depth = 1.0 everywhere)
2. Set `glDepthFunc(GL_LEQUAL)` so that scene geometry (depth < 1.0) passes the depth test against the skybox
3. Restore `glDepthFunc(GL_LESS)` after drawing the skybox

This is the cleanest approach. The skybox writes depth 1.0 everywhere, and everything else has depth less than 1.0, so it draws over the skybox.

### Approach 2: Render Last with Depth Write Disabled

1. Render all scene geometry first
2. Disable depth writes with `glDepthMask(GL_FALSE)`
3. Render the skybox — it fills only the pixels where nothing was drawn
4. Re-enable depth writes with `glDepthMask(GL_TRUE)`

Both work. We'll use **Approach 1** because it's simpler and lets us draw the skybox in a single early step.

---

## Skybox Class

This class owns the cubemap texture and the VAO. It has a single `render()` method.

### src/engine/renderer/skybox.h

```cpp
#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <array>
#include <string>

class Shader;

class Skybox {
public:
    Skybox() = default;
    ~Skybox();

    // Non-copyable (owns GPU resources)
    Skybox(const Skybox&) = delete;
    Skybox& operator=(const Skybox&) = delete;

    // Movable
    Skybox(Skybox&& other) noexcept;
    Skybox& operator=(Skybox&& other) noexcept;

    // Load a skybox from six image paths.
    // Order: +X (right), -X (left), +Y (top), -Y (bottom), +Z (front), -Z (back)
    bool load(const std::array<std::string, 6>& facePaths);

    // Render the skybox.
    // Pass the camera's view and projection matrices.
    void render(const Shader& shader,
                const glm::mat4& view,
                const glm::mat4& projection) const;

    // Is this skybox ready to render?
    bool isLoaded() const { return m_textureID != 0; }

private:
    GLuint m_VAO       = 0;
    GLuint m_VBO       = 0;
    GLuint m_textureID = 0;

    void setupMesh();
    void cleanup();
};
```

### src/engine/renderer/skybox.cpp

```cpp
#include "engine/renderer/skybox.h"
#include "engine/renderer/cubemap_loader.h"
#include "engine/renderer/shader.h"

#include <utility>

// ─── Vertex data ────────────────────────────────────────────────

static constexpr float skyboxVertices[] = {
    // Back face (-Z)
    -1.0f,  1.0f, -1.0f,
    -1.0f, -1.0f, -1.0f,
     1.0f, -1.0f, -1.0f,
     1.0f, -1.0f, -1.0f,
     1.0f,  1.0f, -1.0f,
    -1.0f,  1.0f, -1.0f,

    // Front face (+Z)
    -1.0f, -1.0f,  1.0f,
    -1.0f,  1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,
     1.0f, -1.0f,  1.0f,
    -1.0f, -1.0f,  1.0f,

    // Left face (-X)
    -1.0f,  1.0f,  1.0f,
    -1.0f,  1.0f, -1.0f,
    -1.0f, -1.0f, -1.0f,
    -1.0f, -1.0f, -1.0f,
    -1.0f, -1.0f,  1.0f,
    -1.0f,  1.0f,  1.0f,

    // Right face (+X)
     1.0f,  1.0f, -1.0f,
     1.0f,  1.0f,  1.0f,
     1.0f, -1.0f,  1.0f,
     1.0f, -1.0f,  1.0f,
     1.0f, -1.0f, -1.0f,
     1.0f,  1.0f, -1.0f,

    // Top face (+Y)
    -1.0f,  1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,
     1.0f,  1.0f, -1.0f,
     1.0f,  1.0f, -1.0f,
    -1.0f,  1.0f, -1.0f,
    -1.0f,  1.0f,  1.0f,

    // Bottom face (-Y)
    -1.0f, -1.0f, -1.0f,
    -1.0f, -1.0f,  1.0f,
     1.0f, -1.0f,  1.0f,
     1.0f, -1.0f,  1.0f,
     1.0f, -1.0f, -1.0f,
    -1.0f, -1.0f, -1.0f,
};

// ─── Lifecycle ──────────────────────────────────────────────────

Skybox::~Skybox() {
    cleanup();
}

Skybox::Skybox(Skybox&& other) noexcept
    : m_VAO(other.m_VAO),
      m_VBO(other.m_VBO),
      m_textureID(other.m_textureID)
{
    other.m_VAO       = 0;
    other.m_VBO       = 0;
    other.m_textureID = 0;
}

Skybox& Skybox::operator=(Skybox&& other) noexcept {
    if (this != &other) {
        cleanup();
        m_VAO       = other.m_VAO;
        m_VBO       = other.m_VBO;
        m_textureID = other.m_textureID;
        other.m_VAO       = 0;
        other.m_VBO       = 0;
        other.m_textureID = 0;
    }
    return *this;
}

void Skybox::cleanup() {
    if (m_textureID) {
        glDeleteTextures(1, &m_textureID);
        m_textureID = 0;
    }
    if (m_VAO) {
        glDeleteVertexArrays(1, &m_VAO);
        m_VAO = 0;
    }
    if (m_VBO) {
        glDeleteBuffers(1, &m_VBO);
        m_VBO = 0;
    }
}

// ─── Loading ────────────────────────────────────────────────────

bool Skybox::load(const std::array<std::string, 6>& facePaths) {
    cleanup();

    m_textureID = loadCubemap(facePaths);
    if (m_textureID == 0) {
        return false;
    }

    setupMesh();
    return true;
}

void Skybox::setupMesh() {
    glGenVertexArrays(1, &m_VAO);
    glGenBuffers(1, &m_VBO);

    glBindVertexArray(m_VAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(skyboxVertices),
                 skyboxVertices, GL_STATIC_DRAW);

    // Position attribute — layout(location = 0), 3 floats
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                          3 * sizeof(float), (void*)0);

    glBindVertexArray(0);
}

// ─── Rendering ──────────────────────────────────────────────────

void Skybox::render(const Shader& shader,
                    const glm::mat4& view,
                    const glm::mat4& projection) const {
    if (!isLoaded()) return;

    // Change depth function so the skybox passes at depth 1.0
    glDepthFunc(GL_LEQUAL);

    shader.use();
    shader.setMat4("view", view);
    shader.setMat4("projection", projection);

    glBindVertexArray(m_VAO);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_textureID);

    glDrawArrays(GL_TRIANGLES, 0, 36);

    glBindVertexArray(0);

    // Restore default depth function
    glDepthFunc(GL_LESS);
}
```

### Usage

```cpp
// During initialisation (e.g. in LoadingState or at startup)
Skybox skybox;
skybox.load({
    "assets/skybox/right.jpg",    // +X
    "assets/skybox/left.jpg",     // -X
    "assets/skybox/top.jpg",      // +Y
    "assets/skybox/bottom.jpg",   // -Y
    "assets/skybox/front.jpg",    // +Z
    "assets/skybox/back.jpg"      // -Z
});

// During rendering
skybox.render(skyboxShader, camera.getViewMatrix(), camera.getProjectionMatrix());
```

---

## Integration: Render Order

The skybox is drawn **first**, before opaque geometry. Because its depth is always 1.0 (the maximum), everything drawn after it will pass the depth test and cover it.

Here is the updated render order for QEngine:

| Step | What | Depth Write | Depth Test | Notes |
|------|------|-------------|------------|-------|
| 1 | Clear colour and depth buffers | -- | -- | `glClear(...)` |
| 2 | **Skybox** | Yes | `GL_LEQUAL` | Fills background at depth 1.0 |
| 3 | Opaque geometry | Yes | `GL_LESS` | World, enemies, items |
| 4 | Transparent geometry | No | `GL_LESS` | Windows, particles (back-to-front) |
| 5 | HUD / UI | No | Disabled | Drawn in screen space |

### Updated PlayingState::render()

```cpp
void PlayingState::render() {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glm::mat4 view = m_camera.getViewMatrix();
    glm::mat4 projection = m_camera.getProjectionMatrix();

    // 1. Skybox — drawn first, always at maximum depth
    m_skybox.render(m_skyboxShader, view, projection);

    // 2. Opaque geometry
    renderSystem(m_registry, m_camera);

    // 3. Transparent geometry (particles, etc.)
    particleSystem(m_registry, m_camera);

    // 4. HUD (screen-space, no depth)
    hudSystem(m_registry, m_window);
}
```

### Where Does the Skybox Live?

The `Skybox` object is stored as a member of whatever owns the render loop — typically the `PlayingState` or a shared `RenderContext`. It is **not** an ECS entity. A skybox is engine infrastructure: a shared resource that the render system uses, like the shader cache or the camera. There's exactly one skybox per scene, it has no position or velocity, and no system iterates over it. It doesn't belong in the ECS registry.

> **ECS note**: Components have no behaviour. Systems have no state. The skybox is neither — it's a shared resource, like the window or the audio manager. The render function uses it, but it doesn't own it.

---

## C++ Concept: `std::array`

Throughout this chapter we've been passing `std::array<std::string, 6>` for the cubemap face paths. Let's look at what `std::array` gives us over raw C arrays.

### Raw Array

```cpp
// C-style array
std::string faces[6] = {
    "right.jpg", "left.jpg", "top.jpg",
    "bottom.jpg", "front.jpg", "back.jpg"
};

void load(std::string faces[6]);  // Decays to pointer — size is lost!
```

The problem: when you pass a raw array to a function, it **decays** to a pointer. The function has no idea how many elements there are. The `[6]` in the parameter is just documentation — the compiler ignores it.

### `std::array`

```cpp
#include <array>

std::array<std::string, 6> faces = {
    "right.jpg", "left.jpg", "top.jpg",
    "bottom.jpg", "front.jpg", "back.jpg"
};

void load(const std::array<std::string, 6>& faces);  // Size is part of the type!
```

`std::array<T, N>` is a fixed-size container that wraps a raw array. It does not decay, the size is part of the type, and you get all the standard container features:

| Feature | Raw array `T[N]` | `std::array<T, N>` |
|---------|------------------|---------------------|
| Knows its own size | No (decays to pointer) | Yes (`.size()`) |
| Bounds-checked access | No | Yes (`.at(i)` throws on out-of-bounds) |
| Works with range-for | Yes | Yes |
| Assignable / copyable | No | Yes |
| Comparable (`==`, `<`) | No | Yes |
| Pass by value / reference | Decays to `T*` | Stays `std::array` |
| Overhead vs raw array | -- | Zero (same memory layout) |

`std::array` has **zero overhead** compared to a raw array — it's the same contiguous block of memory. The size `N` is a compile-time template parameter, not stored at runtime. Use `std::array` whenever you have a fixed-size collection known at compile time.

```cpp
// Iterating
for (const auto& face : faces) {
    std::cout << face << std::endl;
}

// Size
std::cout << faces.size() << std::endl;  // 6

// Bounds-checked access (throws std::out_of_range)
faces.at(7);  // throws!

// Unchecked access (same as raw array — fast, no bounds check)
faces[0];     // "right.jpg"
```

---

## Where to Get Skybox Textures

You need six square images that tile seamlessly at the edges. Here are free sources:

- **learnopengl.com** — The LearnOpenGL cubemap tutorial includes a downloadable skybox. Good for testing.
- **OpenGameArt.org** — Search for "skybox" or "cubemap". Many CC0 and CC-BY licensed sets available.
- **Polyhaven (polyhaven.com)** — High-quality HDRIs that can be converted to cubemap faces. Download as HDRI, then use a tool like `cmft` or the Polyhaven website itself to export as six separate face images.
- **Humus.name** — A classic collection of free cubemaps in various resolutions.

### Naming Convention

Most skybox sets use one of these naming patterns:

```
right.jpg, left.jpg, top.jpg, bottom.jpg, front.jpg, back.jpg
```

or:

```
px.jpg, nx.jpg, py.jpg, ny.jpg, pz.jpg, nz.jpg
```

(`p` for positive, `n` for negative.)

Place them in a folder like `assets/skybox/` and load them in the correct order:

```cpp
skybox.load({
    "assets/skybox/px.jpg",   // +X  (right)
    "assets/skybox/nx.jpg",   // -X  (left)
    "assets/skybox/py.jpg",   // +Y  (top)
    "assets/skybox/ny.jpg",   // -Y  (bottom)
    "assets/skybox/pz.jpg",   // +Z  (front)
    "assets/skybox/nz.jpg"    // -Z  (back)
});
```

### Common Pitfall: Flipped or Rotated Faces

If your skybox looks wrong — seams don't align, the sky is mirrored, or faces are rotated — you likely have the face order wrong or the images are oriented differently than OpenGL expects. The most common fix: swap +Z and -Z, or flip images vertically. Unlike 2D textures, **do not** call `stbi_set_flip_vertically_on_load(true)` for cubemap faces — OpenGL's cubemap coordinate system expects the top-left origin that stb_image provides by default.

---

## Summary

What we built this chapter:

```
src/engine/renderer/cubemap_loader.h    — loadCubemap() declaration
src/engine/renderer/cubemap_loader.cpp  — loads 6 images into GL_TEXTURE_CUBE_MAP
src/engine/renderer/skybox.h            — Skybox class declaration
src/engine/renderer/skybox.cpp          — mesh setup, loading, rendering
assets/shaders/skybox.vert              — strips translation, forces max depth
assets/shaders/skybox.frag              — samples the cubemap
```

The total is about 150 lines of C++ and 20 lines of GLSL. For that, you get a full panoramic sky that makes the world feel vastly larger than it is. The skybox is a shared resource — not an ECS entity — rendered at the start of each frame before any scene geometry.

---

## What's Next

In **Chapter 25**, we'll add weapon animations and view models — the gun bobbing and swaying in front of the camera as the player moves. This is where the game starts to really *feel* like a Quake-style shooter.
