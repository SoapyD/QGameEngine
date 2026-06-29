# Chapter 38: Instanced Rendering

## What You'll Learn
- Why issuing one draw call per object is the primary CPU-side bottleneck
- How `glDrawElementsInstanced` renders thousands of copies in a single call
- Setting up per-instance vertex attributes with `glVertexAttribDivisor`
- Passing a mat4 through 4 consecutive vec4 attribute slots
- An ECS-compliant `InstanceGroup` component and render system
- Static vs dynamic instance groups and the `dirty` flag pattern
- Combining instanced rendering with frustum culling (Ch 32)
- Extending per-instance data beyond transforms (colour tint, scale)
- Buffer update strategies: `glBufferData` vs `glBufferSubData` vs `glMapBufferRange`

---

## The Draw Call Problem

Every `glDrawElements` call forces the driver to validate state, translate the call into GPU commands, and push it into the command queue. This overhead is roughly constant regardless of triangle count. Drawing 1,000 identical crates means paying that overhead 1,000 times — even though the mesh and material never change.

```
Without instancing (1000 crates):

  CPU                                        GPU
  ────────────────────────────────────       ─────────────────────
  Bind shader                                (idle, waiting)
  Set uniforms (model matrix #1)
  glDrawElements ────────────────────────►   Draw 12 triangles
  Set uniforms (model matrix #2)
  glDrawElements ────────────────────────►   Draw 12 triangles
  ...  (x1000)                               ...
  Total: 1000 draw calls  ~4-8ms CPU overhead

With instancing (1000 crates):

  CPU                                        GPU
  ────────────────────────────────────       ─────────────────────
  Bind shader
  Upload 1000 matrices (one buffer)
  glDrawElementsInstanced ───────────────►   Draw 12 tri x 1000

  Total: 1 draw call  ~0.05ms CPU overhead
```

GPU time is nearly identical (same total triangles). The savings are entirely on the CPU side.

---

## How Instancing Works

Give the GPU one mesh and an array of per-instance data, then say "draw this mesh N times, using the i-th element for the i-th copy."

```
                      Per-Instance Data (VBO)
                      ┌───────────────────┐
                      │ Model Matrix #0   │
                      │ Model Matrix #1   │
  Mesh (VAO)          │ Model Matrix #2   │
  ┌──────────┐        │       ...         │
  │ Vertices │        │ Model Matrix #999 │
  │ Normals  │        └───────────────────┘
  │ UVs      │                 │
  │ Indices  │                 │
  └──────────┘                 │
       │                       │
       ▼                       ▼
  glDrawElementsInstanced(GL_TRIANGLES, indexCount,
                          GL_UNSIGNED_INT, nullptr, 1000);
       │
       ▼
  GPU draws mesh 1000 times, each with a different matrix
```

Per-instance data is passed as **vertex attributes** configured to advance once per instance rather than once per vertex. This avoids the size limits of uniform arrays.

---

## Instance Buffer Setup

A `mat4` is 64 bytes — four `vec4` columns. Since vertex attributes hold at most one `vec4`, we need **4 consecutive attribute slots**.

```
  mat4 in memory (column-major):

  Offset:  0         16        32        48
           ┌─────────┬─────────┬─────────┬─────────┐
           │ col 0   │ col 1   │ col 2   │ col 3   │
           │ (vec4)  │ (vec4)  │ (vec4)  │ (vec4)  │
           └─────────┴─────────┴─────────┴─────────┘
             attr 3    attr 4    attr 5    attr 6

  Mesh attributes:       Instance attributes:
    loc 0 = aPos           loc 3 = model col 0
    loc 1 = aNormal        loc 4 = model col 1
    loc 2 = aTexUV         loc 5 = model col 2
                           loc 6 = model col 3
```

The key call is `glVertexAttribDivisor(loc, 1)` — it tells OpenGL to advance that attribute once per **instance**, not once per vertex.

```cpp
// In src/engine/renderer/instance_buffer.h

#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>

namespace qe {

inline GLuint createInstanceBuffer(GLuint vao, std::size_t initialCapacity = 256) {
    GLuint instanceVBO = 0;
    glGenBuffers(1, &instanceVBO);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(initialCapacity * sizeof(glm::mat4)),
                 nullptr, GL_DYNAMIC_DRAW);

    for (int col = 0; col < 4; ++col) {
        GLuint loc = 3 + col;
        glEnableVertexAttribArray(loc);
        glVertexAttribPointer(loc, 4, GL_FLOAT, GL_FALSE,
                              sizeof(glm::mat4),
                              reinterpret_cast<void*>(sizeof(glm::vec4) * col));
        glVertexAttribDivisor(loc, 1);  // Per instance, not per vertex
    }

    glBindVertexArray(0);
    return instanceVBO;
}

inline void uploadInstanceData(GLuint instanceVBO,
                               const std::vector<glm::mat4>& matrices) {
    glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
    GLsizeiptr size = static_cast<GLsizeiptr>(matrices.size() * sizeof(glm::mat4));
    glBufferData(GL_ARRAY_BUFFER, size, nullptr, GL_DYNAMIC_DRAW);   // Orphan
    glBufferSubData(GL_ARRAY_BUFFER, 0, size, matrices.data());      // Upload
}

} // namespace qe
```

---

## Instanced Shader

The vertex shader receives the model matrix as a vertex attribute instead of a uniform. The fragment shader is unchanged.

```glsl
// In assets/shaders/instanced.vert
#version 330 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexUV;
layout (location = 3) in mat4 instanceModel;  // 4 vec4 slots (3-6)

uniform mat4 view;
uniform mat4 projection;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoord;

void main() {
    vec4 worldPos = instanceModel * vec4(aPos, 1.0);
    FragPos  = worldPos.xyz;
    Normal   = mat3(transpose(inverse(instanceModel))) * aNormal;
    TexCoord = aTexUV;
    gl_Position = projection * view * worldPos;
}
```

```glsl
// In assets/shaders/instanced.frag
#version 330 core

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoord;

uniform sampler2D diffuseTexture;
uniform vec3 lightDir;
uniform vec3 lightColour;
uniform vec3 ambientColour;

out vec4 FragColor;

void main() {
    vec3 norm = normalize(Normal);
    float diff = max(dot(norm, -lightDir), 0.0);
    vec3 lighting = ambientColour + lightColour * diff;
    vec4 texel = texture(diffuseTexture, TexCoord);
    FragColor = vec4(texel.rgb * lighting, texel.a);
}
```

The fragment shader has no idea instancing is happening — it receives interpolated varyings just like the non-instanced path.

---

## InstanceGroup Component

Pure data, no behaviour — following QEngine's ECS rules:

```cpp
// In src/engine/ecs/components/instance_group.h

#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace qe {

struct InstanceGroup {
    std::string meshName;
    std::string textureName;

    GLuint vao         = 0;
    GLuint instanceVBO = 0;
    int    indexCount   = 0;

    std::vector<glm::mat4> allTransforms;      // Full set (never modified after setup)
    std::vector<glm::mat4> visibleTransforms;   // Rebuilt each frame by culling
    int  instanceCount = 0;
    bool dirty         = true;

    bool isStatic           = false;
    std::size_t bufferCapacity = 0;
    float boundingRadius    = 1.0f;
};

} // namespace qe
```

Each `InstanceGroup` entity represents a batch of identical objects. A forest might have one entity for pine trees (2,000 instances) and another for oak trees (800 instances). Individual positions are baked into `allTransforms`.

---

## InstanceRenderSystem

The system uploads transforms when dirty and issues one instanced draw call per group:

```cpp
// In src/engine/ecs/systems/instance_render_system.h

#pragma once
#include <entt/entt.hpp>
#include <glad/glad.h>
#include "src/engine/ecs/components/instance_group.h"
#include "src/engine/renderer/instance_buffer.h"
#include "src/engine/renderer/shader.h"

namespace qe {

inline void instanceRenderSystem(entt::registry& registry,
                                 const Shader& shader,
                                 const glm::mat4& view,
                                 const glm::mat4& projection) {
    shader.use();
    shader.setMat4("view", view);
    shader.setMat4("projection", projection);

    for (auto [entity, group] : registry.view<InstanceGroup>().each()) {
        if (group.instanceCount == 0) continue;

        if (group.dirty) {
            if (static_cast<std::size_t>(group.instanceCount) > group.bufferCapacity) {
                group.bufferCapacity = static_cast<std::size_t>(group.instanceCount) * 2;
                glBindBuffer(GL_ARRAY_BUFFER, group.instanceVBO);
                glBufferData(GL_ARRAY_BUFFER,
                    static_cast<GLsizeiptr>(group.bufferCapacity * sizeof(glm::mat4)),
                    nullptr, group.isStatic ? GL_STATIC_DRAW : GL_DYNAMIC_DRAW);
            }
            glBindBuffer(GL_ARRAY_BUFFER, group.instanceVBO);
            glBufferSubData(GL_ARRAY_BUFFER, 0,
                static_cast<GLsizeiptr>(group.instanceCount * sizeof(glm::mat4)),
                group.visibleTransforms.data());
            group.dirty = false;
        }

        auto* texMgr = registry.ctx().find<TextureManager*>();
        if (texMgr && *texMgr) (*texMgr)->bind(group.textureName, 0);

        glBindVertexArray(group.vao);
        glDrawElementsInstanced(GL_TRIANGLES, group.indexCount,
                                GL_UNSIGNED_INT, nullptr, group.instanceCount);
    }
    glBindVertexArray(0);
}

} // namespace qe
```

If you have 3 types of vegetation, that's 3 draw calls total — regardless of how many individual plants exist.

---

## Dynamic vs Static Instances

**Static instances** are placed once and never move: decorative rocks, columns, tree trunks. Set `isStatic = true`, upload once with `GL_STATIC_DRAW`, and `dirty` stays `false` forever. The driver keeps this data in fast GPU-local memory.

**Dynamic instances** can move or disappear: projectiles, particles, debris. Set `isStatic = false`, use `GL_DYNAMIC_DRAW`, and mark `dirty = true` whenever transforms change.

```cpp
// In src/engine/ecs/systems/instance_setup_system.h

namespace qe {

/// Called once during level load to initialise instance groups.
inline void instanceSetupSystem(entt::registry& registry) {
    for (auto [entity, group] : registry.view<InstanceGroup>().each()) {
        if (group.instanceVBO != 0) continue;  // Already initialised

        auto* assets = registry.ctx().find<AssetManager*>();
        if (!assets || !*assets) continue;

        const Mesh* mesh = (*assets)->getMesh(group.meshName);
        if (!mesh) continue;

        group.vao        = mesh->vao;
        group.indexCount  = mesh->indexCount;
        group.instanceVBO = createInstanceBuffer(group.vao, group.allTransforms.size());
        group.bufferCapacity = group.allTransforms.size();
        group.visibleTransforms = group.allTransforms;
        group.instanceCount = static_cast<int>(group.allTransforms.size());
        group.dirty = true;
    }
}

} // namespace qe
```

The `GL_STATIC_DRAW` / `GL_DYNAMIC_DRAW` hints are advisory — using the wrong one won't crash, it will just be slower than necessary.

---

## Combining with Frustum Culling

Instead of skipping an entire draw call (Ch 32), we filter the transforms vector to include only visible instances:

```cpp
// In src/engine/ecs/systems/instance_cull_system.h

namespace qe {

inline void instanceCullSystem(entt::registry& registry,
                               const Frustum& frustum) {
    for (auto [entity, group] : registry.view<InstanceGroup>().each()) {
        group.visibleTransforms.clear();

        for (const auto& mat : group.allTransforms) {
            glm::vec3 pos(mat[3]);  // Extract position from column 3
            if (frustum.testSphere(pos, group.boundingRadius)) {
                group.visibleTransforms.push_back(mat);
            }
        }

        int newCount = static_cast<int>(group.visibleTransforms.size());
        if (newCount != group.instanceCount) {
            group.instanceCount = newCount;
            group.dirty = true;
        }
    }
}

} // namespace qe
```

The `allTransforms` vector is never modified — the cull system rebuilds `visibleTransforms` each frame. The visible count changes as the camera moves, and the `dirty` flag triggers a re-upload only when needed.

---

## Use Cases in QEngine

**Vegetation** — grass tufts, bushes, and small plants placed across outdoor terrain. Hundreds or thousands sharing one mesh.

**Debris** — rubble, small rocks, scattered props after explosions.

**Architecture** — repeated columns, pillars, crates, barrels in warehouses.

**Particles** — if particles share a quad mesh (Ch 37), instance them. 10,000 particles = 1 draw call.

```cpp
// In src/game/level_builder.cpp — spawning 500 grass instances

void spawnGrassInstances(entt::registry& registry, const Terrain& terrain) {
    auto entity = registry.create();
    auto& group = registry.emplace<InstanceGroup>(entity);
    group.meshName    = "grass_tuft";
    group.textureName = "grass_atlas";
    group.isStatic    = true;
    group.boundingRadius = 0.5f;

    std::mt19937 rng(42);
    std::uniform_real_distribution<float> distXZ(0.0f, terrain.width());
    std::uniform_real_distribution<float> distRot(0.0f, glm::two_pi<float>());
    std::uniform_real_distribution<float> distScale(0.8f, 1.2f);

    group.allTransforms.reserve(500);
    for (int i = 0; i < 500; ++i) {
        float x = distXZ(rng), z = distXZ(rng);
        float y = terrain.heightAt(x, z);

        glm::mat4 model(1.0f);
        model = glm::translate(model, glm::vec3(x, y, z));
        model = glm::rotate(model, distRot(rng), glm::vec3(0, 1, 0));
        model = glm::scale(model, glm::vec3(distScale(rng)));
        group.allTransforms.push_back(model);
    }
    group.instanceCount = 500;
    group.dirty = true;
}
```

---

## Performance Comparison

```
  Scenario: 1000 identical crates, 36 triangles each

  ┌──────────────────────┬───────────────┬──────────────┐
  │                      │ No Instancing │  Instanced   │
  ├──────────────────────┼───────────────┼──────────────┤
  │ Draw calls           │      1000     │      1       │
  │ Uniform uploads      │      1000     │      0       │
  │ Buffer uploads       │         0     │      1       │
  │ CPU render time      │    ~5.2 ms    │   ~0.15 ms   │
  │ GPU render time      │    ~0.8 ms    │   ~0.8 ms    │
  │ Total frame time     │    ~6.0 ms    │   ~0.95 ms   │
  └──────────────────────┴───────────────┴──────────────┘

  Scenario: 5000 grass tufts, 2000 visible after frustum culling

  ┌──────────────────────┬───────────────┬──────────────┐
  │                      │ No Instancing │  Instanced   │
  ├──────────────────────┼───────────────┼──────────────┤
  │ Draw calls           │      2000     │      1       │
  │ CPU render time      │   ~10.4 ms    │   ~0.3 ms    │
  │ GPU render time      │    ~1.5 ms    │   ~1.5 ms    │
  └──────────────────────┴───────────────┴──────────────┘
```

Verify in QEngine using the developer console (Ch 27):

```
> show_draw_calls
Draw calls this frame: 47 (42 regular + 5 instanced)
Instanced objects: 3782
```

---

## Per-Instance Colour and Data

You can pass more than just transforms. A common extension is per-instance colour tint:

```cpp
// In src/engine/renderer/instance_data.h

namespace qe {

struct InstanceData {
    glm::mat4 model;       // 64 bytes — locations 3-6
    glm::vec4 colourTint;  // 16 bytes — location 7
};

} // namespace qe
```

Extend the buffer setup with one more attribute:

```cpp
// In src/engine/renderer/instance_buffer.h — extended version

inline GLuint createExtendedInstanceBuffer(GLuint vao, std::size_t cap = 256) {
    GLuint vbo = 0;
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(cap * sizeof(InstanceData)),
                 nullptr, GL_DYNAMIC_DRAW);

    std::size_t stride = sizeof(InstanceData);
    for (int col = 0; col < 4; ++col) {
        GLuint loc = 3 + col;
        glEnableVertexAttribArray(loc);
        glVertexAttribPointer(loc, 4, GL_FLOAT, GL_FALSE,
                              static_cast<GLsizei>(stride),
                              reinterpret_cast<void*>(sizeof(glm::vec4) * col));
        glVertexAttribDivisor(loc, 1);
    }
    glEnableVertexAttribArray(7);
    glVertexAttribPointer(7, 4, GL_FLOAT, GL_FALSE,
                          static_cast<GLsizei>(stride),
                          reinterpret_cast<void*>(sizeof(glm::mat4)));
    glVertexAttribDivisor(7, 1);

    glBindVertexArray(0);
    return vbo;
}
```

The shader picks it up as `layout (location = 7) in vec4 instanceColour;` and multiplies it with the texture colour in the fragment shader:

```glsl
// In assets/shaders/instanced_tinted.frag — key line
FragColor = vec4(texel.rgb * lighting * Tint.rgb, texel.a * Tint.a);
```

Now each grass tuft can have a slightly different green, each debris chunk a different shade of grey — all in the same single draw call.

---

## C++ Concept: Buffer Update Strategies

When updating a GPU buffer every frame, the upload method matters.

### `glBufferData` — Reallocate

```cpp
glBufferData(GL_ARRAY_BUFFER, size, data, GL_DYNAMIC_DRAW);
```

Allocates a new buffer and copies data in. The old buffer is orphaned. **Use when** the size changes between frames. **Downside**: allocation overhead every frame.

### `glBufferSubData` — In-Place Update

```cpp
glBufferSubData(GL_ARRAY_BUFFER, 0, size, data);
```

Updates existing buffer contents without reallocating. **Use when** size is stable. **Downside**: may stall if the GPU is still reading the old data (pipeline stall).

### Buffer Orphaning — Best of Both

```cpp
glBufferData(GL_ARRAY_BUFFER, size, nullptr, GL_DYNAMIC_DRAW);  // Orphan
glBufferSubData(GL_ARRAY_BUFFER, 0, size, data);                // Fill
```

The `nullptr` call tells the driver "discard the old buffer." The driver gives you a fresh allocation while the GPU finishes with the old one — no stall. This is QEngine's default pattern for all per-frame buffer updates.

### `glMapBufferRange` — Direct Memory Access

```cpp
void* ptr = glMapBufferRange(GL_ARRAY_BUFFER, 0, size,
                             GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
if (ptr) {
    std::memcpy(ptr, data, size);
    glUnmapBuffer(GL_ARRAY_BUFFER);
}
```

Returns a pointer directly into GPU-accessible memory. **Use when** generating data directly into the buffer (avoids an intermediate copy). **Downside**: mapping can fail, must unmap before drawing.

### Comparison

```
  ┌────────────────────────┬───────────┬──────────┬───────────────┐
  │ Method                 │ Realloc?  │ Stall?   │ Best For      │
  ├────────────────────────┼───────────┼──────────┼───────────────┤
  │ glBufferData           │ Yes       │ No       │ Changing size │
  │ glBufferSubData        │ No        │ Possible │ Stable size   │
  │ Orphan + SubData       │ Sometimes │ No       │ Per-frame     │
  │ glMapBufferRange       │ No        │ No*      │ Direct write  │
  └────────────────────────┴───────────┴──────────┴───────────────┘
  * With GL_MAP_INVALIDATE_BUFFER_BIT
```

For QEngine's instanced rendering, orphan + `glBufferSubData` is the default. If profiling shows buffer uploads as a bottleneck, switch to `glMapBufferRange` for the hot path.

---

## What's Next

In **Chapter 39**, we'll tackle **water and liquid rendering** — reflective and refractive surfaces using framebuffer render-to-texture, Fresnel blending, animated normal maps for wave motion, and depth-based transparency to make shallow water look different from deep water. We'll also integrate water volumes with the physics system so entities can float, swim, and take drowning damage.
