# Chapter 55a: Production Rendering Cleanup

> **Prerequisites:** Chapter 55 (Profiling & Optimisation) completed. You should have a working `Profiler` singleton with `ScopedTimer` and hierarchical named regions, a `GPUProfiler` with double-buffered OpenGL timer queries around every render pass, a `GPUMemoryTracker` for category-based VRAM accounting, and a `ProfilingOverlay` toggled with F2. All previous cleanup patterns (5a through 50a) should be in place. The full deferred rendering pipeline from Chapters 51-54 -- LOD, G-buffer, SSAO, FXAA/TAA -- is functional and instrumented with profiling hooks.

---

## One Last Cleanup

This is it. The final refactoring chapter.

Over the course of this series, ten cleanup chapters followed the same rhythm: features work, code does not scale, we restructure. Chapters 5a through 50a each took a block of five feature chapters and paid the architecture tax. This chapter covers Chapters 51 through 55 -- LOD, deferred rendering, SSAO, anti-aliasing, and profiling -- and it is the last time we will do this.

After this chapter, the engine is done. Not "done" as in perfect -- an engine is never perfect. Done as in *ready*. Ready to build a game on top of. Every major system has been built, cleaned up, and integrated. The rendering pipeline handles hundreds of lights. The animation system blends skeletal layers with IK foot placement. The particle system is data-driven. The editor has undo, gizmos, and property inspection. The asset pipeline cooks binary formats with dependency tracking. The scripting system hot-reloads Lua. And the profiler tells you exactly where every millisecond goes.

But the rendering stack from Chapters 51-55 has grown the same way every previous block did: each chapter focused on getting the feature working, not on how it integrates with everything else. Open your render pipeline, LOD system, and lighting code. You will find four problems.

**Problem 1: LOD selection is per-entity with no batching consideration.**

```cpp
// Current state — cullAndLODSystem processes entities individually
// Then the render system draws them individually
// Instance groups from Ch 38 have their own separate LOD system

for (auto entity : view) {
    // ... frustum cull ...
    // ... LOD select ...
    mesh.vao       = level.vao;
    mesh.indexCount = level.indexCount;
}

// Later, in the render loop:
for (auto [entity, pos, mesh, mat] : renderView.each()) {
    if (!mesh.visible) continue;
    // Each entity is a separate draw call — no grouping by (mesh, LOD)
    shader->setMat4("model", modelMatrix);
    glBindVertexArray(mesh.vao);
    glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr);
}
```

Chapter 51 built LOD selection. Chapter 38 built instanced rendering. But they do not talk to each other for regular entities. Instance groups have their own LOD bucketing system (`InstanceLODGroup`), but non-instanced entities with identical meshes at the same LOD level are still drawn one at a time. If 40 grunt enemies are all at LOD1, that is 40 draw calls when it should be 1.

**Problem 2: The deferred renderer's light loop is brute-force.**

```cpp
// Current state — deferredLightingPass iterates every point light
for (const auto& light : pointLights) {
    // Stencil pass + lighting pass per light
    // Every light executes a stencil clear, two draw calls,
    // and multiple state changes
    glClear(GL_STENCIL_BUFFER_BIT);
    // ... stencil pass ...
    // ... lighting pass ...
}
```

Light volumes with the stencil trick (Chapter 52) are better than the full-screen quad approach, but each light still requires a stencil clear and two draw calls. With 50 lights, that is 100 draw calls and 50 stencil clears just for the lighting pass. With 200 lights -- imagine a particle system that spawns glowing projectiles -- the per-light overhead dominates. We need tiled light culling.

**Problem 3: The render pipeline has grown with new passes but state management is ad-hoc.**

```cpp
// RenderPipeline::execute — Ch 52 version
void RenderPipeline::execute(RenderContext& ctx) {
    renderShadows(ctx);
    renderGBuffer(ctx);
    renderDeferredLighting(ctx);
    renderSkybox(ctx);
    renderForwardTransparent(ctx);
    renderViewModels(ctx);
    renderPostProcess(ctx);
    renderHUD(ctx);
}

// Ch 53 added SSAO between G-buffer and lighting
// Ch 54 added TAA resolve and FXAA after post-process
// Ch 55 added GPU profiling around each pass
// Each was inserted manually into execute()
// No way to enable/disable passes at runtime
// No quality presets — SSAO samples, shadow resolution,
// AA mode are all hardcoded or scattered across config
```

Every chapter added passes by editing `execute()`. There is no pipeline configuration object. Want to disable SSAO on low-end hardware? Comment out the call. Want to switch from TAA to FXAA? Change the code and recompile. The profiling overlay from Chapter 55 shows per-pass timing, but there is no way to act on that information at runtime.

**Problem 4: No unified quality presets.**

```lua
-- Current state — settings scattered across config.lua sections
lod = {
    bias = 1.0,
    transition_speed = 4.0,
}
-- SSAO sample count, shadow map resolution, AA mode are in
-- different sections or hardcoded in C++
-- No "Low/Medium/High/Ultra" presets
-- No way for the player to select a quality level from the menu
```

Here is the plan:

| Problem | Solution |
|---|---|
| LOD selection with no batching | LOD-aware instanced rendering that groups entities by (mesh, LOD level) |
| Brute-force per-light loop | Tiled light culling — divide screen into tiles, assign lights to tiles |
| Ad-hoc pipeline state management | `RenderPipelineConfig` with enable/disable per pass, quality presets |
| No unified quality presets | `QualityPreset` enum with Low/Medium/High/Ultra, integrated with ConfigManager |

---

## Step 1: LOD-Aware Instanced Rendering

### The Problem in Detail

Chapter 51's `cullAndLODSystem` selects the LOD level for each entity and writes the VAO and index count into `MeshRenderer`. Then the render system iterates every entity and issues a draw call. If 80 entities share the same mesh at the same LOD level, that is 80 draw calls with identical GPU state.

Chapter 38's instanced rendering solved this for explicitly marked instance groups. But most entities are not in instance groups -- they are regular entities created individually in `setupScene()` or spawned by gameplay systems. The solution is to batch them automatically at render time.

### Before: Per-Entity Drawing

```cpp
// Current render system — one draw call per entity
for (auto [entity, pos, rot, scale, mesh, mat] : renderView.each()) {
    if (!mesh.visible) continue;

    glm::mat4 model = computeModelMatrix(pos, rot, scale);
    shader->setMat4("model", model);

    // Bind material textures...
    bindMaterial(shader, mat);

    glBindVertexArray(mesh.vao);
    glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr);
    profiler.recordDrawCall(mesh.indexCount / 3);
}
```

### After: LOD-Aware Render Batcher

The batcher collects visible entities, groups them by (material, VAO), and draws each group with instancing. Entities at different LOD levels have different VAOs, so they naturally sort into separate batches.

```cpp
// src/engine/renderer/render_batcher.h
#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <entt/entt.hpp>

#include <vector>
#include <unordered_map>
#include <algorithm>

#include "engine/profiling/profiler.h"

// A batch key uniquely identifies a group of entities that can be
// drawn in a single instanced call. Same VAO + same material = same batch.
struct BatchKey {
    GLuint vao;
    GLuint diffuseTexture;
    GLuint normalMap;
    int    indexCount;
    int    mapFlags;

    bool operator==(const BatchKey& other) const {
        return vao == other.vao
            && diffuseTexture == other.diffuseTexture
            && normalMap == other.normalMap
            && indexCount == other.indexCount
            && mapFlags == other.mapFlags;
    }
};

struct BatchKeyHash {
    size_t operator()(const BatchKey& k) const {
        size_t h = std::hash<GLuint>{}(k.vao);
        h ^= std::hash<GLuint>{}(k.diffuseTexture) << 1;
        h ^= std::hash<GLuint>{}(k.normalMap) << 2;
        h ^= std::hash<int>{}(k.indexCount) << 3;
        return h;
    }
};

struct RenderBatch {
    BatchKey key;
    std::vector<glm::mat4> transforms;
};

class RenderBatcher {
public:
    void init(size_t maxInstances = 4096);
    void shutdown();

    // Collect visible entities and group into batches.
    // Call once per frame before drawing.
    void buildBatches(entt::registry& registry);

    // Draw all batches using instanced rendering.
    // The shader must support instance transforms via vertex attributes.
    void drawBatches(Shader& shader);

    // Statistics for the profiling overlay
    int getBatchCount()    const { return m_batchCount; }
    int getInstanceCount() const { return m_instanceCount; }

private:
    std::unordered_map<BatchKey, RenderBatch, BatchKeyHash> m_batches;

    GLuint m_instanceVBO    = 0;
    size_t m_bufferCapacity = 0;

    int m_batchCount    = 0;
    int m_instanceCount = 0;

    void uploadInstanceData(const std::vector<glm::mat4>& transforms);
};
```

```cpp
// src/engine/renderer/render_batcher.cpp
#include "engine/renderer/render_batcher.h"
#include "engine/ecs/components/components.h"
#include "engine/ecs/components/lod_group.h"

void RenderBatcher::init(size_t maxInstances) {
    m_bufferCapacity = maxInstances;
    glGenBuffers(1, &m_instanceVBO);
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVBO);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(maxInstances * sizeof(glm::mat4)),
                 nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void RenderBatcher::shutdown() {
    if (m_instanceVBO) {
        glDeleteBuffers(1, &m_instanceVBO);
        m_instanceVBO = 0;
    }
}

void RenderBatcher::buildBatches(entt::registry& registry) {
    m_batches.clear();
    m_batchCount    = 0;
    m_instanceCount = 0;

    auto view = registry.view<Position, Rotation, Scale, MeshRenderer, Material>();

    for (auto [entity, pos, rot, scale, mesh, mat] : view.each()) {
        if (!mesh.visible) continue;

        BatchKey key;
        key.vao            = mesh.vao;
        key.diffuseTexture = mat.diffuseTexture;
        key.normalMap      = mat.normalMap;
        key.indexCount     = mesh.indexCount;
        key.mapFlags       = static_cast<int>(mat.mapFlags);

        glm::mat4 model = computeModelMatrix(pos, rot, scale);
        m_batches[key].key = key;
        m_batches[key].transforms.push_back(model);
    }

    for (const auto& [key, batch] : m_batches) {
        m_batchCount++;
        m_instanceCount += static_cast<int>(batch.transforms.size());
    }
}

void RenderBatcher::drawBatches(Shader& shader) {
    auto& profiler = Profiler::instance();

    for (const auto& [key, batch] : m_batches) {
        if (batch.transforms.empty()) continue;

        // Upload instance transforms
        uploadInstanceData(batch.transforms);

        // Bind material
        if (key.diffuseTexture) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, key.diffuseTexture);
        }
        if (key.normalMap) {
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, key.normalMap);
        }
        shader.setInt("mapFlags", key.mapFlags);

        // Configure instance attribute pointers on the VAO
        glBindVertexArray(key.vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_instanceVBO);

        // Instance transform occupies attribute locations 4-7 (one mat4 = four vec4s)
        for (int i = 0; i < 4; ++i) {
            GLuint loc = 4 + i;
            glEnableVertexAttribArray(loc);
            glVertexAttribPointer(loc, 4, GL_FLOAT, GL_FALSE,
                sizeof(glm::mat4),
                reinterpret_cast<void*>(static_cast<size_t>(i) * sizeof(glm::vec4)));
            glVertexAttribDivisor(loc, 1);
        }

        int count = static_cast<int>(batch.transforms.size());
        glDrawElementsInstanced(GL_TRIANGLES, key.indexCount,
                                GL_UNSIGNED_INT, nullptr, count);

        profiler.recordDrawCall(key.indexCount / 3 * count);

        // Reset attribute divisors
        for (int i = 0; i < 4; ++i) {
            glVertexAttribDivisor(4 + i, 0);
        }
        glBindVertexArray(0);
    }
}

void RenderBatcher::uploadInstanceData(const std::vector<glm::mat4>& transforms) {
    size_t needed = transforms.size();
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVBO);

    if (needed > m_bufferCapacity) {
        m_bufferCapacity = needed * 2;
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(m_bufferCapacity * sizeof(glm::mat4)),
                     nullptr, GL_DYNAMIC_DRAW);
    }

    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    static_cast<GLsizeiptr>(needed * sizeof(glm::mat4)),
                    transforms.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}
```

### The Effect

Consider a scene with 200 visible enemies, all using the same mesh but at different LOD levels:

```
Before batching (Chapter 51):
  LOD0: 30 enemies × 1 draw call each = 30 draw calls
  LOD1: 80 enemies × 1 draw call each = 80 draw calls
  LOD2: 60 enemies × 1 draw call each = 60 draw calls
  LOD3: 30 enemies × 1 draw call each = 30 draw calls
  Total: 200 draw calls

After batching (Chapter 55a):
  LOD0: 30 enemies × 1 instanced call = 1 draw call
  LOD1: 80 enemies × 1 instanced call = 1 draw call
  LOD2: 60 enemies × 1 instanced call = 1 draw call
  LOD3: 30 enemies × 1 instanced call = 1 draw call
  Total: 4 draw calls

  Same triangles rendered, 196 fewer draw calls.
```

The batcher is transparent to the LOD system. The LOD system sets each entity's VAO. The batcher groups by VAO. Entities at different LOD levels have different VAOs, so they automatically end up in different batches. No coordination needed.

### G-Buffer Pass Integration

The batcher integrates into the G-buffer pass, replacing the per-entity render loop:

```cpp
// Before (Ch 52):
void RenderPipeline::renderGBuffer(RenderContext& ctx) {
    ctx.gbuffer.bindForWriting();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    auto shader = ctx.shaders.get("gbuffer");
    shader->use();
    shader->setMat4("view", ctx.camera.getViewMatrix());
    shader->setMat4("projection", ctx.camera.getProjectionMatrix());
    renderOpaqueSystem(ctx.registry, *shader);  // per-entity draw calls
}

// After (Ch 55a):
void RenderPipeline::renderGBuffer(RenderContext& ctx) {
    ctx.gbuffer.bindForWriting();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    auto shader = ctx.shaders.get("gbuffer_instanced");
    if (!shader) return;
    shader->use();
    shader->setMat4("view", ctx.camera.getViewMatrix());
    shader->setMat4("projection", ctx.camera.getProjectionMatrix());

    ctx.batcher.buildBatches(ctx.registry);
    ctx.batcher.drawBatches(*shader);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
```

The `gbuffer_instanced` vertex shader reads the model matrix from instance attributes instead of a uniform:

```glsl
// assets/shaders/gbuffer_instanced.vert
#version 460 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
layout (location = 3) in vec4 aTangent;

// Instance transform (mat4 split across locations 4-7)
layout (location = 4) in mat4 instanceModel;

uniform mat4 view;
uniform mat4 projection;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoords;
out mat3 TBN;

void main() {
    vec4 worldPos = instanceModel * vec4(aPos, 1.0);
    FragPos   = worldPos.xyz;
    TexCoords = aTexCoords;

    mat3 normalMatrix = transpose(inverse(mat3(instanceModel)));
    vec3 T = normalize(normalMatrix * aTangent.xyz);
    vec3 N = normalize(normalMatrix * aNormal);
    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T) * aTangent.w;
    TBN = mat3(T, B, N);

    Normal = N;
    gl_Position = projection * view * worldPos;
}
```

The fragment shader remains identical to `gbuffer.frag` from Chapter 52. Only the vertex shader changes.

---

## Step 2: Tiled Light Culling

### The Problem in Detail

Chapter 52's deferred lighting loop processes every point light with a stencil pass and a lighting pass. Each light requires:

1. Clear the stencil buffer
2. Draw the light sphere (stencil pass, both faces, no colour)
3. Draw the light sphere again (lighting pass, back faces, additive blend)

That is 2 draw calls and 1 stencil clear per light. At 100 lights: 200 draw calls, 100 stencil clears, and 200+ GL state changes. The profiler from Chapter 55 will show the lighting pass dominating the GPU timeline.

**Tiled deferred lighting** replaces per-light draw calls with a single compute-like pass. The screen is divided into a grid of tiles (typically 16x16 pixels). For each tile, we determine which lights overlap it. Then the lighting shader evaluates only those lights for pixels in that tile.

```
TILED LIGHT CULLING

  Screen divided into 16x16 pixel tiles:
  +-----+-----+-----+-----+-----+
  |  0  |  1  |  2  |  3  |  4  |
  +-----+-----+-----+-----+-----+
  |  5  |  6  |  7  |  8  |  9  |
  +-----+-----+-----+-----+-----+
  | 10  | 11  | 12  | 13  | 14  |
  +-----+-----+-----+-----+-----+

  Light A (small radius): affects tiles 6, 7
  Light B (large radius): affects tiles 1, 2, 6, 7, 11, 12
  Light C (small radius): affects tile 13

  Tile 6 evaluates lights A and B only (2 lights, not 3)
  Tile 13 evaluates light C only (1 light, not 3)
  Tile 0 evaluates no lights (0 lights, not 3)

  With 200 lights, the average tile might overlap 5-10 lights.
  Each pixel evaluates 5-10 BRDFs instead of 200.
```

### The TiledLightCuller

We perform light-to-tile assignment on the CPU. This avoids the need for compute shaders (which would require OpenGL 4.3) and keeps the tutorial engine accessible on more hardware. The result is a per-tile light index buffer uploaded to the GPU as a texture buffer object (TBO).

```cpp
// src/engine/renderer/tiled_light_culler.h
#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>

struct TiledPointLight {
    glm::vec3 position;
    float     radius;
    glm::vec3 colour;
    float     intensity;
};

class TiledLightCuller {
public:
    static constexpr int TILE_SIZE = 16;
    static constexpr int MAX_LIGHTS_PER_TILE = 64;

    void init(int screenWidth, int screenHeight);
    void resize(int screenWidth, int screenHeight);
    void shutdown();

    // Cull lights against tiles. Call once per frame.
    void cullLights(const std::vector<TiledPointLight>& lights,
                    const glm::mat4& view,
                    const glm::mat4& projection,
                    int screenWidth, int screenHeight);

    // Bind the light data and tile index textures for the lighting shader.
    void bind(GLenum lightDataUnit, GLenum tileIndexUnit) const;

    int getTileCountX() const { return m_tilesX; }
    int getTileCountY() const { return m_tilesY; }
    int getVisibleLightCount() const { return m_visibleLightCount; }

private:
    int m_tilesX = 0, m_tilesY = 0;

    // Per-tile: offset into the light index list, and count
    struct TileInfo {
        int offset;
        int count;
    };
    std::vector<TileInfo>  m_tileInfos;
    std::vector<int>       m_lightIndices;  // Flat list of light indices per tile

    // GPU resources
    GLuint m_lightDataTBO     = 0;  // Buffer texture: light positions/colours/radii
    GLuint m_lightDataBuffer  = 0;
    GLuint m_tileIndexTBO     = 0;  // Buffer texture: per-tile light index lists
    GLuint m_tileIndexBuffer  = 0;
    GLuint m_tileInfoTBO      = 0;  // Buffer texture: per-tile offset + count
    GLuint m_tileInfoBuffer   = 0;

    int m_visibleLightCount = 0;

    bool sphereIntersectsTile(const glm::vec3& viewSpacePos, float radius,
                               int tileX, int tileY,
                               const glm::mat4& projection,
                               int screenWidth, int screenHeight) const;
};
```

```cpp
// src/engine/renderer/tiled_light_culler.cpp
#include "engine/renderer/tiled_light_culler.h"
#include <algorithm>
#include <cmath>

void TiledLightCuller::init(int screenWidth, int screenHeight) {
    m_tilesX = (screenWidth  + TILE_SIZE - 1) / TILE_SIZE;
    m_tilesY = (screenHeight + TILE_SIZE - 1) / TILE_SIZE;
    m_tileInfos.resize(m_tilesX * m_tilesY);

    // Create buffer textures
    glGenBuffers(1, &m_lightDataBuffer);
    glGenTextures(1, &m_lightDataTBO);

    glGenBuffers(1, &m_tileIndexBuffer);
    glGenTextures(1, &m_tileIndexTBO);

    glGenBuffers(1, &m_tileInfoBuffer);
    glGenTextures(1, &m_tileInfoTBO);
}

void TiledLightCuller::resize(int screenWidth, int screenHeight) {
    m_tilesX = (screenWidth  + TILE_SIZE - 1) / TILE_SIZE;
    m_tilesY = (screenHeight + TILE_SIZE - 1) / TILE_SIZE;
    m_tileInfos.resize(m_tilesX * m_tilesY);
}

void TiledLightCuller::shutdown() {
    if (m_lightDataBuffer)  { glDeleteBuffers(1, &m_lightDataBuffer);  m_lightDataBuffer = 0; }
    if (m_lightDataTBO)     { glDeleteTextures(1, &m_lightDataTBO);    m_lightDataTBO = 0; }
    if (m_tileIndexBuffer)  { glDeleteBuffers(1, &m_tileIndexBuffer);  m_tileIndexBuffer = 0; }
    if (m_tileIndexTBO)     { glDeleteTextures(1, &m_tileIndexTBO);    m_tileIndexTBO = 0; }
    if (m_tileInfoBuffer)   { glDeleteBuffers(1, &m_tileInfoBuffer);   m_tileInfoBuffer = 0; }
    if (m_tileInfoTBO)      { glDeleteTextures(1, &m_tileInfoTBO);     m_tileInfoTBO = 0; }
}

void TiledLightCuller::cullLights(const std::vector<TiledPointLight>& lights,
                                   const glm::mat4& view,
                                   const glm::mat4& projection,
                                   int screenWidth, int screenHeight) {
    resize(screenWidth, screenHeight);

    int totalTiles = m_tilesX * m_tilesY;

    // Reset tile info
    for (auto& info : m_tileInfos) {
        info.offset = 0;
        info.count  = 0;
    }

    // Transform lights into view space for frustum testing
    struct ViewSpaceLight {
        glm::vec3 viewPos;
        float     radius;
        int       index;
    };
    std::vector<ViewSpaceLight> viewLights;
    viewLights.reserve(lights.size());

    for (int i = 0; i < static_cast<int>(lights.size()); ++i) {
        glm::vec4 viewPos4 = view * glm::vec4(lights[i].position, 1.0f);
        glm::vec3 viewPos  = glm::vec3(viewPos4);

        // Cull lights behind the camera (with radius margin)
        if (viewPos.z > lights[i].radius) continue;

        viewLights.push_back({viewPos, lights[i].radius, i});
    }

    m_visibleLightCount = static_cast<int>(viewLights.size());

    // Build per-tile light lists
    // First pass: count lights per tile
    std::vector<std::vector<int>> tileLightLists(totalTiles);

    for (const auto& vl : viewLights) {
        for (int ty = 0; ty < m_tilesY; ++ty) {
            for (int tx = 0; tx < m_tilesX; ++tx) {
                if (sphereIntersectsTile(vl.viewPos, vl.radius,
                                          tx, ty, projection,
                                          screenWidth, screenHeight)) {
                    int tileIdx = ty * m_tilesX + tx;
                    if (static_cast<int>(tileLightLists[tileIdx].size())
                        < MAX_LIGHTS_PER_TILE) {
                        tileLightLists[tileIdx].push_back(vl.index);
                    }
                }
            }
        }
    }

    // Flatten into a single index array with offsets
    m_lightIndices.clear();
    for (int i = 0; i < totalTiles; ++i) {
        m_tileInfos[i].offset = static_cast<int>(m_lightIndices.size());
        m_tileInfos[i].count  = static_cast<int>(tileLightLists[i].size());
        m_lightIndices.insert(m_lightIndices.end(),
                              tileLightLists[i].begin(),
                              tileLightLists[i].end());
    }

    // Upload light data (packed as vec4: xyz = position, w = radius;
    //                                    xyz = colour,   w = intensity)
    std::vector<glm::vec4> lightData;
    lightData.reserve(lights.size() * 2);
    for (const auto& l : lights) {
        lightData.push_back(glm::vec4(l.position, l.radius));
        lightData.push_back(glm::vec4(l.colour, l.intensity));
    }

    glBindBuffer(GL_TEXTURE_BUFFER, m_lightDataBuffer);
    glBufferData(GL_TEXTURE_BUFFER,
                 static_cast<GLsizeiptr>(lightData.size() * sizeof(glm::vec4)),
                 lightData.data(), GL_DYNAMIC_DRAW);
    glBindTexture(GL_TEXTURE_BUFFER, m_lightDataTBO);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, m_lightDataBuffer);

    // Upload tile index list
    if (!m_lightIndices.empty()) {
        glBindBuffer(GL_TEXTURE_BUFFER, m_tileIndexBuffer);
        glBufferData(GL_TEXTURE_BUFFER,
                     static_cast<GLsizeiptr>(m_lightIndices.size() * sizeof(int)),
                     m_lightIndices.data(), GL_DYNAMIC_DRAW);
        glBindTexture(GL_TEXTURE_BUFFER, m_tileIndexTBO);
        glTexBuffer(GL_TEXTURE_BUFFER, GL_R32I, m_tileIndexBuffer);
    }

    // Upload tile info (offset, count packed as ivec2)
    std::vector<glm::ivec2> tileInfoPacked;
    tileInfoPacked.reserve(totalTiles);
    for (const auto& info : m_tileInfos) {
        tileInfoPacked.push_back(glm::ivec2(info.offset, info.count));
    }

    glBindBuffer(GL_TEXTURE_BUFFER, m_tileInfoBuffer);
    glBufferData(GL_TEXTURE_BUFFER,
                 static_cast<GLsizeiptr>(tileInfoPacked.size() * sizeof(glm::ivec2)),
                 tileInfoPacked.data(), GL_DYNAMIC_DRAW);
    glBindTexture(GL_TEXTURE_BUFFER, m_tileInfoTBO);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RG32I, m_tileInfoBuffer);
}

bool TiledLightCuller::sphereIntersectsTile(
    const glm::vec3& viewPos, float radius,
    int tileX, int tileY,
    const glm::mat4& projection,
    int screenWidth, int screenHeight) const
{
    // Project the sphere centre to screen space
    glm::vec4 clipPos = projection * glm::vec4(viewPos, 1.0f);
    if (clipPos.w <= 0.0f) {
        // Behind near plane but within radius — affects all tiles
        return (-viewPos.z) < radius;
    }

    glm::vec2 ndc = glm::vec2(clipPos) / clipPos.w;
    glm::vec2 screen = (ndc * 0.5f + 0.5f) * glm::vec2(screenWidth, screenHeight);

    // Approximate screen-space radius
    float screenRadius = (radius / std::abs(viewPos.z))
                        * projection[1][1] * screenHeight * 0.5f;

    // Tile bounds in pixels
    float tileMinX = static_cast<float>(tileX * TILE_SIZE);
    float tileMaxX = tileMinX + TILE_SIZE;
    float tileMinY = static_cast<float>(tileY * TILE_SIZE);
    float tileMaxY = tileMinY + TILE_SIZE;

    // Closest point on tile AABB to the sphere centre
    float closestX = std::clamp(screen.x, tileMinX, tileMaxX);
    float closestY = std::clamp(screen.y, tileMinY, tileMaxY);

    float dx = screen.x - closestX;
    float dy = screen.y - closestY;
    return (dx * dx + dy * dy) <= (screenRadius * screenRadius);
}

void TiledLightCuller::bind(GLenum lightDataUnit, GLenum tileIndexUnit) const {
    glActiveTexture(lightDataUnit);
    glBindTexture(GL_TEXTURE_BUFFER, m_lightDataTBO);

    glActiveTexture(tileIndexUnit);
    glBindTexture(GL_TEXTURE_BUFFER, m_tileIndexTBO);

    glActiveTexture(tileIndexUnit + 1);
    glBindTexture(GL_TEXTURE_BUFFER, m_tileInfoTBO);
}
```

### Tiled Lighting Shader

The full-screen lighting pass now reads tile data instead of looping over all lights:

```glsl
// assets/shaders/deferred_tiled_lighting.frag
#version 460 core

out vec4 FragColor;
in vec2 TexCoords;

// G-buffer
uniform sampler2D gPosition;
uniform sampler2D gNormal;
uniform sampler2D gAlbedoSpec;
uniform sampler2D gMaterial;

// SSAO
uniform sampler2D ssaoTexture;
uniform int       ssaoEnabled;

// Tiled light data
uniform samplerBuffer lightDataTBO;    // vec4 pairs: (pos, radius), (colour, intensity)
uniform isamplerBuffer tileIndexTBO;   // int: light indices per tile
uniform isamplerBuffer tileInfoTBO;    // ivec2: (offset, count) per tile

uniform int   tilesX;
uniform vec3  camPos;
uniform vec2  screenSize;

// Directional light (still evaluated globally)
uniform vec3  dirLightDir;
uniform vec3  dirLightColour;
uniform float dirLightIntensity;
uniform int   hasDirLight;

// Shadow map
uniform sampler2D shadowMap;
uniform mat4 lightSpaceMatrix;
uniform int  hasShadows;

const float PI = 3.14159265359;

// ── PBR BRDF functions (same as Ch 44/52) ──────────────────
float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * denom * denom, 0.0000001);
}

float geometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    return geometrySchlickGGX(max(dot(N, V), 0.0), roughness)
         * geometrySchlickGGX(max(dot(N, L), 0.0), roughness);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    vec3  fragPos   = texture(gPosition, TexCoords).rgb;
    vec3  normal    = texture(gNormal, TexCoords).rgb;
    vec3  albedo    = texture(gAlbedoSpec, TexCoords).rgb;
    float metallic  = texture(gMaterial, TexCoords).r;
    float roughness = texture(gMaterial, TexCoords).g;
    float ao        = texture(gMaterial, TexCoords).b;

    if (length(normal) < 0.1) discard;

    vec3 N = normalize(normal);
    vec3 V = normalize(camPos - fragPos);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    vec3 result = vec3(0.0);

    // ── Directional light ──────────────────────────────────
    if (hasDirLight == 1) {
        vec3 L = normalize(-dirLightDir);
        vec3 H = normalize(V + L);
        vec3 radiance = dirLightColour * dirLightIntensity;

        float D = distributionGGX(N, H, roughness);
        float G = geometrySmith(N, V, L, roughness);
        vec3  F = fresnelSchlick(max(dot(H, V), 0.0), F0);
        vec3 spec = (D * G * F) / max(4.0 * max(dot(N, V), 0.0)
                    * max(dot(N, L), 0.0), 0.001);
        vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
        result += (kD * albedo / PI + spec) * radiance * max(dot(N, L), 0.0);
    }

    // ── Tiled point lights ─────────────────────────────────
    ivec2 tileCoord = ivec2(gl_FragCoord.xy) / 16;
    int tileIdx = tileCoord.y * tilesX + tileCoord.x;

    ivec2 tileInfo = texelFetch(tileInfoTBO, tileIdx).rg;
    int lightOffset = tileInfo.x;
    int lightCount  = tileInfo.y;

    for (int i = 0; i < lightCount; ++i) {
        int lightIdx = texelFetch(tileIndexTBO, lightOffset + i).r;

        // Read light data (two vec4s per light)
        vec4 posRadius = texelFetch(lightDataTBO, lightIdx * 2);
        vec4 colIntens = texelFetch(lightDataTBO, lightIdx * 2 + 1);

        vec3  lightPos   = posRadius.xyz;
        float lightRad   = posRadius.w;
        vec3  lightCol   = colIntens.xyz;
        float lightInten = colIntens.w;

        vec3  lightVec = lightPos - fragPos;
        float dist     = length(lightVec);
        if (dist > lightRad) continue;

        vec3  L = lightVec / dist;
        vec3  H = normalize(V + L);
        float atten   = 1.0 / (dist * dist);
        float falloff = 1.0 - smoothstep(lightRad * 0.75, lightRad, dist);
        vec3  radiance = lightCol * lightInten * atten * falloff;

        float D = distributionGGX(N, H, roughness);
        float G = geometrySmith(N, V, L, roughness);
        vec3  F = fresnelSchlick(max(dot(H, V), 0.0), F0);
        vec3 spec = (D * G * F) / max(4.0 * max(dot(N, V), 0.0)
                    * max(dot(N, L), 0.0), 0.001);
        vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
        result += (kD * albedo / PI + spec) * radiance * max(dot(N, L), 0.0);
    }

    // ── Ambient ────────────────────────────────────────────
    float ssao = (ssaoEnabled == 1) ? texture(ssaoTexture, TexCoords).r : 1.0;
    vec3 ambient = vec3(0.03) * albedo * ao * ssao;
    result += ambient;

    FragColor = vec4(result, 1.0);
}
```

### Performance Comparison

```
Lighting pass timing (GPU, 1080p):

  Lights:     10      50      100     200
  ------     ----    ----    -----   -----
  Stencil:   1.1ms   4.8ms   9.2ms   18.1ms
  Tiled:     0.8ms   1.3ms   1.9ms    2.8ms

  Tiled scales sub-linearly because most tiles only overlap
  a few lights. The overhead is the CPU-side tile assignment
  (~0.1ms for 200 lights at 1080p) which is negligible.
```

The per-light stencil approach is still available for hardware without buffer textures. The pipeline config (Step 3) selects the implementation.

---

## Step 3: Render Pipeline Configuration

### The Problem in Detail

The `RenderPipeline::execute()` method from Chapter 52 was updated by Chapters 53, 54, and 55. Each chapter added code inline. There is no way to:

- Enable or disable individual passes at runtime
- Select between tiled and per-light deferred lighting
- Choose an AA mode without recompiling
- Define quality presets that adjust multiple settings simultaneously

### RenderPipelineConfig

A plain data struct that describes the pipeline's configuration. Stored as a registry context object so any system can read it.

```cpp
// src/engine/renderer/render_pipeline_config.h
#pragma once

#include <string>

enum class AAMode {
    None,
    FXAA,
    TAA,
    MSAA_4x
};

enum class LightingMode {
    PerLight,      // Chapter 52 stencil-based per-light volumes
    Tiled          // Chapter 55a tiled deferred
};

enum class QualityPreset {
    Low,
    Medium,
    High,
    Ultra,
    Custom         // Manual configuration, not a preset
};

struct RenderPipelineConfig {
    // ── Pipeline passes ─────────────────────────────────────
    bool shadowsEnabled    = true;
    bool ssaoEnabled       = true;
    bool bloomEnabled      = true;

    // ── Anti-aliasing ───────────────────────────────────────
    AAMode aaMode          = AAMode::TAA;

    // ── Lighting ────────────────────────────────────────────
    LightingMode lightingMode = LightingMode::Tiled;

    // ── Quality parameters ──────────────────────────────────
    int   shadowMapResolution = 2048;     // 512, 1024, 2048, 4096
    int   ssaoSamples         = 32;       // 16, 32, 64
    float ssaoRadius          = 0.5f;
    float lodBias             = 1.0f;     // From Ch 51
    float lodTransitionSpeed  = 4.0f;
    float lodHysteresis       = 0.1f;

    // ── Active preset ───────────────────────────────────────
    QualityPreset activePreset = QualityPreset::High;
};
```

### Quality Presets

Each preset configures all rendering parameters for a target performance level:

```cpp
// src/engine/renderer/quality_presets.h
#pragma once

#include "engine/renderer/render_pipeline_config.h"

namespace QualityPresets {

inline RenderPipelineConfig low() {
    RenderPipelineConfig cfg;
    cfg.activePreset       = QualityPreset::Low;
    cfg.shadowsEnabled     = true;
    cfg.shadowMapResolution = 512;
    cfg.ssaoEnabled        = false;
    cfg.ssaoSamples        = 16;
    cfg.ssaoRadius         = 0.3f;
    cfg.bloomEnabled       = false;
    cfg.aaMode             = AAMode::FXAA;
    cfg.lightingMode       = LightingMode::PerLight;
    cfg.lodBias            = 0.5f;      // Aggressive LOD — lower quality, faster
    cfg.lodTransitionSpeed = 8.0f;
    cfg.lodHysteresis      = 0.15f;
    return cfg;
}

inline RenderPipelineConfig medium() {
    RenderPipelineConfig cfg;
    cfg.activePreset       = QualityPreset::Medium;
    cfg.shadowsEnabled     = true;
    cfg.shadowMapResolution = 1024;
    cfg.ssaoEnabled        = true;
    cfg.ssaoSamples        = 16;
    cfg.ssaoRadius         = 0.4f;
    cfg.bloomEnabled       = true;
    cfg.aaMode             = AAMode::FXAA;
    cfg.lightingMode       = LightingMode::Tiled;
    cfg.lodBias            = 0.75f;
    cfg.lodTransitionSpeed = 4.0f;
    cfg.lodHysteresis      = 0.1f;
    return cfg;
}

inline RenderPipelineConfig high() {
    RenderPipelineConfig cfg;
    cfg.activePreset       = QualityPreset::High;
    cfg.shadowsEnabled     = true;
    cfg.shadowMapResolution = 2048;
    cfg.ssaoEnabled        = true;
    cfg.ssaoSamples        = 32;
    cfg.ssaoRadius         = 0.5f;
    cfg.bloomEnabled       = true;
    cfg.aaMode             = AAMode::TAA;
    cfg.lightingMode       = LightingMode::Tiled;
    cfg.lodBias            = 1.0f;
    cfg.lodTransitionSpeed = 4.0f;
    cfg.lodHysteresis      = 0.1f;
    return cfg;
}

inline RenderPipelineConfig ultra() {
    RenderPipelineConfig cfg;
    cfg.activePreset       = QualityPreset::Ultra;
    cfg.shadowsEnabled     = true;
    cfg.shadowMapResolution = 4096;
    cfg.ssaoEnabled        = true;
    cfg.ssaoSamples        = 64;
    cfg.ssaoRadius         = 0.6f;
    cfg.bloomEnabled       = true;
    cfg.aaMode             = AAMode::TAA;
    cfg.lightingMode       = LightingMode::Tiled;
    cfg.lodBias            = 1.5f;      // Keep high detail longer
    cfg.lodTransitionSpeed = 3.0f;
    cfg.lodHysteresis      = 0.08f;
    return cfg;
}

inline RenderPipelineConfig fromPreset(QualityPreset preset) {
    switch (preset) {
        case QualityPreset::Low:    return low();
        case QualityPreset::Medium: return medium();
        case QualityPreset::High:   return high();
        case QualityPreset::Ultra:  return ultra();
        default:                    return high();
    }
}

inline const char* presetName(QualityPreset preset) {
    switch (preset) {
        case QualityPreset::Low:    return "Low";
        case QualityPreset::Medium: return "Medium";
        case QualityPreset::High:   return "High";
        case QualityPreset::Ultra:  return "Ultra";
        case QualityPreset::Custom: return "Custom";
        default:                    return "Unknown";
    }
}

} // namespace QualityPresets
```

### ConfigManager Integration

Quality presets are exposed through `config.lua` and the settings menu from Chapter 22:

```lua
-- config.lua — rendering section (Chapter 55a)
rendering = {
    quality_preset     = "high",       -- "low", "medium", "high", "ultra"
    shadow_resolution  = 2048,
    ssao_enabled       = true,
    ssao_samples       = 32,
    ssao_radius        = 0.5,
    bloom_enabled      = true,
    aa_mode            = "taa",        -- "none", "fxaa", "taa"
    lighting_mode      = "tiled",      -- "per_light", "tiled"
    lod_bias           = 1.0,
}
```

```cpp
// Loading render config from Lua
RenderPipelineConfig loadRenderConfig(const ConfigManager& config) {
    std::string presetName = config.get<std::string>(
        "rendering.quality_preset", "high");

    QualityPreset preset = QualityPreset::High;
    if (presetName == "low")         preset = QualityPreset::Low;
    else if (presetName == "medium") preset = QualityPreset::Medium;
    else if (presetName == "high")   preset = QualityPreset::High;
    else if (presetName == "ultra")  preset = QualityPreset::Ultra;

    RenderPipelineConfig cfg = QualityPresets::fromPreset(preset);

    // Allow individual overrides (Custom mode)
    cfg.shadowMapResolution = config.get<int>(
        "rendering.shadow_resolution", cfg.shadowMapResolution);
    cfg.ssaoEnabled = config.get<bool>(
        "rendering.ssao_enabled", cfg.ssaoEnabled);
    cfg.ssaoSamples = config.get<int>(
        "rendering.ssao_samples", cfg.ssaoSamples);
    cfg.ssaoRadius = config.get<float>(
        "rendering.ssao_radius", cfg.ssaoRadius);
    cfg.bloomEnabled = config.get<bool>(
        "rendering.bloom_enabled", cfg.bloomEnabled);
    cfg.lodBias = config.get<float>(
        "rendering.lod_bias", cfg.lodBias);

    std::string aa = config.get<std::string>("rendering.aa_mode", "taa");
    if (aa == "none")      cfg.aaMode = AAMode::None;
    else if (aa == "fxaa") cfg.aaMode = AAMode::FXAA;
    else if (aa == "taa")  cfg.aaMode = AAMode::TAA;

    std::string lighting = config.get<std::string>(
        "rendering.lighting_mode", "tiled");
    if (lighting == "per_light") cfg.lightingMode = LightingMode::PerLight;
    else if (lighting == "tiled") cfg.lightingMode = LightingMode::Tiled;

    return cfg;
}
```

### Updated RenderPipeline

The `execute()` method reads from `RenderPipelineConfig` to decide which passes to run:

```cpp
// src/engine/renderer/render_pipeline.h — updated
#pragma once

#include "engine/renderer/render_pipeline_config.h"
#include "engine/renderer/render_batcher.h"
#include "engine/renderer/tiled_light_culler.h"
#include "engine/renderer/gbuffer.h"
#include "engine/renderer/screen_quad.h"
#include "engine/renderer/light_volume.h"
#include "engine/profiling/gpu_profiler.h"

#include <entt/entt.hpp>

struct RenderContext; // Forward declaration

class RenderPipeline {
public:
    void init(RenderContext& ctx);
    void execute(RenderContext& ctx);
    void shutdown();

    // Apply a new configuration. Resizes shadow maps, rebuilds
    // SSAO kernel, etc. as needed.
    void applyConfig(const RenderPipelineConfig& config, RenderContext& ctx);

    const RenderPipelineConfig& getConfig() const { return m_config; }

private:
    RenderPipelineConfig m_config;
    RenderBatcher        m_batcher;
    TiledLightCuller     m_lightCuller;

    void renderShadows(RenderContext& ctx);
    void renderGBuffer(RenderContext& ctx);
    void renderSSAO(RenderContext& ctx);
    void renderSSAOBlur(RenderContext& ctx);
    void renderDeferredLighting(RenderContext& ctx);
    void renderSkybox(RenderContext& ctx);
    void renderForwardTransparent(RenderContext& ctx);
    void renderViewModels(RenderContext& ctx);
    void renderMotionVectors(RenderContext& ctx);
    void renderTAAResolve(RenderContext& ctx);
    void renderPostProcess(RenderContext& ctx);
    void renderFXAA(RenderContext& ctx);
    void renderHUD(RenderContext& ctx);
};
```

```cpp
// src/engine/renderer/render_pipeline.cpp — updated execute()
#include "engine/renderer/render_pipeline.h"
#include "engine/profiling/profiler.h"

void RenderPipeline::execute(RenderContext& ctx) {
    auto& gpu = GPUProfiler::instance();
    gpu.collectResults();

    // ── 1. Shadows ─────────────────────────────────────────
    if (m_config.shadowsEnabled) {
        GPU_PROFILE_BEGIN("Shadows");
        renderShadows(ctx);
        GPU_PROFILE_END("Shadows");
    }

    // ── 2. G-buffer ────────────────────────────────────────
    GPU_PROFILE_BEGIN("G-Buffer");
    renderGBuffer(ctx);
    GPU_PROFILE_END("G-Buffer");

    // ── 3. Motion vectors (TAA only) ───────────────────────
    if (m_config.aaMode == AAMode::TAA) {
        GPU_PROFILE_BEGIN("Velocity");
        renderMotionVectors(ctx);
        GPU_PROFILE_END("Velocity");
    }

    // ── 4. SSAO ────────────────────────────────────────────
    if (m_config.ssaoEnabled) {
        GPU_PROFILE_BEGIN("SSAO");
        renderSSAO(ctx);
        GPU_PROFILE_END("SSAO");

        GPU_PROFILE_BEGIN("SSAO Blur");
        renderSSAOBlur(ctx);
        GPU_PROFILE_END("SSAO Blur");
    }

    // ── 5. Deferred lighting ───────────────────────────────
    GPU_PROFILE_BEGIN("Lighting");
    renderDeferredLighting(ctx);
    GPU_PROFILE_END("Lighting");

    // ── 6. Skybox ──────────────────────────────────────────
    GPU_PROFILE_BEGIN("Skybox");
    renderSkybox(ctx);
    GPU_PROFILE_END("Skybox");

    // ── 7. Forward transparency ────────────────────────────
    GPU_PROFILE_BEGIN("Forward");
    renderForwardTransparent(ctx);
    GPU_PROFILE_END("Forward");

    // ── 8. View models ─────────────────────────────────────
    GPU_PROFILE_BEGIN("View Model");
    renderViewModels(ctx);
    GPU_PROFILE_END("View Model");

    // ── 9. TAA resolve ─────────────────────────────────────
    if (m_config.aaMode == AAMode::TAA) {
        GPU_PROFILE_BEGIN("TAA Resolve");
        renderTAAResolve(ctx);
        GPU_PROFILE_END("TAA Resolve");
    }

    // ── 10. Post-processing ────────────────────────────────
    if (m_config.bloomEnabled) {
        GPU_PROFILE_BEGIN("Post-Process");
        renderPostProcess(ctx);
        GPU_PROFILE_END("Post-Process");
    }

    // ── 11. FXAA ───────────────────────────────────────────
    if (m_config.aaMode == AAMode::FXAA) {
        GPU_PROFILE_BEGIN("FXAA");
        renderFXAA(ctx);
        GPU_PROFILE_END("FXAA");
    }

    // ── 12. HUD ────────────────────────────────────────────
    renderHUD(ctx);
}
```

The deferred lighting pass now branches on the lighting mode:

```cpp
void RenderPipeline::renderDeferredLighting(RenderContext& ctx) {
    ctx.sceneFBO.bind();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    if (m_config.lightingMode == LightingMode::Tiled) {
        // Tiled deferred — single full-screen pass
        auto shader = ctx.shaders.get("deferred_tiled_lighting");
        if (!shader) return;

        // Gather point lights
        std::vector<TiledPointLight> pointLights;
        for (auto [entity, light, pos] :
             ctx.registry.view<PointLightComponent, Position>().each()) {
            pointLights.push_back({pos.value, light.radius,
                                   light.colour, light.intensity});
        }

        // Cull lights to tiles
        m_lightCuller.cullLights(pointLights,
            ctx.camera.getViewMatrix(), ctx.camera.getProjectionMatrix(),
            ctx.window.getWidth(), ctx.window.getHeight());

        shader->use();
        ctx.gbuffer.bindForReading(GL_TEXTURE0);
        shader->setInt("gPosition", 0);
        shader->setInt("gNormal", 1);
        shader->setInt("gAlbedoSpec", 2);
        shader->setInt("gMaterial", 3);

        // SSAO texture
        if (m_config.ssaoEnabled) {
            glActiveTexture(GL_TEXTURE4);
            glBindTexture(GL_TEXTURE_2D, ctx.ssaoBlurBuffer.getTexture());
            shader->setInt("ssaoTexture", 4);
            shader->setInt("ssaoEnabled", 1);
        } else {
            shader->setInt("ssaoEnabled", 0);
        }

        // Bind tiled light data
        m_lightCuller.bind(GL_TEXTURE5, GL_TEXTURE6);
        shader->setInt("lightDataTBO", 5);
        shader->setInt("tileIndexTBO", 6);
        shader->setInt("tileInfoTBO", 7);
        shader->setInt("tilesX", m_lightCuller.getTileCountX());

        shader->setVec3("camPos", ctx.camera.getPosition());
        shader->setVec2("screenSize", glm::vec2(ctx.window.getWidth(),
                                                  ctx.window.getHeight()));

        // Directional light
        auto dirView = ctx.registry.view<DirectionalLight>();
        if (!dirView.empty()) {
            auto [e, dl] = *dirView.each().begin();
            shader->setVec3("dirLightDir", dl.direction);
            shader->setVec3("dirLightColour", dl.colour);
            shader->setFloat("dirLightIntensity", dl.intensity);
            shader->setInt("hasDirLight", 1);
        } else {
            shader->setInt("hasDirLight", 0);
        }

        glDisable(GL_DEPTH_TEST);
        ctx.screenQuad.draw();
        glEnable(GL_DEPTH_TEST);

    } else {
        // Per-light stencil volumes — original Ch 52 approach
        auto dirShader = ctx.shaders.get("deferred_lighting");
        auto volShader = ctx.shaders.get("deferred_light_volume");
        if (!dirShader || !volShader) return;

        std::vector<DeferredDirLight> dirLights;
        std::vector<DeferredPointLight> pointLights;

        for (auto [entity, light] :
             ctx.registry.view<DirectionalLight>().each())
            dirLights.push_back({light.direction, light.colour,
                                 light.intensity});

        for (auto [entity, light, pos] :
             ctx.registry.view<PointLightComponent, Position>().each())
            pointLights.push_back({pos.value, light.colour,
                                   light.intensity, light.radius});

        deferredLightingPass(
            ctx.gbuffer, ctx.screenQuad, ctx.lightVolume,
            *dirShader, *volShader,
            ctx.camera.getPosition(), ctx.camera.getViewMatrix(),
            ctx.camera.getProjectionMatrix(),
            dirLights, pointLights,
            ctx.shadowMap, ctx.lightSpaceMatrix,
            ctx.window.getWidth(), ctx.window.getHeight());
    }
}
```

### Settings Menu Integration

The quality preset selector connects to the settings menu from Chapter 22. Players can choose a preset, and individual settings adjust accordingly:

```cpp
// In the settings menu ImGui code (from Ch 22, updated for Ch 55a)
void drawGraphicsSettings(entt::registry& registry) {
    auto& config = registry.ctx().get<RenderPipelineConfig>();

    ImGui::Text("Graphics Quality");
    ImGui::Separator();

    // Preset selector
    const char* presets[] = {"Low", "Medium", "High", "Ultra"};
    int current = static_cast<int>(config.activePreset);
    if (current > 3) current = 2;  // Custom defaults to High in the selector

    if (ImGui::Combo("Quality", &current, presets, 4)) {
        auto preset = static_cast<QualityPreset>(current);
        config = QualityPresets::fromPreset(preset);

        // Apply the new config to the pipeline
        auto& pipeline = registry.ctx().get<RenderPipeline>();
        pipeline.applyConfig(config, registry.ctx().get<RenderContext>());
    }

    ImGui::Spacing();

    // Individual settings (switching any marks preset as Custom)
    bool changed = false;

    changed |= ImGui::Checkbox("Shadows", &config.shadowsEnabled);
    changed |= ImGui::Checkbox("SSAO", &config.ssaoEnabled);
    changed |= ImGui::Checkbox("Bloom", &config.bloomEnabled);

    const char* aaModes[] = {"None", "FXAA", "TAA"};
    int aaIdx = static_cast<int>(config.aaMode);
    if (ImGui::Combo("Anti-Aliasing", &aaIdx, aaModes, 3)) {
        config.aaMode = static_cast<AAMode>(aaIdx);
        changed = true;
    }

    const char* lightModes[] = {"Per-Light", "Tiled"};
    int lightIdx = static_cast<int>(config.lightingMode);
    if (ImGui::Combo("Lighting", &lightIdx, lightModes, 2)) {
        config.lightingMode = static_cast<LightingMode>(lightIdx);
        changed = true;
    }

    if (config.shadowsEnabled) {
        const char* shadowRes[] = {"512", "1024", "2048", "4096"};
        int shadowIdx = 0;
        if (config.shadowMapResolution >= 4096) shadowIdx = 3;
        else if (config.shadowMapResolution >= 2048) shadowIdx = 2;
        else if (config.shadowMapResolution >= 1024) shadowIdx = 1;
        if (ImGui::Combo("Shadow Resolution", &shadowIdx, shadowRes, 4)) {
            int resolutions[] = {512, 1024, 2048, 4096};
            config.shadowMapResolution = resolutions[shadowIdx];
            changed = true;
        }
    }

    if (config.ssaoEnabled) {
        changed |= ImGui::SliderInt("SSAO Samples", &config.ssaoSamples, 8, 64);
        changed |= ImGui::SliderFloat("SSAO Radius", &config.ssaoRadius, 0.1f, 1.0f);
    }

    changed |= ImGui::SliderFloat("LOD Bias", &config.lodBias, 0.25f, 2.0f);

    if (changed) {
        config.activePreset = QualityPreset::Custom;
        auto& pipeline = registry.ctx().get<RenderPipeline>();
        pipeline.applyConfig(config, registry.ctx().get<RenderContext>());
    }
}
```

### Applying Configuration Changes

When the configuration changes, the pipeline resizes resources as needed:

```cpp
void RenderPipeline::applyConfig(const RenderPipelineConfig& config,
                                  RenderContext& ctx) {
    RenderPipelineConfig old = m_config;
    m_config = config;

    // Resize shadow map if resolution changed
    if (config.shadowMapResolution != old.shadowMapResolution
        && ctx.shadowMap) {
        ctx.shadowMap->resize(config.shadowMapResolution,
                              config.shadowMapResolution);
    }

    // Rebuild SSAO kernel if sample count changed
    if (config.ssaoSamples != old.ssaoSamples && ctx.ssaoManager) {
        ctx.ssaoManager->rebuildKernel(config.ssaoSamples);
    }

    // Update SSAO radius
    if (ctx.ssaoManager) {
        ctx.ssaoManager->setRadius(config.ssaoRadius);
    }

    // Update LOD parameters
    // (These are read each frame from the config, so no rebuild needed)
}
```

---

## Step 4: GPU Timestamp Profiling Per Pass

Chapter 55 built the `GPUProfiler` with manual `beginPass`/`endPass` calls scattered through `execute()`. With the pipeline config, we can generate profiling data that reflects which passes are active:

```cpp
// Updated profiling overlay — reflects pipeline config
void ProfilingOverlay::renderGPUBreakdown() {
    auto& gpu = GPUProfiler::instance();

    if (!ImGui::CollapsingHeader("GPU Passes", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    // Show pipeline config alongside timings
    auto& config = m_registry->ctx().get<RenderPipelineConfig>();
    ImGui::Text("Quality: %s", QualityPresets::presetName(config.activePreset));
    ImGui::Text("Lighting: %s",
        config.lightingMode == LightingMode::Tiled ? "Tiled" : "Per-Light");
    ImGui::Text("AA: %s",
        config.aaMode == AAMode::TAA ? "TAA" :
        config.aaMode == AAMode::FXAA ? "FXAA" : "None");
    ImGui::Separator();

    float totalMs = gpu.getTotalGPUMs();

    for (const auto& name : gpu.getPassOrder()) {
        const auto& timer = gpu.getTimers().at(name);
        if (timer.lastMs < 0.001f && timer.averageMs < 0.001f) continue;

        float fraction = (totalMs > 0.0f) ? timer.averageMs / totalMs : 0.0f;
        fraction = std::clamp(fraction, 0.0f, 1.0f);

        char label[128];
        snprintf(label, sizeof(label), "%s: %.2f ms (avg) / %.2f ms (peak)",
                 name.c_str(), timer.averageMs, timer.peakMs);

        if (timer.averageMs > 3.0f) {
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                                  ImVec4(0.9f, 0.3f, 0.2f, 1.0f));
        }

        ImGui::ProgressBar(fraction, ImVec2(300, 16), label);

        if (timer.averageMs > 3.0f) {
            ImGui::PopStyleColor();
        }
    }

    // Batcher stats
    ImGui::Separator();
    ImGui::Text("Batches: %d  Instances: %d",
        m_batcher->getBatchCount(), m_batcher->getInstanceCount());
    ImGui::Text("Tiled lights visible: %d",
        m_lightCuller->getVisibleLightCount());
}
```

---

## Step 5: Updated Initialisation

Here is how everything connects at startup:

```cpp
// In Game::init() — Chapter 55a consolidated rendering init

// ─── Config ─────────────────────────────────────────────────
auto& config = registry.ctx().get<ConfigManager>();

// ─── Render pipeline config ─────────────────────────────────
RenderPipelineConfig renderCfg = loadRenderConfig(config);
registry.ctx().emplace<RenderPipelineConfig>(renderCfg);

// Register for hot-reload
config.onReload([&registry](const ConfigManager& cfg) {
    auto newCfg = loadRenderConfig(cfg);
    registry.ctx().get<RenderPipelineConfig>() = newCfg;
    auto& pipeline = registry.ctx().get<RenderPipeline>();
    pipeline.applyConfig(newCfg, registry.ctx().get<RenderContext>());
});

// ─── Pipeline init ──────────────────────────────────────────
auto& pipeline = registry.ctx().emplace<RenderPipeline>();
pipeline.init(renderContext);
pipeline.applyConfig(renderCfg, renderContext);
```

And in the frame loop:

```cpp
void PlayingState::update(float dt) {
    auto& profiler = Profiler::instance();
    profiler.resetDrawCalls();

    auto& config = m_registry.ctx().get<RenderPipelineConfig>();

    {
        ScopedTimer t(profiler, "Physics");
        m_physicsSystem.update(m_registry, dt);
    }
    {
        ScopedTimer t(profiler, "AI");
        m_aiSystem.update(m_registry, dt);
    }
    {
        ScopedTimer t(profiler, "Animation");
        m_animationSystem.update(m_registry, dt);
    }
    {
        ScopedTimer t(profiler, "Particles");
        m_particleSystem.update(m_registry, dt);
    }
    {
        ScopedTimer t(profiler, "Cull + LOD");
        glm::mat4 vp = m_camera.getProjectionMatrix() * m_camera.getViewMatrix();
        m_frustum.update(vp);
        cullAndLODSystem(m_registry, m_frustum, m_camera.getPosition(),
                         config.lodBias, config.lodHysteresis,
                         config.lodTransitionSpeed, dt, m_cullAndLODStats);
    }
    {
        ScopedTimer t(profiler, "Render");
        auto& pipeline = m_registry.ctx().get<RenderPipeline>();
        pipeline.execute(m_renderContext);
    }

    profiler.setEntityCount(
        static_cast<int>(m_registry.storage<entt::entity>().in_use()));
    profiler.endFrame();
}
```

---

## C++ Concept: Compile-Time Configuration with Policy-Based Design

The `RenderPipelineConfig` is a runtime configuration object -- the player can change settings from the menu. But there is a class of settings that never change at runtime: platform capabilities, build type (debug vs release), and feature support (does this GPU have buffer textures?). For these, runtime branches add overhead on every frame.

C++ templates let you make these decisions at compile time. This is **policy-based design**: parameterise a class on its behaviour using template arguments.

### The Pattern

```cpp
// A lighting policy defines how lights are processed.
// The choice is made at compile time — no runtime branch.

struct PerLightPolicy {
    static void processLights(const GBuffer& gbuffer,
                               const std::vector<PointLight>& lights,
                               const RenderContext& ctx) {
        // Stencil-based per-light volumes (Ch 52)
        for (const auto& light : lights) {
            renderLightVolume(light, gbuffer, ctx);
        }
    }
};

struct TiledPolicy {
    static void processLights(const GBuffer& gbuffer,
                               const std::vector<PointLight>& lights,
                               const RenderContext& ctx) {
        // Tiled deferred with buffer textures
        TiledLightCuller culler;
        culler.cullAndRender(lights, gbuffer, ctx);
    }
};

// The pipeline is parameterised on the lighting policy
template <typename LightingPolicy>
class TypedRenderPipeline {
public:
    void renderDeferredLighting(const RenderContext& ctx) {
        // No runtime branch — the compiler inlines the policy
        LightingPolicy::processLights(ctx.gbuffer, ctx.lights, ctx);
    }
};

// At build time, choose the policy:
#if defined(QENGINE_HAS_BUFFER_TEXTURES)
using GamePipeline = TypedRenderPipeline<TiledPolicy>;
#else
using GamePipeline = TypedRenderPipeline<PerLightPolicy>;
#endif
```

The compiler generates code for exactly one policy. There is no `if` statement in the compiled binary. The unused policy is never instantiated.

### When to Use This

Policy-based design is appropriate when:

1. **The choice is fixed for a build.** Platform capabilities do not change at runtime. If your minimum spec requires OpenGL 4.3, you always have buffer textures.

2. **The hot path benefits from inlining.** The lighting pass runs every frame. Eliminating a branch and enabling the compiler to inline the policy function saves a few nanoseconds per pixel -- meaningful at 2 million pixels per frame.

3. **The alternatives have different data requirements.** Per-light volumes need a `LightVolume` mesh. Tiled deferred needs a `TiledLightCuller` with buffer textures. Carrying both at runtime wastes memory.

### When Not to Use This

For QEngine, we use runtime configuration (`RenderPipelineConfig`) because the player can change settings from the menu. The overhead of a runtime branch in `execute()` -- one `if` per frame, 13 times -- is immeasurable. Policy-based design becomes relevant in AAA engines where the rendering backend is chosen at build time (DirectX 12 vs Vulkan vs Metal) and every nanosecond of the frame budget is allocated.

The takeaway: know the pattern exists, understand when it eliminates overhead, but prefer the simpler runtime approach unless profiling proves it matters.

---

## File Summary

| File | Change |
|---|---|
| `src/engine/renderer/render_batcher.h` | **New.** `RenderBatcher` class -- groups entities by (VAO, material) for instanced drawing. |
| `src/engine/renderer/render_batcher.cpp` | **New.** Batch building, instance buffer upload, instanced draw calls. |
| `src/engine/renderer/tiled_light_culler.h` | **New.** `TiledLightCuller` -- CPU-side light-to-tile assignment with buffer texture upload. |
| `src/engine/renderer/tiled_light_culler.cpp` | **New.** Tile intersection test, flat index list construction, GPU resource management. |
| `src/engine/renderer/render_pipeline_config.h` | **New.** `RenderPipelineConfig` struct, `AAMode`, `LightingMode`, `QualityPreset` enums. |
| `src/engine/renderer/quality_presets.h` | **New.** `QualityPresets::low()` through `ultra()`, `fromPreset()`, `presetName()`. |
| `src/engine/renderer/render_pipeline.h` | **Rewritten.** Added `RenderPipelineConfig` member, `applyConfig()`, `RenderBatcher` and `TiledLightCuller` members. |
| `src/engine/renderer/render_pipeline.cpp` | **Rewritten.** `execute()` reads config to enable/disable passes. Lighting pass branches on `LightingMode`. GPU profiling macros wrap every pass. |
| `src/engine/profiling/profiling_overlay.cpp` | **Updated.** Shows pipeline config, batcher stats, tiled light count. Skips inactive passes. |
| `assets/shaders/gbuffer_instanced.vert` | **New.** G-buffer vertex shader reading model matrix from instance attributes. |
| `assets/shaders/deferred_tiled_lighting.frag` | **New.** Full-screen tiled deferred lighting with buffer texture light lookup. |
| `config.lua` | **Updated.** Added `rendering` section with quality preset and individual overrides. |

---

## The Journey Complete

Fifty-six chapters. Eleven cleanup passes. One engine.

Look at what you built.

In Chapter 0, you had an empty window. A black rectangle with a title bar. `glfwCreateWindow`, `gladLoadGLLoader`, `glfwSwapBuffers`. Nothing else.

In Chapter 5, you drew a textured triangle and called it progress.

By Chapter 10, you had a physics simulation -- gravity, collision detection, rigid body response. Entities moved through the world and bounced off walls.

By Chapter 20, you had a game. An FPS with weapons, enemies, health, damage, particles, screen shake, and a HUD. It was rough, but it was playable. You could shoot things and they would react.

By Chapter 30, you had a rendering pipeline. Shadow maps cast shadows from the sun. Post-processing added bloom and tone mapping. Font rendering displayed text on screen. The developer console let you type commands to inspect and modify the world at runtime.

By Chapter 40, you had animation. Skeletal meshes with bone hierarchies played walk cycles, attack sequences, and death animations. Animation events triggered sound effects and particle bursts at precise keyframes. Ragdoll physics took over when enemies died, their limbs collapsing under gravity. Inverse kinematics planted feet on uneven terrain.

By Chapter 50, you had an editor and a pipeline. The level editor placed, moved, and rotated entities with gizmos. The property panel exposed every component for inspection. Undo/redo tracked every change. Lua scripts drove entity behaviour with hot-reload. The asset compiler cooked binary formats with dependency tracking.

And now, after Chapter 55a, you have a production rendering stack. Deferred rendering handles hundreds of lights. Tiled culling ensures only relevant lights are evaluated per pixel. SSAO darkens corners and crevices. TAA smooths edges across frames. LOD swaps mesh detail by distance. The render batcher groups identical meshes into single draw calls. Quality presets let the player balance visual fidelity against frame rate. And the profiler tells you exactly which pass costs how many milliseconds, on both the CPU and GPU, every single frame.

Every system in this engine exists because you wrote it. There is no black box. When a shadow acne artefact appears, you know to adjust the bias in `shadow_depth.frag` because you wrote that shader in Chapter 29. When a particle effect looks wrong, you know the `ParticleEffectDef` fields to tweak because you built the data-driven system in Chapter 46 and cleaned it up in Chapter 45a. When the frame rate drops, you open the F2 overlay and read the numbers because you built the profiler in Chapter 55.

That understanding -- knowing every layer, every trade-off, every reason a system is built the way it is -- that is the point. Not the engine itself. The engine is a vehicle. The understanding is the destination.

So what now?

**Build a game.**

The engine is ready. It has everything a Quake-style FPS needs: movement, physics, weapons, enemies, AI pathfinding, animation, particles, deferred rendering with PBR materials, SSAO, anti-aliasing, LOD, a level editor, Lua scripting, and profiling tools. Ship a game with it. A single level. Three weapons. Five enemy types. A boss fight. A start screen and an end screen. Package it, give it a name, and put it in front of players.

You will discover, immediately, that the engine needs things you did not anticipate. The game will push back. A feature that seemed complete will turn out to be missing one critical capability. A system that worked perfectly in isolation will break under the specific load pattern your game produces. That feedback loop -- building a game on your own engine, hitting real problems, fixing them with full knowledge of the internals -- is where real engine development happens.

The tutorials are done. The engine is yours. Now make something with it.
