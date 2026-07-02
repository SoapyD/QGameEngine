# Chapter 51: Level of Detail

## What You'll Learn
- Why rendering full-detail meshes at every distance wastes GPU time
- Discrete LOD — storing multiple mesh versions and selecting by distance
- The `LODGroup` component and `LODLevel` data structure
- Distance-based LOD selection with hysteresis to prevent flickering
- Cross-fade dithering in the fragment shader for smooth LOD transitions
- Billboard impostors for extreme distances
- Integrating LOD selection with frustum culling (Ch 32) and instanced rendering (Ch 38)
- LOD bias — a configurable quality vs performance slider via ConfigManager
- Screen-space metrics — projecting bounding spheres to pixel size for accurate LOD selection

---

## The Problem: Detail Nobody Can See

Stand in a QEngine level and look at an enemy 100 metres away. That enemy model has 10,000 triangles. At that distance, the enemy occupies maybe 20 pixels on screen. Each pixel covers roughly 500 triangles. The GPU dutifully rasterises all 10,000 triangles and then most of those triangles map to sub-pixel fragments that get discarded or blended into the same pixel.

```
Close up (5m):                    Far away (100m):

   Enemy fills ~400x600 pixels       Enemy fills ~15x20 pixels
   10,000 triangles visible          10,000 triangles rendered
   Good use of geometry              Massive waste

   ┌──────────────────────┐          ┌──────────────────────┐
   │                      │          │                      │
   │    ╔══════════╗      │          │                      │
   │    ║          ║      │          │         .:           │
   │    ║  Enemy   ║      │          │         ''           │
   │    ║  model   ║      │          │                      │
   │    ║  10K tri ║      │          │   (still 10K tri!)   │
   │    ║          ║      │          │                      │
   │    ╚══════════╝      │          │                      │
   │                      │          │                      │
   └──────────────────────┘          └──────────────────────┘
```

**Level of Detail (LOD)** solves this by swapping in simpler geometry when the object is far away. At 100 metres, a 200-triangle version looks identical to the 10,000-triangle version because the detail is below the pixel resolution anyway. The GPU saves 9,800 triangles worth of vertex processing and rasterisation.

This is one of the oldest tricks in real-time graphics, and it remains one of the most effective. Every shipped game uses LOD in some form.

---

## Discrete LOD

The simplest and most widely used approach is **discrete LOD**: store 2-4 pre-built mesh versions per model, each with progressively fewer triangles. At runtime, pick the version that matches the object's screen size.

```
LOD0 (full detail)    LOD1 (medium)       LOD2 (low)          LOD3 (billboard)
  10,000 triangles      2,500 triangles     500 triangles       2 triangles
  0 - 20m               20 - 60m            60 - 150m           150m+

     /\                    /\                  /\
    /  \                  /  \                / \                ┌──────┐
   / /\ \               / /\ \              /   \               │      │
  / /  \ \             / /__\ \            /_____\              │ img  │
 / /    \ \           /________\          /       \             │      │
/_________ \                             /_________\            └──────┘
  Detail!             Simplified         Blocky but OK         Flat image
```

The key insight: each LOD level looks the same at its intended distance. LOD1 looks identical to LOD0 when the object is 30 metres away. LOD2 looks identical to LOD1 when the object is 80 metres away. The player never notices the difference.

LOD meshes are generated offline — either hand-authored by artists, or automatically decimated by tools like MeshLab, Simplygon, or Meshoptimizer. For this chapter, we assume they already exist as separate `.qmesh` files (e.g. `enemy_grunt_lod0.qmesh`, `enemy_grunt_lod1.qmesh`). Chapter 50's asset pipeline can add a decimation step, but the algorithm itself is outside our scope.

---

## The LODLevel and LODGroup Structures

Pure data, no behaviour — following QEngine's ECS conventions:

```cpp
// src/engine/ecs/components/lod_group.h

#pragma once

#include <string>
#include <vector>
#include <glad/glad.h>

namespace qe {

struct LODLevel {
    std::string meshName;       // Key into ResourceManager / AssetCache
    GLuint      vao       = 0;  // Resolved at load time
    int         indexCount = 0;  // Triangle count for this level
    float       maxDistance = 0.0f;  // Switch to next LOD beyond this distance
};

struct LODGroup {
    std::vector<LODLevel> levels;   // Sorted by distance: LOD0 first, LOD3 last
    int    activeLOD      = 0;      // Currently selected level index
    int    previousLOD    = 0;      // Previous frame's level (for transition detection)
    float  transitionAlpha = 1.0f;  // 1.0 = fully transitioned, < 1.0 = cross-fading
    bool   isBillboard    = false;  // True when activeLOD is the billboard level
    float  boundingRadius = 1.0f;   // For screen-space size calculation
};

} // namespace qe
```

The `levels` vector is sorted from highest detail (LOD0) to lowest detail (LOD3). Each level specifies the maximum distance at which it should be used. Beyond that distance, the system switches to the next level.

```
Distance:  0         20m        60m        150m       ∞
           |----------|----------|----------|----------|
  LOD:     |  LOD0    |  LOD1    |  LOD2    |  LOD3    |
           | 10K tri  | 2.5K tri | 500 tri  | billboard|
```

### Setting Up LOD Groups

When we create an entity with LOD support, we populate its `LODGroup` in setup code:

```cpp
// In setupScene() or level loading code

// Helper to add a LOD level from ResourceManager
LODLevel makeLODLevel(ResourceManager& res, const std::string& meshName, float maxDist) {
    LODLevel l;
    l.meshName    = meshName;
    l.vao         = res.getMeshVAO(meshName);
    l.indexCount   = res.getMeshIndexCount(meshName);
    l.maxDistance  = maxDist;
    return l;
}

void createEnemyGrunt(entt::registry& registry, const glm::vec3& position,
                      ResourceManager& resources) {
    auto entity = registry.create();

    registry.emplace<Position>(entity, position);
    registry.emplace<Rotation>(entity, glm::quat(1, 0, 0, 0));
    registry.emplace<Scale>(entity, glm::vec3(1.0f));

    // Material (shared across all LODs — textures don't change)
    Material mat;
    mat.diffuseTexture = resources.getTexture("enemy_grunt_diffuse");
    mat.normalMap      = resources.getTexture("enemy_grunt_normal");
    mat.mapFlags       = MapFlags::Diffuse | MapFlags::Normal;
    registry.emplace<Material>(entity, mat);

    // LOD group — 4 levels sorted by distance
    LODGroup lod;
    lod.levels.push_back(makeLODLevel(resources, "enemy_grunt_lod0", 20.0f));   // 10K tri
    lod.levels.push_back(makeLODLevel(resources, "enemy_grunt_lod1", 60.0f));   // 2.5K tri
    lod.levels.push_back(makeLODLevel(resources, "enemy_grunt_lod2", 150.0f));  // 500 tri
    lod.levels.push_back(makeLODLevel(resources, "quad", 9999.0f));             // billboard
    lod.boundingRadius = 1.2f;
    registry.emplace<LODGroup>(entity, std::move(lod));

    // MeshRenderer starts at LOD0 — the LOD system updates it each frame
    MeshRenderer renderer;
    renderer.vao        = lod.levels[0].vao;
    renderer.indexCount  = lod.levels[0].indexCount;
    registry.emplace<MeshRenderer>(entity, renderer);
}
```

The entity still has a `MeshRenderer`. The LOD system updates its VAO and index count each frame. The render system draws whatever `MeshRenderer` says — it never knows about LOD.

---

## Distance-Based LOD Selection with Hysteresis

Imagine an entity exactly 20 metres from the camera. One frame it's at 19.98m (LOD0). Next frame the camera sways slightly and it's at 20.02m (LOD1). Next frame, 19.99m (LOD0 again). The mesh flickers between two detail levels every frame. The player will absolutely notice this.

The fix is **hysteresis**: use different thresholds for switching up versus switching down. If you're currently at LOD0 and the distance exceeds 20m, switch to LOD1. But to switch *back* to LOD0, the distance must drop below 18m (a 10% margin). The entity must move a meaningful distance before it switches back.

```
Without hysteresis:                  With hysteresis:

Distance: ~~~19.9~20.1~19.8~20.2    Distance: ~~~19.9~20.1~19.8~20.2
LOD:      ...LOD0.LOD1.LOD0.LOD1     LOD:      ...LOD0.LOD1.LOD1.LOD1
              ↑ flicker!                                   ↑ stable
                                     (won't drop back until distance < 18m)
```

The algorithm is straightforward: only allow LOD to change by one level per frame, and require the entity to cross back past a tighter threshold before switching to higher detail. This prevents skipping from LOD0 to LOD3 in a single frame and eliminates the boundary flutter. We will implement this directly in the combined cull-and-LOD system later in the chapter.

---

## Cross-Fade Dithering

Even with hysteresis, the LOD switch is still a hard pop — one frame you see the detailed mesh, next frame you see the simplified one. For slow-moving objects or objects near the transition distance, this is visible.

The classic solution is **screen-door transparency**: during the transition, render the mesh with a dithered alpha pattern. Pixels are either fully on or fully off based on a repeating pattern. From a distance, the eye blends the pattern into a semi-transparent surface.

```
Dither pattern during transition:

  alpha = 0.0 (start)    alpha = 0.5 (midway)    alpha = 1.0 (done)

  . . . . . . . .        # . # . # . # .          # # # # # # # #
  . . . . . . . .        . # . # . # . #          # # # # # # # #
  . . . . . . . .        # . # . # . # .          # # # # # # # #
  . . . . . . . .        . # . # . # . #          # # # # # # # #

  (fully discarded)      (checkerboard)            (fully visible)
```

### The Dither Fragment Shader

We add a `lodTransitionAlpha` uniform and a dither test to the fragment shader. When `lodTransitionAlpha` is 1.0, no pixels are discarded and the shader behaves normally.

```glsl
// assets/shaders/lit_lod.frag
#version 330 core

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoord;

uniform sampler2D diffuseMap;
uniform sampler2D normalMap;
uniform sampler2D specularMap;
uniform int       mapFlags;

uniform vec3  lightDir;
uniform vec3  lightColour;
uniform vec3  ambientColour;
uniform float shininess;

// LOD transition
uniform float lodTransitionAlpha;   // 0.0 = fully transparent, 1.0 = fully opaque

out vec4 FragColor;

// 4x4 Bayer dither matrix — values from 0.0 to 1.0
// This gives 16 distinct threshold levels, which is enough for a smooth fade
float bayerDither(vec2 screenPos) {
    // Map fragment position to a 4x4 grid cell
    int x = int(mod(screenPos.x, 4.0));
    int y = int(mod(screenPos.y, 4.0));

    // Bayer 4x4 matrix (normalised to 0..1 range)
    // Each value is (matrix_value + 0.5) / 16.0
    const float bayer[16] = float[16](
         0.03125, 0.53125, 0.15625, 0.65625,
         0.78125, 0.28125, 0.90625, 0.40625,
         0.21875, 0.71875, 0.09375, 0.59375,
         0.96875, 0.46875, 0.84375, 0.34375
    );

    return bayer[y * 4 + x];
}

void main() {
    // LOD dither test — discard pixels below the threshold
    if (lodTransitionAlpha < 1.0) {
        float threshold = bayerDither(gl_FragCoord.xy);
        if (lodTransitionAlpha < threshold) {
            discard;
        }
    }

    // Normal lighting calculation (same as existing lit shader)
    vec3 norm = normalize(Normal);
    float diff = max(dot(norm, -lightDir), 0.0);
    vec3 lighting = ambientColour + lightColour * diff;

    vec4 texel = texture(diffuseMap, TexCoord);
    FragColor = vec4(texel.rgb * lighting, texel.a);
}
```

The Bayer matrix distributes thresholds evenly across a 4x4 block. At 50% alpha, exactly 8 of the 16 pixels pass — a perfect checkerboard. The pattern is spatially uniform (no clumping), and the implementation is a lookup: one modulo, one array access, one comparison. On modern GPUs this is essentially free.

### Setting the Uniform

In the render system, set `lodTransitionAlpha` before drawing each entity:

```cpp
// In the render loop, after binding the shader and before glDrawElements

void renderEntityWithLOD(const Shader& shader,
                         const LODGroup& lod,
                         const MeshRenderer& mesh,
                         const glm::mat4& modelMatrix) {
    shader.setFloat("lodTransitionAlpha", lod.transitionAlpha);
    shader.setMat4("model", modelMatrix);

    glBindVertexArray(mesh.vao);
    glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr);
}
```

When `transitionAlpha` is 1.0, the `if (lodTransitionAlpha < 1.0)` branch is never entered, so there is zero cost for non-transitioning entities. The `discard` only fires during the brief transition window.

### Configuring Transition Duration

The transition speed controls how fast `transitionAlpha` ramps from 0 to 1. We expose all LOD settings through ConfigManager (details in the LOD Bias section below).

---

## Billboard Impostors

At extreme distances, even a 500-triangle mesh is overkill. The object is 5-10 pixels on screen. A **billboard impostor** replaces the 3D mesh with a flat 2D image rendered on a camera-facing quad.

```
3D mesh at 200m:                Billboard at 200m:

  Still processes 500 vertices    Processes 4 vertices
  Still runs lighting calcs       One texture sample
  Takes ~500 vertex shader        Takes 1 vertex shader
    invocations                     invocation (instanced: 4)

  Looks identical at that distance.
```

### How Impostors Work

1. **Offline**: Render the 3D model from 8-16 angles around the Y axis. Store these as a texture atlas.
2. **Runtime**: When the LOD system selects the billboard level, render a camera-facing quad with the appropriate sub-image from the atlas.

```
Impostor atlas layout (8 angles):

  ┌────┬────┬────┬────┬────┬────┬────┬────┐
  │ 0° │ 45°│ 90°│135°│180°│225°│270°│315°│
  │    │    │    │    │    │    │    │    │
  └────┴────┴────┴────┴────┴────┴────┴────┘

  At runtime, pick the angle closest to the
  camera-to-entity direction and UV into that cell.
```

### The Impostor Component

```cpp
// src/engine/ecs/components/impostor.h

#pragma once

#include <glad/glad.h>

namespace qe {

struct Impostor {
    GLuint atlasTexture = 0;   // Texture atlas with pre-rendered views
    int    angleCount   = 8;   // Number of views in the atlas (columns)
    float  width        = 1.0f;  // World-space quad width
    float  height       = 2.0f;  // World-space quad height
};

} // namespace qe
```

### Billboard Vertex Shader

The billboard vertex shader constructs a camera-facing quad. It takes the entity's world position as a uniform and expands the quad vertices to always face the camera.

```glsl
// assets/shaders/billboard.vert
#version 330 core

layout (location = 0) in vec3 aPos;      // Quad vertices: (-0.5,-0.5), (0.5,-0.5), etc.
layout (location = 2) in vec2 aTexUV;

uniform mat4  view;
uniform mat4  projection;
uniform vec3  entityPosition;   // World position of the entity
uniform float billboardWidth;
uniform float billboardHeight;

// For impostor atlas selection
uniform float uvOffsetX;        // Horizontal offset into the atlas
uniform float uvScaleX;         // Width of one atlas cell (1.0 / angleCount)

out vec2 TexCoord;

void main() {
    // Extract camera right and up vectors from the view matrix
    // These are the first two columns of the INVERSE view matrix,
    // which are the first two ROWS of the view matrix (since it's orthogonal)
    vec3 cameraRight = vec3(view[0][0], view[1][0], view[2][0]);
    vec3 cameraUp    = vec3(view[0][1], view[1][1], view[2][1]);

    // Expand the quad in camera space
    vec3 worldPos = entityPosition
                  + cameraRight * aPos.x * billboardWidth
                  + cameraUp    * aPos.y * billboardHeight;

    gl_Position = projection * view * vec4(worldPos, 1.0);

    // Map UV into the correct atlas cell
    TexCoord = vec2(aTexUV.x * uvScaleX + uvOffsetX, aTexUV.y);
}
```

The billboard fragment shader is straightforward: sample the impostor atlas, apply the same Bayer dither test from `lit_lod.frag` for LOD transitions, and discard fully transparent pixels (`texel.a < 0.1`) for alpha cutout. Since the dither function is identical, you can extract it into a shared GLSL include file if your shader system supports `#include`.

### Selecting the Atlas Angle

At render time, compute the angle between the camera-to-entity direction and the entity's forward direction, then pick the closest atlas cell:

```cpp
// src/engine/renderer/impostor_renderer.h

#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>

namespace qe {

inline int selectImpostorAngle(const glm::vec3& cameraPos,
                               const glm::vec3& entityPos,
                               int angleCount) {
    glm::vec3 toCamera = cameraPos - entityPos;
    float angle = std::atan2(toCamera.x, toCamera.z);  // Angle around Y axis

    // Map from [-pi, pi] to [0, 2*pi]
    if (angle < 0.0f) {
        angle += glm::two_pi<float>();
    }

    // Map to atlas index
    float cellAngle = glm::two_pi<float>() / static_cast<float>(angleCount);
    int index = static_cast<int>(std::round(angle / cellAngle)) % angleCount;
    return index;
}

inline float impostorUVOffset(int angleIndex, int angleCount) {
    return static_cast<float>(angleIndex) / static_cast<float>(angleCount);
}

inline float impostorUVScale(int angleCount) {
    return 1.0f / static_cast<float>(angleCount);
}

} // namespace qe
```

In QEngine, impostors are only used for the *last* LOD level (LOD3). LOD0 through LOD2 are real meshes. The billboard is only visible at distances where the object occupies very few pixels, so the lack of real lighting and the flat silhouette are imperceptible. Impostors work best for complex-silhouette objects (enemies, trees) that appear in large numbers. Simple shapes like crates are already cheap enough at LOD2 that a billboard buys little.

---

## LOD-Aware Frustum Culling

Chapter 32 introduced frustum culling — skip entities outside the camera's view. LOD selection and frustum culling are both per-entity tests that happen before rendering. The order matters:

1. **Frustum cull first** — why compute LOD for an entity that's off-screen?
2. **Then select LOD** — only for visible entities

This is a simple optimisation but it adds up. If 70% of entities are culled, LOD selection runs on 30% of the entities instead of 100%.

```
Frame pipeline:

  All entities (500)
       │
       ▼
  Frustum cull (Ch 32)
       │
       ├── Culled: 350 entities (skipped entirely)
       │
       ▼
  LOD selection (150 remaining)
       │
       ├── LOD0: 20 entities
       ├── LOD1: 45 entities
       ├── LOD2: 60 entities
       └── LOD3: 25 entities (billboards)
       │
       ▼
  Render (sorted by material, then LOD)
```

### Integrated Cull + LOD System

```cpp
// src/engine/ecs/systems/cull_and_lod_system.h

#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include "src/engine/renderer/frustum.h"
#include "src/engine/ecs/components/lod_group.h"
#include "src/engine/ecs/components/components.h"

namespace qe {

struct CullAndLODStats {
    int totalEntities    = 0;
    int culledEntities   = 0;
    int visibleEntities  = 0;
    int lod0Count        = 0;
    int lod1Count        = 0;
    int lod2Count        = 0;
    int lod3Count        = 0;
    int transitionCount  = 0;
};

inline void cullAndLODSystem(entt::registry& registry,
                             const Frustum& frustum,
                             const glm::vec3& cameraPosition,
                             float lodBias,
                             float lodHysteresis,
                             float lodTransitionSpeed,
                             float dt,
                             CullAndLODStats& stats) {
    stats = {};

    auto view = registry.view<Position, MeshRenderer>();

    for (auto entity : view) {
        stats.totalEntities++;

        auto& pos = view.get<Position>(entity);

        // ── Step 1: Frustum cull ──────────────────────────────
        // Use the entity's bounding radius for a sphere test.
        // MeshRenderer stores boundingRadius (set during mesh loading).
        auto& mesh = view.get<MeshRenderer>(entity);
        float cullRadius = mesh.boundingRadius;

        // If this entity has an LODGroup, use its bounding radius instead
        // (it may be larger to account for animations)
        LODGroup* lod = registry.try_get<LODGroup>(entity);
        if (lod) {
            cullRadius = lod->boundingRadius;
        }

        if (!frustum.isSphereInside(pos.value, cullRadius)) {
            stats.culledEntities++;
            mesh.visible = false;  // The render system checks this flag
            continue;
        }

        mesh.visible = true;
        stats.visibleEntities++;

        // ── Step 2: LOD selection (only for entities with LODGroup) ──
        if (!lod) continue;  // No LOD — render at full detail

        float distance = glm::length(cameraPosition - pos.value);
        float biasedDistance = distance / lodBias;

        int current = lod->activeLOD;
        int levelCount = static_cast<int>(lod->levels.size());
        int selectedLOD = current;

        // Check switch to lower detail
        if (current < levelCount - 1) {
            if (biasedDistance > lod->levels[current].maxDistance) {
                selectedLOD = current + 1;
            }
        }

        // Check switch to higher detail (with hysteresis)
        if (current > 0) {
            float switchBack = lod->levels[current - 1].maxDistance
                             * (1.0f - lodHysteresis);
            if (biasedDistance < switchBack) {
                selectedLOD = current - 1;
            }
        }

        lod->previousLOD = lod->activeLOD;
        lod->activeLOD = selectedLOD;
        lod->isBillboard = (selectedLOD == levelCount - 1)
                           && lod->levels.back().indexCount <= 6;

        // Update MeshRenderer
        const LODLevel& level = lod->levels[selectedLOD];
        mesh.vao        = level.vao;
        mesh.indexCount  = level.indexCount;

        // Transition
        if (lod->activeLOD != lod->previousLOD) {
            lod->transitionAlpha = 0.0f;
        }
        if (lod->transitionAlpha < 1.0f) {
            lod->transitionAlpha += dt * lodTransitionSpeed;
            if (lod->transitionAlpha > 1.0f) lod->transitionAlpha = 1.0f;
            stats.transitionCount++;
        }

        switch (selectedLOD) {
            case 0: stats.lod0Count++; break;
            case 1: stats.lod1Count++; break;
            case 2: stats.lod2Count++; break;
            default: stats.lod3Count++; break;
        }
    }
}

} // namespace qe
```

### The `visible` Flag

We add a `visible` flag to `MeshRenderer`:

```cpp
// In src/engine/ecs/components/components.h — update MeshRenderer

struct MeshRenderer {
    GLuint vao          = 0;
    int    indexCount    = 0;
    float  boundingRadius = 1.0f;
    bool   visible      = true;   // Set by cull system, read by render system
};
```

The render system checks `visible` before drawing:

```cpp
// In the render system loop
for (auto [entity, pos, mesh, mat] : renderView.each()) {
    if (!mesh.visible) continue;  // Culled — skip entirely

    // ... bind material, set uniforms, draw
}
```

This is the same pattern Chapter 32 established, just extended to include LOD.

---

## Integration with Instanced Rendering

Chapter 38 introduced instanced rendering for groups of identical objects. LOD adds a wrinkle: within an instance group, different instances may be at different LOD levels. You can't render them all in one draw call if they have different meshes.

The solution: **group by (mesh, LOD level)**. An instance group of 500 pine trees might split into:

```
Pine tree instance group (500 trees total):

  Before LOD:    1 draw call, 500 instances, LOD0 mesh (5000 tri each)
                 = 2,500,000 triangles

  After LOD:     LOD0: 30 instances  × 5000 tri = 150,000 tri  (1 draw call)
                 LOD1: 120 instances × 1200 tri = 144,000 tri  (1 draw call)
                 LOD2: 200 instances × 300 tri  = 60,000 tri   (1 draw call)
                 LOD3: 150 instances × 2 tri    = 300 tri      (1 draw call)
                                                  ─────────
                 Total: 354,300 triangles         4 draw calls

  Savings: 2,145,700 triangles (~86% reduction) at the cost of 3 extra draw calls
```

Four draw calls instead of one is a small price for an 86% triangle reduction. The GPU was the bottleneck (too many triangles), not the CPU (too many draw calls).

### LOD-Aware Instance Buffer Construction

```cpp
// src/engine/ecs/systems/instance_lod_system.h

#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <array>
#include <vector>
#include "src/engine/renderer/frustum.h"
#include "src/engine/ecs/components/instance_group.h"
#include "src/engine/ecs/components/lod_group.h"
#include "src/engine/renderer/instance_buffer.h"

namespace qe {

// Maximum LOD levels per instance group
constexpr int MAX_INSTANCE_LOD_LEVELS = 4;

struct InstanceLODBucket {
    GLuint vao       = 0;
    int    indexCount = 0;
    std::vector<glm::mat4> transforms;
};

struct InstanceLODGroup {
    std::string baseMeshName;
    std::array<InstanceLODBucket, MAX_INSTANCE_LOD_LEVELS> buckets;
    int lodLevelCount = 1;

    // LOD distance thresholds (shared across all instances in this group)
    std::array<float, MAX_INSTANCE_LOD_LEVELS> maxDistances = {20, 60, 150, 9999};

    // GPU buffers — one per LOD level
    std::array<GLuint, MAX_INSTANCE_LOD_LEVELS> instanceVBOs = {0, 0, 0, 0};
    std::array<std::size_t, MAX_INSTANCE_LOD_LEVELS> bufferCapacities = {0, 0, 0, 0};

    float boundingRadius = 1.0f;
};

inline void instanceLODCullSystem(entt::registry& registry,
                                  const Frustum& frustum,
                                  const glm::vec3& cameraPosition,
                                  float lodBias) {
    auto view = registry.view<InstanceLODGroup>();

    for (auto [entity, group] : view.each()) {
        // Clear all buckets
        for (int i = 0; i < group.lodLevelCount; ++i) {
            group.buckets[i].transforms.clear();
        }

        // Original InstanceGroup holds all transforms
        auto* ig = registry.try_get<InstanceGroup>(entity);
        if (!ig) continue;

        for (const auto& transform : ig->allTransforms) {
            // Extract position from the transform matrix (column 3)
            glm::vec3 instancePos(transform[3][0], transform[3][1], transform[3][2]);

            // Frustum cull this instance
            if (!frustum.isSphereInside(instancePos, group.boundingRadius)) {
                continue;
            }

            // Select LOD based on distance
            float distance = glm::length(cameraPosition - instancePos);
            float biasedDist = distance / lodBias;

            int selectedLOD = group.lodLevelCount - 1;
            for (int i = 0; i < group.lodLevelCount; ++i) {
                if (biasedDist <= group.maxDistances[i]) {
                    selectedLOD = i;
                    break;
                }
            }

            group.buckets[selectedLOD].transforms.push_back(transform);
        }

        // Upload each bucket to its instance buffer
        for (int i = 0; i < group.lodLevelCount; ++i) {
            auto& bucket = group.buckets[i];
            if (bucket.transforms.empty()) continue;

            std::size_t needed = bucket.transforms.size();
            if (needed > group.bufferCapacities[i]) {
                group.bufferCapacities[i] = needed * 2;
                glBindBuffer(GL_ARRAY_BUFFER, group.instanceVBOs[i]);
                glBufferData(GL_ARRAY_BUFFER,
                             static_cast<GLsizeiptr>(group.bufferCapacities[i] * sizeof(glm::mat4)),
                             nullptr, GL_DYNAMIC_DRAW);
            }

            uploadInstanceData(group.instanceVBOs[i], bucket.transforms);
        }
    }
}

inline void instanceLODRenderSystem(entt::registry& registry,
                                    const Shader& shader,
                                    const glm::mat4& viewMatrix,
                                    const glm::mat4& projectionMatrix) {
    shader.use();
    shader.setMat4("view", viewMatrix);
    shader.setMat4("projection", projectionMatrix);
    shader.setFloat("lodTransitionAlpha", 1.0f);  // No per-instance transition for now

    auto view = registry.view<InstanceLODGroup>();

    for (auto [entity, group] : view.each()) {
        for (int i = 0; i < group.lodLevelCount; ++i) {
            auto& bucket = group.buckets[i];
            int count = static_cast<int>(bucket.transforms.size());
            if (count == 0) continue;

            glBindVertexArray(bucket.vao);
            glDrawElementsInstanced(GL_TRIANGLES, bucket.indexCount,
                                    GL_UNSIGNED_INT, nullptr, count);
        }
    }
}

} // namespace qe
```

To set up a forest, create an entity with both an `InstanceGroup` (holding all 2000 tree transforms, same as Chapter 38) and an `InstanceLODGroup`. The LOD group defines the per-level meshes and distance thresholds. The `instanceLODCullSystem` partitions the transforms each frame, and `instanceLODRenderSystem` issues one draw call per populated bucket.

---

## LOD Bias — Quality vs Performance Slider

LOD bias is a simple multiplier that shifts all LOD transitions closer or farther. It lets the player (or the engine's auto-performance system) trade visual quality for frame rate.

```
LOD bias effect on a 20m transition distance:

  bias = 0.5:  Transitions at 10m  (aggressive — lower quality, faster)
  bias = 1.0:  Transitions at 20m  (default)
  bias = 1.5:  Transitions at 30m  (conservative — higher quality, slower)
  bias = 2.0:  Transitions at 40m  (maximum quality)
```

The math is simple: `biasedDistance = actualDistance / bias`. A higher bias makes distances appear shorter, so the engine keeps higher-detail LODs longer.

### Config Integration

```lua
-- config.lua

lod = {
    bias = 1.0,                -- 1.0 = default, > 1.0 = higher quality
    transition_speed = 4.0,    -- Alpha units per second (4.0 = 0.25s transition)
    hysteresis = 0.1,          -- 10% distance margin for switching back
}
```

### Reading LOD Config in PlayingState

```cpp
// In PlayingState::init() or wherever you set up the frame loop

void PlayingState::init() {
    // ... existing init code ...

    auto& config = m_registry.ctx().get<ConfigManager>();
    m_lodBias            = config.get<float>("lod.bias", 1.0f);
    m_lodTransitionSpeed = config.get<float>("lod.transition_speed", 4.0f);
    m_lodHysteresis      = config.get<float>("lod.hysteresis", 0.1f);

    // Register for hot-reload
    config.onReload([this](const ConfigManager& cfg) {
        m_lodBias            = cfg.get<float>("lod.bias", 1.0f);
        m_lodTransitionSpeed = cfg.get<float>("lod.transition_speed", 4.0f);
        m_lodHysteresis      = cfg.get<float>("lod.hysteresis", 0.1f);
    });
}
```

### Console Command for Runtime Tuning

```cpp
// In registerDebugCommands()

console.registerCommand("lod_bias", "Set LOD bias (0.1 - 4.0)",
    [&playingState, &console](const std::vector<std::string>& args) {
        if (args.empty()) {
            console.print("Current LOD bias: " + std::to_string(playingState.getLODBias()));
            return;
        }
        float bias = std::stof(args[0]);
        bias = glm::clamp(bias, 0.1f, 4.0f);
        playingState.setLODBias(bias);
        console.print("LOD bias set to " + std::to_string(bias));
    });

console.registerCommand("show_lod_stats", "Toggle LOD statistics display",
    [&debug, &console](const std::vector<std::string>& args) {
        debug.showLODStats = !debug.showLODStats;
        console.print(debug.showLODStats ? "LOD stats ON" : "LOD stats OFF");
    });
```

### LOD Statistics Display

```cpp
// In HUD rendering code

if (debugRenderer.showLODStats) {
    const auto& s = lodStats;
    std::string text =
        "LOD | Total: " + std::to_string(s.totalEntities) +
        " Visible: "    + std::to_string(s.visibleEntities) +
        " Culled: "     + std::to_string(s.culledEntities) +
        "\n  L0: "      + std::to_string(s.lod0Count) +
        " L1: "         + std::to_string(s.lod1Count) +
        " L2: "         + std::to_string(s.lod2Count) +
        " L3: "         + std::to_string(s.lod3Count) +
        " Trans: "      + std::to_string(s.transitionCount) +
        " Bias: "       + std::to_string(m_lodBias);

    font.renderText(text, 10, 60, 1.0f, glm::vec4(0, 1, 0, 1));
}
```

```
Example debug output:

  LOD | Total: 500  Visible: 148  Culled: 352
    L0: 18  L1: 42  L2: 63  L3: 25  Trans: 3  Bias: 1.00
```

---

## Putting It All Together — The Frame Loop

The systems connect in the same order as the pipeline diagram above:

```cpp
// In PlayingState::update(float dt)

void PlayingState::update(float dt) {
    // ... input, physics, animation updates ...

    // Update frustum from camera
    glm::mat4 vp = m_camera.getProjectionMatrix() * m_camera.getViewMatrix();
    m_frustum.update(vp);

    // Combined cull + LOD pass for individual entities
    cullAndLODSystem(m_registry, m_frustum, m_camera.getPosition(),
                     m_lodBias, m_lodHysteresis, m_lodTransitionSpeed,
                     dt, m_cullAndLODStats);

    // LOD sort for instance groups
    instanceLODCullSystem(m_registry, m_frustum, m_camera.getPosition(),
                          m_lodBias);
}
```

In the render loop, the only new logic is checking `mesh.visible` (set by the cull pass) and setting `lodTransitionAlpha` per entity. Entities with `lod->isBillboard == true` take the billboard render path. Instanced groups are drawn via `instanceLODRenderSystem` which issues one draw call per populated LOD bucket. Everything else — skybox, particles, HUD — remains unchanged.

---

## Debug Visualisation

Colour-coding entities by LOD level is invaluable during development. Add an overlay mode that tints each entity based on its active LOD:

```cpp
// Debug LOD visualisation — set a colour tint per LOD level

if (debugRenderer.showLODColours) {
    glm::vec3 lodColours[] = {
        {0.0f, 1.0f, 0.0f},   // LOD0 = green (full detail)
        {1.0f, 1.0f, 0.0f},   // LOD1 = yellow
        {1.0f, 0.5f, 0.0f},   // LOD2 = orange
        {1.0f, 0.0f, 0.0f},   // LOD3 = red (billboard)
    };

    auto* lod = m_registry.try_get<LODGroup>(entity);
    if (lod) {
        int idx = glm::clamp(lod->activeLOD, 0, 3);
        m_litLODShader.setVec3("debugTint", lodColours[idx]);
        m_litLODShader.setBool("debugTintEnabled", true);
    } else {
        m_litLODShader.setBool("debugTintEnabled", false);
    }
}
```

```
Debug view with LOD colouring:

  ┌─────────────────────────────────────┐
  │ Green Green   Yellow   Orange  Red  │
  │ (LOD0)(LOD0)  (LOD1)   (LOD2) (LOD3)│
  │                                     │
  │  █  █          ▲        ·      ·    │
  │  █  █          ▲        ·      ·    │
  │  ║  ║          │        │      │    │
  │  close         mid      far    very │
  │                                far  │
  └─────────────────────────────────────┘
```

Register the console command:

```cpp
console.registerCommand("show_lod_colours", "Toggle LOD colour overlay",
    [&debug, &console](const std::vector<std::string>& args) {
        debug.showLODColours = !debug.showLODColours;
        console.print(debug.showLODColours ? "LOD colours ON" : "LOD colours OFF");
    });
```

---

## C++ Concept Sidebar: Screen-Space Metrics

So far we've used world-space distance for LOD selection. This works, but it has a blind spot: it doesn't account for the camera's field of view or the screen resolution. A 1-metre object at 50 metres occupies more pixels on a 4K display than a 1080p display, and more pixels with a narrow FOV than a wide one.

**Screen-space LOD selection** projects the object's bounding sphere onto the screen and measures how many pixels it covers. This gives a resolution-independent and FOV-independent metric.

### The Math

Given:
- A bounding sphere with centre `C` and radius `r` in world space
- Camera position `P`
- Vertical field of view `fovY` (radians)
- Screen height in pixels `screenHeight`

The screen-space diameter in pixels is:

```
distance = length(C - P)

If distance <= r, the object fills the entire screen (LOD0).

Otherwise:
  angularSize = 2 * atan(r / distance)        // Angle subtended by the sphere
  screenFraction = angularSize / fovY          // Fraction of screen height
  screenPixels = screenFraction * screenHeight // Pixels on screen
```

### Implementation

```cpp
// src/engine/renderer/screen_metrics.h

#pragma once

#include <glm/glm.hpp>
#include <cmath>

namespace qe {

// Returns the approximate screen-space diameter in pixels of a bounding sphere.
// This accounts for FOV and screen resolution, giving a more accurate LOD metric
// than raw distance alone.
inline float screenSpaceDiameter(const glm::vec3& sphereCentre,
                                 float sphereRadius,
                                 const glm::vec3& cameraPosition,
                                 float fovY,
                                 float screenHeight) {
    float distance = glm::length(sphereCentre - cameraPosition);

    // Object engulfs the camera — treat as full screen
    if (distance <= sphereRadius) {
        return screenHeight;
    }

    // Angular size of the sphere (approximation — exact for small angles)
    float angularSize = 2.0f * std::atan(sphereRadius / distance);

    // Convert to pixels
    float screenFraction = angularSize / fovY;
    return screenFraction * screenHeight;
}

// Select LOD based on screen-space pixel size.
// thresholds[] contains minimum pixel sizes for each LOD level:
//   thresholds[0] = 200  → use LOD0 if >= 200 pixels
//   thresholds[1] = 80   → use LOD1 if >= 80 pixels
//   thresholds[2] = 20   → use LOD2 if >= 20 pixels
//   anything smaller      → use LOD3 (billboard)
inline int selectLODByScreenSize(float screenPixels,
                                 const float* thresholds,
                                 int levelCount) {
    for (int i = 0; i < levelCount - 1; ++i) {
        if (screenPixels >= thresholds[i]) {
            return i;
        }
    }
    return levelCount - 1;
}

} // namespace qe
```

### Using Screen-Space Metrics in the LOD System

You can swap the distance-based selection in `cullAndLODSystem` for screen-space selection:

```cpp
// Alternative LOD selection using screen-space size

float pixels = screenSpaceDiameter(pos.value, lod->boundingRadius,
                                   cameraPosition, fovY, screenHeight);

// Screen-space thresholds (pixels)
float thresholds[] = {200.0f, 80.0f, 20.0f};
int selectedLOD = selectLODByScreenSize(pixels, thresholds,
                                         lod->levels.size());
```

For QEngine, distance-based selection is the default because it is simpler and the thresholds (in metres) are intuitive for level designers. Screen-space selection is the better choice for games with variable FOV (sniper zoom) or that target multiple resolutions with the same LOD settings, since it accounts for both FOV and screen height automatically. The `atan` call costs roughly 5 nanoseconds per entity — negligible.

---

## Performance Impact

```
Scenario: 500 entities, typical Quake-style level
  Without LOD: 150 visible × 5000 tri = 750,000 triangles → ~3.9 ms GPU
  With LOD:    150 visible × 1200 tri = 180,000 triangles → ~1.1 ms GPU
  LOD system CPU cost: ~0.02 ms.  Net saving: ~2.8 ms/frame.

Scenario: 2000 instanced trees (forest)
  Without LOD: 800 visible × 5000 tri = 4,000,000 tri → ~11.2 ms GPU (1 draw call)
  With LOD:    800 visible, mixed LODs =   520,000 tri → ~1.5 ms GPU  (4 draw calls)
```

LOD is one of the highest-impact optimisations in a 3D engine. CPU cost is near zero, and GPU savings at typical game distances are 70-90%.

---

## Common Pitfalls

**Popping transitions** — Increase cross-fade duration, tighten hysteresis, or add more LOD levels so each step is smaller.

**Shadows using full-detail meshes** — The shadow pass should also use LOD, often more aggressively since shadow maps have lower effective resolution. We will address this in later chapters.

**Billboard impostors look wrong when lit** — They are pre-rendered with baked lighting. At 150m+ this is imperceptible. For lit impostors, store a normal map atlas alongside the colour atlas.

**Instanced LOD groups create too many draw calls** — 4 LOD levels means up to 4 draw calls per group. For 10 groups that is 40 draw calls, still vastly better than 10,000 individual calls. Merge groups that share a material if draw calls become a concern.

---

## File Summary

Here is every file we created or modified in this chapter:

| File | Status | Purpose |
|------|--------|---------|
| `src/engine/ecs/components/lod_group.h` | **New** | `LODLevel` and `LODGroup` component structs |
| `src/engine/ecs/components/impostor.h` | **New** | `Impostor` component for billboard atlas data |
| `src/engine/ecs/components/components.h` | **Modified** | Added `visible` flag to `MeshRenderer` |
| `src/engine/ecs/systems/cull_and_lod_system.h` | **New** | Combined frustum cull + LOD selection with hysteresis |
| `src/engine/ecs/systems/instance_lod_system.h` | **New** | Per-LOD bucketing for instance groups |
| `src/engine/renderer/impostor_renderer.h` | **New** | Angle selection and UV helpers for impostors |
| `src/engine/renderer/screen_metrics.h` | **New** | Screen-space bounding sphere projection |
| `assets/shaders/lit_lod.frag` | **New** | Fragment shader with Bayer dither for LOD transitions |
| `assets/shaders/billboard.vert` | **New** | Camera-facing quad vertex shader |
| `assets/shaders/billboard.frag` | **New** | Billboard fragment shader with dither and alpha cutout |
| `config.lua` | **Modified** | Added `lod` config section |

---

## What's Next

In **Chapter 52**, we'll implement **deferred rendering** — splitting the render pass into a geometry buffer (G-buffer) that stores positions, normals, and material properties per pixel, followed by a lighting pass that reads from the G-buffer. This decouples lighting cost from scene complexity, letting us render dozens of dynamic lights without multiplying draw calls. It's the foundation for the production-quality lighting pipeline that the remaining chapters in this block will build on.
