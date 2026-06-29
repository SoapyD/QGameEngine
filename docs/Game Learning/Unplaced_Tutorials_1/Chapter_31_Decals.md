# Chapter 31: Decals

## What You'll Learn
- What decals are and why they make game worlds feel reactive
- The difference between geometry projection decals and deferred decals
- Building a `Decal` component that stores position, normal, atlas index, and fade state
- Creating a texture atlas for multiple decal types (bullet holes, scorch marks, blood)
- Generating a quad aligned to any surface from a hit point and normal
- Batched rendering with alpha blending and depth-write-off
- Writing vertex and fragment shaders for atlas-based decals
- A stateless `decalSystem` that fades and destroys decals over time
- Spawning decals from raycast hits (integrating with the combat system from Chapter 12)
- Limiting decal count to avoid performance problems

---

## What Are Decals?

Decals are flat images projected onto surfaces in the world. Bullet holes in a wall, scorch marks around an explosion, blood splatter on a floor, cracks in concrete, footprints in mud -- these are all decals. They make the environment feel reactive. Without them, you shoot a wall and nothing happens. With them, every gunshot leaves a mark.

There are two main approaches:

1. **Geometry projection** (what we'll build): generate a small quad at the hit point, aligned to the surface normal, and render it with alpha blending. Simple, works with forward rendering, easy to understand.

2. **Deferred decals** (advanced): render a box volume in the deferred pass, project the decal texture using the depth buffer to reconstruct world position. More powerful (handles curved surfaces and corners), but requires a deferred rendering pipeline. We won't implement this, but it's worth knowing about if you move to deferred rendering later.

```
Before decals:                    After decals:

┌──────────────────┐              ┌──────────────────┐
│                  │              │    o    *         │
│      Wall        │              │      Wall    o   │
│                  │              │  *        o      │
│                  │              │        *         │
└──────────────────┘              └──────────────────┘

Flat, lifeless surface            Bullet holes (o) and
                                  scorch marks (*) tell
                                  a story
```

---

## The Decal Component

Following QEngine's ECS rules: components have no behaviour, systems have no state. The `Decal` component is pure data.

```cpp
// In src/engine/ecs/components.h

struct Decal {
    glm::vec3 position;          // World position (hit point on surface)
    glm::vec3 normal;            // Surface normal at hit point
    float size = 0.1f;           // Half-size of the decal quad
    int textureIndex = 0;        // Index into decal texture atlas
    float lifetime = 10.0f;      // Seconds before fade starts
    float fadeTime = 2.0f;       // Seconds to fully fade out
    float age = 0.0f;            // Current age in seconds
    float alpha = 1.0f;          // Current opacity (1.0 = fully visible)
};
```

Each field serves a clear purpose:
- **position** and **normal** come from the raycast hit (Chapter 12). They define where the decal goes and which direction it faces.
- **size** controls how large the decal appears. Bullet holes are small (0.05), explosions are large (0.5).
- **textureIndex** selects which image to use from the atlas (see next section).
- **lifetime** is how long the decal stays fully visible before it starts fading.
- **fadeTime** is how long the fade takes once it begins.
- **age** and **alpha** are updated each frame by the decal system.

---

## Decal Texture Atlas

Instead of binding a different texture for each decal type, we pack all decal images into a single texture called an **atlas**. This lets us render all decals in one draw call.

```
Decal Atlas (512x512, 4x4 grid = 16 slots)

    col 0      col 1      col 2      col 3
  ┌──────────┬──────────┬──────────┬──────────┐
  │  Bullet  │  Bullet  │  Scorch  │  Scorch  │  row 0
  │  Hole 1  │  Hole 2  │  Mark 1  │  Mark 2  │
  ├──────────┼──────────┼──────────┼──────────┤
  │  Blood   │  Blood   │  Blood   │  Crack   │  row 1
  │  Splat 1 │  Splat 2 │  Splat 3 │  Small   │
  ├──────────┼──────────┼──────────┼──────────┤
  │  Crack   │  Foot-   │  Foot-   │  Slash   │  row 2
  │  Large   │  print L │  print R │  Mark    │
  ├──────────┼──────────┼──────────┼──────────┤
  │  Burn    │  Ice     │  Acid    │ (empty)  │  row 3
  │  Mark    │  Mark    │  Splash  │          │
  └──────────┴──────────┴──────────┴──────────┘

  Each cell is 128x128 pixels.
  textureIndex 0 = top-left, 1 = next in row, etc.
```

### UV Calculation

Given a `textureIndex`, we calculate which row and column it falls in, then compute UV coordinates for that cell:

```cpp
// In src/engine/renderer/decal_renderer.h

constexpr int DECAL_ATLAS_COLUMNS = 4;
constexpr int DECAL_ATLAS_ROWS    = 4;

struct DecalUVs {
    glm::vec2 bottomLeft;
    glm::vec2 bottomRight;
    glm::vec2 topRight;
    glm::vec2 topLeft;
};

inline DecalUVs getDecalUVs(int textureIndex) {
    int col = textureIndex % DECAL_ATLAS_COLUMNS;
    int row = textureIndex / DECAL_ATLAS_COLUMNS;

    float cellW = 1.0f / static_cast<float>(DECAL_ATLAS_COLUMNS);
    float cellH = 1.0f / static_cast<float>(DECAL_ATLAS_ROWS);

    // UV origin is bottom-left in OpenGL
    // Row 0 of our atlas is at the TOP of the image, so we flip Y
    float u0 = col * cellW;
    float v0 = 1.0f - (row + 1) * cellH;   // Bottom of this cell
    float u1 = (col + 1) * cellW;
    float v1 = 1.0f - row * cellH;          // Top of this cell

    return {
        { u0, v0 },   // bottom-left
        { u1, v0 },   // bottom-right
        { u1, v1 },   // top-right
        { u0, v1 }    // top-left
    };
}
```

### Named Constants for Readability

```cpp
// In src/engine/ecs/components.h (or a separate decal_types.h)

namespace DecalType {
    constexpr int BulletHole1  = 0;
    constexpr int BulletHole2  = 1;
    constexpr int ScorchMark1  = 2;
    constexpr int ScorchMark2  = 3;
    constexpr int BloodSplat1  = 4;
    constexpr int BloodSplat2  = 5;
    constexpr int BloodSplat3  = 6;
    constexpr int CrackSmall   = 7;
    constexpr int CrackLarge   = 8;
    constexpr int FootprintL   = 9;
    constexpr int FootprintR   = 10;
    constexpr int SlashMark    = 11;
    constexpr int BurnMark     = 12;
    constexpr int IceMark      = 13;
    constexpr int AcidSplash   = 14;
}
```

---

## Creating a Decal Quad

This is the core geometry problem: given a point on a surface and the surface normal, generate four vertices for a flat quad that lies flush against that surface.

### Building a Tangent Frame

A tangent frame is three perpendicular axes at a point on a surface: the **normal** (pointing away from the surface), the **tangent** (pointing "right" along the surface), and the **bitangent** (pointing "up" along the surface). We need the tangent and bitangent to offset our four vertices from the centre point.

```
              normal (N)
                ↑
                |
                |
    ────────────●────────────  surface
               /
              /
        tangent (T)

    bitangent (B) = N x T   (points "up" along the surface)
```

The trick is: we only have the normal. We need to derive tangent and bitangent from it. The cross product of two vectors gives a vector perpendicular to both. So we pick an arbitrary reference direction, cross it with the normal to get the tangent, then cross the normal and tangent to get the bitangent.

There is one degenerate case: if the normal is parallel to our chosen reference direction, the cross product is zero. We handle this by picking a different reference.

```cpp
// In src/engine/renderer/decal_renderer.cpp

struct DecalVertex {
    glm::vec3 position;
    glm::vec2 uv;
    float alpha;
};

void buildTangentFrame(const glm::vec3& normal,
                       glm::vec3& outTangent,
                       glm::vec3& outBitangent) {
    // Pick an arbitrary "up" direction to cross with the normal.
    // If the normal is nearly parallel to our choice, pick a different one.
    glm::vec3 up(0.0f, 1.0f, 0.0f);

    if (std::abs(glm::dot(normal, up)) > 0.99f) {
        // Normal is nearly vertical — use +X instead
        up = glm::vec3(1.0f, 0.0f, 0.0f);
    }

    outTangent   = glm::normalize(glm::cross(normal, up));
    outBitangent = glm::normalize(glm::cross(normal, outTangent));
}
```

### Generating the Quad Vertices

With the tangent frame, we offset from the centre point to produce four corners. A tiny offset along the normal (0.001 units) pushes the quad just in front of the surface, preventing z-fighting.

```
        Z-fighting (no offset):          With normal offset:

        Surface ──────────────           Surface ──────────────
        Decal   ──────────────           Decal   ─── (0.001 in front)

        The GPU can't decide which       The decal is always in front.
        pixel is in front. Result:       Clean rendering.
        flickering, shimmering mess.
```

```cpp
// In src/engine/renderer/decal_renderer.cpp

constexpr float DECAL_NORMAL_OFFSET = 0.001f;

void buildDecalQuad(const Decal& decal, DecalVertex outVerts[6]) {
    glm::vec3 tangent, bitangent;
    buildTangentFrame(decal.normal, tangent, bitangent);

    // Offset slightly along normal to prevent z-fighting
    glm::vec3 centre = decal.position + decal.normal * DECAL_NORMAL_OFFSET;

    // Four corners of the quad
    glm::vec3 halfT = tangent * decal.size;
    glm::vec3 halfB = bitangent * decal.size;

    glm::vec3 bl = centre - halfT - halfB;   // bottom-left
    glm::vec3 br = centre + halfT - halfB;   // bottom-right
    glm::vec3 tr = centre + halfT + halfB;   // top-right
    glm::vec3 tl = centre - halfT + halfB;   // top-left

    // UV coordinates from atlas
    DecalUVs uvs = getDecalUVs(decal.textureIndex);

    // Two triangles (6 vertices)
    outVerts[0] = { bl, uvs.bottomLeft,  decal.alpha };
    outVerts[1] = { br, uvs.bottomRight, decal.alpha };
    outVerts[2] = { tr, uvs.topRight,    decal.alpha };

    outVerts[3] = { bl, uvs.bottomLeft,  decal.alpha };
    outVerts[4] = { tr, uvs.topRight,    decal.alpha };
    outVerts[5] = { tl, uvs.topLeft,     decal.alpha };
}
```

### Projection onto a Wall

```
Side view — decal projected onto a wall:

          Wall surface
             │
             │    ← normal points left (away from wall)
             │
     tl ─────●───── tr
             │
     bl ─────●───── br        Quad lies flat against the wall.
             │                 Tangent/bitangent run along the wall surface.
             │                 Normal offset (0.001) prevents z-fighting.
             │

Top-down view:

     ════════╤════════  wall
             │
             ● ← 0.001 offset along normal
             │
        decal quad (edge-on, paper-thin)
```

---

## Decal Shaders

### assets/shaders/decal.vert

```glsl
#version 460 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aUV;
layout (location = 2) in float aAlpha;

out vec2 TexCoords;
out float Alpha;

uniform mat4 view;
uniform mat4 projection;

void main() {
    TexCoords = aUV;
    Alpha = aAlpha;

    // No model matrix — decal vertices are already in world space
    gl_Position = projection * view * vec4(aPos, 1.0);
}
```

### assets/shaders/decal.frag

```glsl
#version 460 core

in vec2 TexCoords;
in float Alpha;

out vec4 FragColour;

uniform sampler2D decalAtlas;

void main() {
    vec4 texColour = texture(decalAtlas, TexCoords);

    // Multiply texture alpha by the fade alpha from the component
    texColour.a *= Alpha;

    // Discard nearly transparent fragments — avoids writing to depth
    // buffer for invisible pixels and saves fill rate
    if (texColour.a < 0.01) {
        discard;
    }

    FragColour = texColour;
}
```

The vertex shader is intentionally simple. Decal vertices are built in world space by `buildDecalQuad`, so we skip the model matrix entirely. The fragment shader samples the atlas, combines the texture's own alpha with the fade alpha from the component, and discards invisible fragments.

---

## DecalRenderer Class

The renderer collects all active decals into a single dynamic VBO each frame, then draws them in one batch. This is efficient because decals are small, share the same shader and texture, and change every frame (as they fade).

```cpp
// In src/engine/renderer/decal_renderer.h

#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <entt/entt.hpp>

class Shader;

class DecalRenderer {
public:
    DecalRenderer() = default;
    ~DecalRenderer();

    // Non-copyable (owns GPU resources)
    DecalRenderer(const DecalRenderer&) = delete;
    DecalRenderer& operator=(const DecalRenderer&) = delete;

    // Initialise the VAO and VBO. Call once at startup.
    void init(int maxDecals = 256);

    // Render all Decal entities in the registry.
    void render(entt::registry& registry,
                const Shader& shader,
                GLuint atlasTexture,
                const glm::mat4& view,
                const glm::mat4& projection);

private:
    GLuint m_VAO = 0;
    GLuint m_VBO = 0;
    int m_maxDecals = 256;

    void cleanup();
};
```

```cpp
// In src/engine/renderer/decal_renderer.cpp

#include "engine/renderer/decal_renderer.h"
#include "engine/renderer/shader.h"
#include "engine/ecs/components.h"

#include <vector>
#include <cmath>

// --- DecalVertex, buildTangentFrame, buildDecalQuad, getDecalUVs
//     (defined earlier in this chapter) ---

// ─── Lifecycle ──────────────────────────────────────────────────

DecalRenderer::~DecalRenderer() {
    cleanup();
}

void DecalRenderer::cleanup() {
    if (m_VAO) { glDeleteVertexArrays(1, &m_VAO); m_VAO = 0; }
    if (m_VBO) { glDeleteBuffers(1, &m_VBO);       m_VBO = 0; }
}

// ─── Initialisation ─────────────────────────────────────────────

void DecalRenderer::init(int maxDecals) {
    m_maxDecals = maxDecals;

    glGenVertexArrays(1, &m_VAO);
    glGenBuffers(1, &m_VBO);

    glBindVertexArray(m_VAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);

    // Allocate enough space for max decals * 6 vertices each
    size_t bufferSize = m_maxDecals * 6 * sizeof(DecalVertex);
    glBufferData(GL_ARRAY_BUFFER, bufferSize, nullptr, GL_DYNAMIC_DRAW);

    // Position — location 0, 3 floats
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                          sizeof(DecalVertex),
                          (void*)offsetof(DecalVertex, position));

    // UV — location 1, 2 floats
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
                          sizeof(DecalVertex),
                          (void*)offsetof(DecalVertex, uv));

    // Alpha — location 2, 1 float
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE,
                          sizeof(DecalVertex),
                          (void*)offsetof(DecalVertex, alpha));

    glBindVertexArray(0);
}

// ─── Rendering ──────────────────────────────────────────────────

void DecalRenderer::render(entt::registry& registry,
                           const Shader& shader,
                           GLuint atlasTexture,
                           const glm::mat4& view,
                           const glm::mat4& projection) {

    auto decalView = registry.view<Decal>();
    if (decalView.size_hint() == 0) return;

    // Build vertex data for all active decals
    std::vector<DecalVertex> vertices;
    vertices.reserve(m_maxDecals * 6);

    for (auto [entity, decal] : decalView.each()) {
        if (decal.alpha <= 0.0f) continue;

        DecalVertex quad[6];
        buildDecalQuad(decal, quad);

        for (int i = 0; i < 6; i++) {
            vertices.push_back(quad[i]);
        }
    }

    if (vertices.empty()) return;

    // Upload vertex data to GPU
    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    vertices.size() * sizeof(DecalVertex),
                    vertices.data());

    // Set render state for decals
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);   // Depth write OFF — decals don't occlude each other

    // Bind shader and set uniforms
    shader.use();
    shader.setMat4("view", view);
    shader.setMat4("projection", projection);

    // Bind the decal atlas texture
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlasTexture);
    shader.setInt("decalAtlas", 0);

    // Draw all decals in one call
    glBindVertexArray(m_VAO);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
    glBindVertexArray(0);

    // Restore render state
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}
```

### Why Depth Write Off?

With depth writes enabled, the first decal drawn at a position would write to the depth buffer, and any overlapping decal behind it would fail the depth test. You'd get missing decals where two bullet holes overlap. With depth writes off, all decals blend correctly on top of each other and on top of the geometry behind them. Depth **testing** stays on so decals still respect walls and floors (a decal on a far wall won't draw on top of a near wall).

---

## The Decal System

A stateless free function. It ages decals, fades them when they exceed their lifetime, and destroys fully-faded entities.

```cpp
// In src/engine/ecs/systems/decal_system.h

#pragma once

#include <entt/entt.hpp>

void decalSystem(entt::registry& registry, float dt);
```

```cpp
// In src/engine/ecs/systems/decal_system.cpp

#include "engine/ecs/systems/decal_system.h"
#include "engine/ecs/components.h"

void decalSystem(entt::registry& registry, float dt) {
    // Collect entities to destroy (can't destroy during iteration)
    std::vector<entt::entity> toDestroy;

    auto view = registry.view<Decal>();

    for (auto [entity, decal] : view.each()) {
        // Age the decal
        decal.age += dt;

        if (decal.age > decal.lifetime) {
            // Fade phase: linearly reduce alpha over fadeTime seconds
            float fadeProgress = (decal.age - decal.lifetime) / decal.fadeTime;
            decal.alpha = 1.0f - fadeProgress;

            if (decal.alpha <= 0.0f) {
                decal.alpha = 0.0f;
                toDestroy.push_back(entity);
            }
        }
    }

    // Destroy fully faded decals
    for (auto entity : toDestroy) {
        registry.destroy(entity);
    }
}
```

The fade timeline:

```
alpha
1.0 ┤████████████████████████████████──────────
    │                                ──────────
    │  fully visible                    fading
0.0 ┤                                          ●  destroyed
    └──────────────────┬──────────────┬─────────
                    lifetime      lifetime +
                    (10s)         fadeTime (12s)
```

---

## Spawning Decals

When a weapon fires and the raycast hits a surface (Chapter 12), we spawn a decal at the hit point. This is a free function — no state, no class.

```cpp
// In src/engine/ecs/systems/decal_helpers.h

#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>

constexpr int MAX_DECALS = 256;

// Spawn a new decal entity at the given position and surface normal.
// Returns the created entity.
entt::entity spawnDecal(entt::registry& registry,
                        const glm::vec3& position,
                        const glm::vec3& normal,
                        int textureIndex,
                        float size = 0.1f);
```

```cpp
// In src/engine/ecs/systems/decal_helpers.cpp

#include "engine/ecs/systems/decal_helpers.h"
#include "engine/ecs/components.h"

#include <limits>

// Find the oldest decal entity in the registry
static entt::entity findOldestDecal(entt::registry& registry) {
    entt::entity oldest = entt::null;
    float maxAge = -1.0f;

    auto view = registry.view<Decal>();
    for (auto [entity, decal] : view.each()) {
        if (decal.age > maxAge) {
            maxAge = decal.age;
            oldest = entity;
        }
    }

    return oldest;
}

entt::entity spawnDecal(entt::registry& registry,
                        const glm::vec3& position,
                        const glm::vec3& normal,
                        int textureIndex,
                        float size) {

    // Enforce decal limit — destroy oldest if at capacity
    auto decalView = registry.view<Decal>();
    if (decalView.size_hint() >= MAX_DECALS) {
        entt::entity oldest = findOldestDecal(registry);
        if (oldest != entt::null) {
            registry.destroy(oldest);
        }
    }

    // Create the new decal entity
    auto entity = registry.create();
    registry.emplace<Decal>(entity, Decal{
        .position     = position,
        .normal       = glm::normalize(normal),
        .size         = size,
        .textureIndex = textureIndex,
        .lifetime     = 10.0f,
        .fadeTime     = 2.0f,
        .age          = 0.0f,
        .alpha        = 1.0f
    });

    return entity;
}
```

### Integration with Combat

In the combat system (or wherever raycast hits are processed), call `spawnDecal`:

```cpp
// In your combat/weapon firing code (Chapter 12)

void handleWeaponHit(entt::registry& registry,
                     const RaycastHit& hit,
                     int weaponType) {
    // Choose decal type based on what was hit and how
    int decalIndex = DecalType::BulletHole1;
    float decalSize = 0.05f;

    if (weaponType == WeaponID::Shotgun) {
        decalIndex = DecalType::BulletHole2;
        decalSize = 0.04f;
    } else if (weaponType == WeaponID::RocketLauncher) {
        decalIndex = DecalType::ScorchMark1;
        decalSize = 0.4f;
    }

    // Did we hit an enemy? Use blood instead
    if (registry.all_of<Health>(hit.entity)) {
        decalIndex = DecalType::BloodSplat1;
        decalSize = 0.08f;
    }

    spawnDecal(registry, hit.point, hit.normal, decalIndex, decalSize);
}
```

---

## Decal Limits

Decals accumulate fast. A player spraying a machine gun for ten seconds could create hundreds. Each decal is 6 vertices uploaded to the GPU every frame. Without a limit, performance degrades.

Our approach is simple: cap at `MAX_DECALS` (256). When we hit the limit, destroy the oldest decal before spawning a new one. This means old bullet holes in hallways the player passed through five minutes ago quietly vanish, while fresh damage near the player stays visible. Players rarely notice.

256 decals at 6 vertices each is 1,536 vertices per frame -- trivial for a modern GPU. You could raise this to 512 or 1024 without concern.

---

## Render Order

Decals slot into QEngine's render pipeline after opaque geometry but before transparent effects:

```
1. Shadow pass          (render to shadow map)
2. Skybox               (GL_LEQUAL depth, always at max depth)
3. Opaque geometry      (depth test ON, depth write ON)
4. Decals               (depth test ON, depth write OFF, alpha blend)   ← HERE
5. Transparent/particles (depth test ON, depth write OFF, alpha blend)
6. View model           (clear depth, narrow FOV)
7. Post-processing      (full-screen quad)
8. HUD                  (orthographic, depth test OFF)
```

Why after opaque geometry? Because decals need the depth buffer to be fully populated so they correctly sit on surfaces. Why before particles? Because particles are also transparent, and decals are attached to surfaces (effectively "more opaque" than floating particles). Both use alpha blending, but ordering decals first avoids decals incorrectly appearing on top of nearby particle effects.

### Updated PlayingState::render()

```cpp
void PlayingState::render() {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glm::mat4 view = m_camera.getViewMatrix();
    glm::mat4 projection = m_camera.getProjectionMatrix();

    // 1. Skybox
    m_skybox.render(m_skyboxShader, view, projection);

    // 2. Opaque geometry
    renderSystem(m_registry, m_camera);

    // 3. Decals (alpha blend, depth write off)
    m_decalRenderer.render(m_registry, m_decalShader,
                           m_decalAtlasTexture, view, projection);

    // 4. Transparent / particles
    particleSystem(m_registry, m_camera);

    // 5. View model
    renderViewModel(m_registry, m_viewModelShader,
                    m_window.getAspectRatio());

    // 6. HUD
    hudSystem(m_registry, m_window);
}
```

---

## Putting It All Together

Here is the full file listing for this chapter:

```
src/engine/ecs/components.h              — Decal struct, DecalType constants
src/engine/ecs/systems/decal_system.h    — decalSystem() declaration
src/engine/ecs/systems/decal_system.cpp  — ages, fades, and destroys decals
src/engine/ecs/systems/decal_helpers.h   — spawnDecal() declaration
src/engine/ecs/systems/decal_helpers.cpp — spawning with limit enforcement
src/engine/renderer/decal_renderer.h     — DecalRenderer class, UV helpers
src/engine/renderer/decal_renderer.cpp   — quad generation, batched rendering
assets/shaders/decal.vert                — MVP transform, pass UVs and alpha
assets/shaders/decal.frag                — atlas sampling, alpha fade, discard
```

The update loop calls `decalSystem` to age and fade. The render loop calls `DecalRenderer::render` to draw. The combat system calls `spawnDecal` when hits are detected. Each piece is independent, and the data flows through the `Decal` component.

---

## C++ Concept: `glm::cross` and Building Orthonormal Bases

The cross product is one of the most important operations in 3D math. Given two vectors **A** and **B**, `glm::cross(A, B)` returns a new vector perpendicular to both.

```cpp
glm::vec3 a(1.0f, 0.0f, 0.0f);   // +X
glm::vec3 b(0.0f, 1.0f, 0.0f);   // +Y
glm::vec3 c = glm::cross(a, b);   // (0, 0, 1) = +Z
```

The result follows the **right-hand rule**: curl the fingers of your right hand from A toward B, and your thumb points in the direction of the cross product.

### Building an Orthonormal Basis from a Single Vector

An orthonormal basis is three mutually perpendicular unit vectors. When we build a decal, we start with only the surface normal -- one vector. We need to derive two more (tangent and bitangent) that are perpendicular to it and to each other.

The algorithm:
1. Pick an arbitrary reference direction (we use world up: `(0, 1, 0)`)
2. Cross the normal with the reference to get the tangent
3. Cross the normal with the tangent to get the bitangent
4. Normalize everything

```cpp
glm::vec3 normal(0.0f, 0.0f, 1.0f);   // Wall facing +Z
glm::vec3 up(0.0f, 1.0f, 0.0f);

glm::vec3 tangent   = glm::normalize(glm::cross(normal, up));
// tangent = normalize(cross((0,0,1), (0,1,0))) = normalize((−1,0,0)) = (−1,0,0)

glm::vec3 bitangent = glm::normalize(glm::cross(normal, tangent));
// bitangent = normalize(cross((0,0,1), (−1,0,0))) = normalize((0,−1,0)) = (0,−1,0)
```

### The Degenerate Case

If the normal is parallel to our chosen reference, the cross product is zero (or nearly zero), and normalizing a zero vector is undefined. This happens when the surface normal points straight up or straight down -- directly along `(0, 1, 0)`.

```cpp
glm::vec3 normal(0.0f, 1.0f, 0.0f);  // Floor — points up
glm::vec3 up(0.0f, 1.0f, 0.0f);

glm::cross(normal, up);  // (0, 0, 0) — DEGENERATE! Can't normalize this.
```

The fix: detect the case with `glm::dot` and switch to a different reference.

```cpp
glm::vec3 up(0.0f, 1.0f, 0.0f);

if (std::abs(glm::dot(normal, up)) > 0.99f) {
    up = glm::vec3(1.0f, 0.0f, 0.0f);  // Switch to +X
}
```

`glm::dot` gives the cosine of the angle between two unit vectors. When it is close to 1.0 (or -1.0), the vectors are nearly parallel. The 0.99 threshold corresponds to about 8 degrees -- well within safety.

This technique of building an orthonormal basis from a single direction shows up constantly in game development: decal placement, normal mapping, camera orientation, procedural mesh generation, and more. It is worth understanding deeply.

---

## What's Next

In **Chapter 32**, we'll implement **frustum culling** -- testing whether objects are inside the camera's view before sending them to the GPU. Right now QEngine submits every entity for rendering every frame, even those behind the camera. Frustum culling skips invisible objects entirely, which is one of the biggest performance wins in any 3D engine.
