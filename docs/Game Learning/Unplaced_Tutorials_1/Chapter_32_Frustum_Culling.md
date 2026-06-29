# Chapter 32: Frustum Culling

## What You'll Learn
- Why submitting every entity to the GPU wastes CPU and GPU time
- What a view frustum is and how it's defined by six planes
- Extracting frustum planes from the view-projection matrix (Griess-Hartmann method)
- Testing points, spheres, and AABBs against the frustum
- The "p-vertex" optimisation for fast AABB-frustum tests
- Integrating culling into the render loop without breaking ECS rules
- Tracking culling statistics via the developer console
- Debug-rendering the frustum wireframe
- Sector-level culling for coarse early rejection

---

## Why Frustum Culling

Right now, the render system iterates every entity that has a `MeshRenderer` and `Position`, builds a model matrix, sets uniforms, and issues a draw call. If the entity is behind the camera, or a thousand units off to the left, the GPU will clip it during rasterisation and draw zero pixels. But the CPU still did all the work: computing the transform, binding the shader, uploading uniforms, and calling `glDrawElements`. The draw call still crossed the CPU-GPU boundary.

For a small level with 50 entities, this is fine. For a large level with 500 or 5000 entities, it's a significant waste. Frustum culling solves this by asking one cheap question before each draw call: **is this entity inside the camera's view?** If not, skip it entirely. No transform math, no uniform uploads, no draw call.

The test is conservative — it can say "maybe visible" when the entity is actually just outside the view, but it will never say "invisible" when the entity is actually visible. False positives are fine (we draw something unnecessary). False negatives would cause visible popping, and we never allow those.

---

## What Is a View Frustum

The camera's projection defines a truncated pyramid — the **view frustum**. Everything inside this shape is potentially visible. Everything outside is guaranteed to be off-screen.

```
Side view (looking along the X axis):

            Near                        Far
             |                           |
       Top   |  . . . . . . . . . . . . |
             | .                       . |
             |.                         .|
  Camera ----+---------------------------+---- Z (forward)
             |.                         .|
             | .                       . |
       Bot   |  . . . . . . . . . . . . |
             |                           |
```

```
Top view (looking down the Y axis):

            Near                        Far
             |                           |
      Left   |  . . . . . . . . . . . . |
             | .                       . |
             |.                         .|
  Camera ----+---------------------------+---- Z (forward)
             |.                         .|
             | .                       . |
      Right  |  . . . . . . . . . . . . |
             |                           |
```

The frustum is bounded by **six planes**:

| Plane  | What It Clips |
|--------|--------------|
| Near   | Objects too close to the camera |
| Far    | Objects beyond the draw distance |
| Left   | Objects to the left of the view |
| Right  | Objects to the right of the view |
| Top    | Objects above the view |
| Bottom | Objects below the view |

Each plane's normal points **inward** — toward the visible space. If a point is on the positive (inner) side of all six planes, it's inside the frustum.

---

## Plane Representation

A plane in 3D is defined by a normal vector and a distance from the origin. We store the normal pointing inward so that positive distance-to-plane means "inside the frustum".

```cpp
// In src/engine/renderer/frustum.h

struct Plane {
    glm::vec3 normal;   // Points inward (toward visible space)
    float distance;      // Distance from origin along normal

    // Signed distance from point to plane
    // Positive = inside (on the normal side)
    // Negative = outside
    float distanceTo(const glm::vec3& point) const {
        return glm::dot(normal, point) + distance;
    }
};
```

The equation `dot(normal, point) + distance` gives the signed distance from the point to the plane. This is the standard plane equation `ax + by + cz + d = 0` where `(a, b, c)` is the normal and `d` is the distance.

---

## Extracting Frustum Planes from the VP Matrix

The elegant part: you don't need to manually compute the six planes from FOV, aspect ratio, and near/far values. Given the combined view-projection matrix `M = projection * view`, each frustum plane can be extracted by adding or subtracting rows of `M`.

This is the **Griess-Hartmann method**. The matrix `M` transforms world-space points into clip space. A point is inside the frustum if `-w <= x <= w`, `-w <= y <= w`, and `-w <= z <= w` in clip space. Each of these inequalities corresponds to a frustum plane, and rearranging the math gives us the plane coefficients directly from the matrix rows.

Given matrix `M` with rows `row0`, `row1`, `row2`, `row3`:

| Plane  | Extraction |
|--------|-----------|
| Left   | `row3 + row0` |
| Right  | `row3 - row0` |
| Bottom | `row3 + row1` |
| Top    | `row3 - row1` |
| Near   | `row3 + row2` |
| Far    | `row3 - row2` |

Each extraction gives four coefficients `(a, b, c, d)` where `(a, b, c)` is the plane normal and `d` is the distance. We normalise each plane after extraction so that `distanceTo()` returns actual distances in world units.

Here is the extraction code:

```cpp
// In src/engine/renderer/frustum.cpp

#include "engine/renderer/frustum.h"
#include <glm/glm.hpp>

void Frustum::update(const glm::mat4& vp) {
    // GLM stores matrices in column-major order.
    // vp[col][row] — so vp[0][3] is column 0, row 3.
    // To get row i: (vp[0][i], vp[1][i], vp[2][i], vp[3][i])

    auto extractPlane = [&](int row, float sign) -> Plane {
        Plane p;
        p.normal.x = vp[0][3] + sign * vp[0][row];
        p.normal.y = vp[1][3] + sign * vp[1][row];
        p.normal.z = vp[2][3] + sign * vp[2][row];
        p.distance  = vp[3][3] + sign * vp[3][row];
        return p;
    };

    planes[0] = extractPlane(0,  1.0f);  // Left:   row3 + row0
    planes[1] = extractPlane(0, -1.0f);  // Right:  row3 - row0
    planes[2] = extractPlane(1,  1.0f);  // Bottom: row3 + row1
    planes[3] = extractPlane(1, -1.0f);  // Top:    row3 - row1
    planes[4] = extractPlane(2,  1.0f);  // Near:   row3 + row2
    planes[5] = extractPlane(2, -1.0f);  // Far:    row3 - row2

    // Normalise each plane so distanceTo() returns real distances
    for (auto& plane : planes) {
        float length = glm::length(plane.normal);
        plane.normal   /= length;
        plane.distance /= length;
    }
}
```

### Why Normalise?

Without normalisation, `distanceTo()` returns a value that tells you which side of the plane a point is on (positive or negative), but the magnitude is meaningless. After normalisation, the magnitude is the actual distance in world units. This matters for sphere tests — we need to compare the distance against the sphere radius.

---

## The Frustum Class

```cpp
// In src/engine/renderer/frustum.h

#pragma once

#include <glm/glm.hpp>
#include <array>

struct Plane {
    glm::vec3 normal;
    float distance;

    float distanceTo(const glm::vec3& point) const {
        return glm::dot(normal, point) + distance;
    }
};

class Frustum {
public:
    // Recalculate planes from a view-projection matrix
    void update(const glm::mat4& viewProjection);

    // Test if a point is inside the frustum
    bool isPointInside(const glm::vec3& point) const;

    // Test if a sphere intersects the frustum
    bool isSphereInside(const glm::vec3& centre, float radius) const;

    // Test if an AABB intersects the frustum
    bool isAABBInside(const glm::vec3& min, const glm::vec3& max) const;

private:
    std::array<Plane, 6> planes;
};
```

### Point Test

The simplest test. A point is inside the frustum if it's on the positive side of all six planes:

```cpp
// In src/engine/renderer/frustum.cpp

bool Frustum::isPointInside(const glm::vec3& point) const {
    for (const auto& plane : planes) {
        if (plane.distanceTo(point) < 0.0f) {
            return false;  // Outside this plane — definitely not visible
        }
    }
    return true;  // Inside all six planes
}
```

### Sphere Test

A sphere is inside (or intersecting) the frustum if its centre is not farther than `radius` outside any plane:

```cpp
// In src/engine/renderer/frustum.cpp

bool Frustum::isSphereInside(const glm::vec3& centre, float radius) const {
    for (const auto& plane : planes) {
        if (plane.distanceTo(centre) < -radius) {
            return false;  // Entire sphere is outside this plane
        }
    }
    return true;
}
```

If `distanceTo(centre)` returns `-5.0` and the radius is `3.0`, the sphere's closest point to the plane is still `2.0` units outside. So we cull it. If the distance is `-2.0` and the radius is `3.0`, the sphere crosses the plane — it's partially visible and we keep it.

### AABB Test — The P-Vertex Method

This is the most important test because QEngine entities use `AABBCollider` components. The key insight is the **p-vertex** (positive vertex): for each plane, find the corner of the AABB that is most in the direction of the plane's normal. If even that corner is outside the plane, the entire AABB must be outside.

```
P-vertex concept for a plane with normal pointing up-right:

                    normal
                      ↗
    +--------+       /
    |        |      /
    |  AABB  |     /
    |        |    /
    +--------+ ← n-vertex (most opposite to normal)
         ↑
     p-vertex (most aligned with normal)
         is at top-right corner

    If the p-vertex is on the OUTSIDE of the plane,
    the entire box is outside. Cull it.
```

For an AABB with corners `min` and `max`, the p-vertex for a given plane normal is constructed by choosing `max` on each axis where the normal is positive, and `min` where the normal is negative:

```
Example — plane normal = (0.7, 0.3, -0.5):

  X component positive → pick max.x
  Y component positive → pick max.y
  Z component negative → pick min.z

  p-vertex = (max.x, max.y, min.z)
```

```cpp
// In src/engine/renderer/frustum.cpp

bool Frustum::isAABBInside(const glm::vec3& min, const glm::vec3& max) const {
    for (const auto& plane : planes) {
        // Build the p-vertex: the corner most in the direction of the normal
        glm::vec3 pVertex;
        pVertex.x = (plane.normal.x >= 0.0f) ? max.x : min.x;
        pVertex.y = (plane.normal.y >= 0.0f) ? max.y : min.y;
        pVertex.z = (plane.normal.z >= 0.0f) ? max.z : min.z;

        // If the p-vertex is outside this plane, the entire AABB is outside
        if (plane.distanceTo(pVertex) < 0.0f) {
            return false;
        }
    }

    // The p-vertex was inside all six planes — the AABB is (likely) visible
    return true;
}
```

This test does at most 6 dot products (one per plane). Compare that to testing all 8 corners against all 6 planes (48 dot products). The p-vertex method is the standard approach used in production engines.

### Can It Give Wrong Answers?

The p-vertex test can produce **false positives** — it may say "visible" when the AABB is actually outside the frustum. This happens when the AABB is outside the frustum but its p-vertex is inside all six planes individually (the AABB spans a corner of the frustum). This is rare and harmless: we draw an extra entity that would have been clipped by the GPU anyway.

It **never** produces false negatives. If the AABB is truly visible, the p-vertex test will always report it as visible. This is the critical property for culling — we never skip something the player should see.

---

## Integration with the Renderer

The `Frustum` is **not** a component and **not** stored in the registry. It's a per-frame calculated value — local to the render function. This follows the ECS rule: systems have no state, components have no behaviour.

### Modified Render System

```cpp
// In src/engine/ecs/systems/render_system.h

#pragma once

#include <entt/entt.hpp>

class Camera;

struct CullStats {
    int total    = 0;
    int rendered = 0;
    int culled   = 0;
};

void renderSystem(entt::registry& registry, const Camera& camera,
                  CullStats& stats);
```

```cpp
// In src/engine/ecs/systems/render_system.cpp

#include "engine/ecs/systems/render_system.h"
#include "engine/ecs/components.h"
#include "engine/renderer/camera.h"
#include "engine/renderer/frustum.h"
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

void renderSystem(entt::registry& registry, const Camera& camera,
                  CullStats& stats) {

    // Reset stats for this frame
    stats.total    = 0;
    stats.rendered = 0;
    stats.culled   = 0;

    // Extract frustum from the camera's VP matrix (per-frame, local variable)
    glm::mat4 view       = camera.getViewMatrix();
    glm::mat4 projection = camera.getProjectionMatrix();
    glm::mat4 vp         = projection * view;

    Frustum frustum;
    frustum.update(vp);

    // Iterate all renderable entities
    auto renderView = registry.view<Position, MeshRenderer>();

    for (auto [entity, pos, mesh] : renderView.each()) {
        stats.total++;

        // ─── Frustum cull ────────────────────────────────────────
        // Use AABBCollider if the entity has one, otherwise skip culling
        if (registry.all_of<AABBCollider>(entity)) {
            const auto& col = registry.get<AABBCollider>(entity);

            glm::vec3 worldMin = pos.value - col.halfExtents;
            glm::vec3 worldMax = pos.value + col.halfExtents;

            if (!frustum.isAABBInside(worldMin, worldMax)) {
                stats.culled++;
                continue;  // Skip this entity — it's outside the frustum
            }
        }

        // ─── Entity is visible — render it ───────────────────────
        stats.rendered++;

        glm::mat4 model = glm::translate(glm::mat4(1.0f), pos.value);

        // Set uniforms and draw (simplified — your actual code may differ)
        glUseProgram(mesh.shaderId);
        // shader.setMat4("model", model);
        // shader.setMat4("view", view);
        // shader.setMat4("projection", projection);

        glBindVertexArray(mesh.vao);
        if (mesh.useIndices) {
            glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, 0);
        } else {
            glDrawArrays(GL_TRIANGLES, 0, mesh.vertexCount);
        }
    }
}
```

### Key Points

- The `Frustum` is a **local variable** inside the render function, created and discarded every frame. It's not a component, not a singleton, not a member variable.
- `CullStats` is passed by reference so the caller can display the results. It's a plain struct — no behaviour.
- Entities without an `AABBCollider` are always rendered (no bounding volume to test against). This is intentional — if you can't test it, draw it. The alternative (skipping it) would cause invisible entities.
- The `continue` statement is the actual culling — it skips the draw call entirely.

### Updated PlayingState

```cpp
// In PlayingState::render()

void PlayingState::render() {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Skybox first (Ch 24)
    m_skybox.render(m_skyboxShader, m_camera.getViewMatrix(),
                    m_camera.getProjectionMatrix());

    // Scene geometry with frustum culling
    CullStats cullStats;
    renderSystem(m_registry, m_camera, cullStats);

    // Store for debug display
    m_lastCullStats = cullStats;

    // Particles, HUD, console, etc.
    particleSystem(m_registry, m_camera);
    hudSystem(m_registry, m_window);
}
```

---

## Culling Statistics

Tracking how many entities are culled per frame tells you two things: (1) that culling is actually working, and (2) how much performance you're saving. We expose this through the developer console from Chapter 27.

### Console Command

```cpp
// In registerDebugCommands() — add alongside existing commands

console.registerCommand("show_cull_stats", "Toggle culling statistics display",
    [&debug, &console](const std::vector<std::string>& args) {
        debug.showCullStats = !debug.showCullStats;
        console.print(debug.showCullStats ? "Cull stats ON" : "Cull stats OFF");
    });
```

### Displaying the Stats

```cpp
// In DebugRenderer, add the flag:
bool showCullStats = false;

// In the HUD rendering code:
if (debugRenderer.showCullStats) {
    std::string statsText =
        "Total: "    + std::to_string(cullStats.total) +
        " Rendered: " + std::to_string(cullStats.rendered) +
        " Culled: "   + std::to_string(cullStats.culled) +
        " (" + std::to_string(
            cullStats.total > 0
            ? (cullStats.culled * 100 / cullStats.total)
            : 0) + "%)";

    // font.renderText(statsText, 10, 30, 1.0f, glm::vec4(0, 1, 1, 1));
}
```

```
Example output in the top-left corner:

  Total: 347  Rendered: 82  Culled: 265 (76%)
```

A 76% cull rate means the render system is skipping three out of every four entities. In a corridor-based level, you'll often see 80-90% cull rates because the player can only see what's directly ahead.

---

## Debug Visualisation

Rendering the frustum as a wireframe is invaluable for verifying it's correct. The idea: take the 8 corners of the NDC cube (`-1` to `+1` on all axes), transform them by the **inverse** VP matrix to get world-space positions, then draw 12 lines connecting them.

```
The 8 NDC corners map to the 8 frustum corners:

         Near face              Far face
     ntl -------- ntr       ftl -------- ftr
      |            |         |            |
      |            |         |            |
     nbl -------- nbr       fbl -------- fbr

  (n = near, f = far, t = top, b = bottom, l = left, r = right)
```

### Corner Calculation

```cpp
// In src/engine/renderer/frustum.h — add to the Frustum class:

struct FrustumCorners {
    glm::vec3 ntl, ntr, nbl, nbr;  // Near face corners
    glm::vec3 ftl, ftr, fbl, fbr;  // Far face corners
};

FrustumCorners getCorners(const glm::mat4& viewProjection) const;
```

```cpp
// In src/engine/renderer/frustum.cpp

static glm::vec3 unprojectNDC(const glm::vec4& ndc, const glm::mat4& invVP) {
    glm::vec4 world = invVP * ndc;
    return glm::vec3(world) / world.w;  // Perspective divide
}

FrustumCorners Frustum::getCorners(const glm::mat4& viewProjection) const {
    glm::mat4 invVP = glm::inverse(viewProjection);

    FrustumCorners c;

    // Near face (z = -1 in NDC)
    c.nbl = unprojectNDC(glm::vec4(-1, -1, -1, 1), invVP);
    c.nbr = unprojectNDC(glm::vec4( 1, -1, -1, 1), invVP);
    c.ntl = unprojectNDC(glm::vec4(-1,  1, -1, 1), invVP);
    c.ntr = unprojectNDC(glm::vec4( 1,  1, -1, 1), invVP);

    // Far face (z = 1 in NDC)
    c.fbl = unprojectNDC(glm::vec4(-1, -1,  1, 1), invVP);
    c.fbr = unprojectNDC(glm::vec4( 1, -1,  1, 1), invVP);
    c.ftl = unprojectNDC(glm::vec4(-1,  1,  1, 1), invVP);
    c.ftr = unprojectNDC(glm::vec4( 1,  1,  1, 1), invVP);

    return c;
}
```

### Drawing the Wireframe

```cpp
// In DebugRenderer — add a new toggle and drawing code:

// Add to DebugRenderer struct:
bool showFrustum = false;

// Register the console command:
console.registerCommand("show_frustum", "Toggle frustum wireframe",
    [&debug, &console](const std::vector<std::string>& args) {
        debug.showFrustum = !debug.showFrustum;
        console.print(debug.showFrustum ? "Frustum ON" : "Frustum OFF");
    });
```

```cpp
// In DebugRenderer::render(), add:

if (showFrustum) {
    glm::mat4 vp = camera.getProjectionMatrix() * camera.getViewMatrix();
    Frustum frustum;
    frustum.update(vp);
    FrustumCorners c = frustum.getCorners(vp);

    glm::vec3 cyan(0.0f, 1.0f, 1.0f);

    // Near face (4 edges)
    drawLine(c.ntl, c.ntr, cyan);
    drawLine(c.ntr, c.nbr, cyan);
    drawLine(c.nbr, c.nbl, cyan);
    drawLine(c.nbl, c.ntl, cyan);

    // Far face (4 edges)
    drawLine(c.ftl, c.ftr, cyan);
    drawLine(c.ftr, c.fbr, cyan);
    drawLine(c.fbr, c.fbl, cyan);
    drawLine(c.fbl, c.ftl, cyan);

    // Connecting edges (4 edges from near to far)
    drawLine(c.ntl, c.ftl, cyan);
    drawLine(c.ntr, c.ftr, cyan);
    drawLine(c.nbr, c.fbr, cyan);
    drawLine(c.nbl, c.fbl, cyan);
}
```

```
Debug frustum wireframe (as seen from a second "free" camera):

        ntl ─────────── ntr
       / |              / |
      /  |             /  |
    ftl ─────────── ftr   |
     |   |           |    |
     |  nbl ─────────|── nbr
     |  /            |  /
     | /             | /
    fbl ─────────── fbr

  12 cyan lines forming the truncated pyramid.
  The near face is small, the far face is large.
  The camera sits at the apex (behind the near face).
```

> **Note**: To see the frustum wireframe from outside, you need a second camera (a "debug" or "free" camera). If you draw the frustum from the same camera that defines it, you'll just see a rectangle filling the screen — because by definition, the frustum edges map to the screen edges.

---

## Sector-Level Culling

QEngine uses sectors to represent rooms and corridors (Chapter 8). Each `Sector` struct already has `boundsMin` and `boundsMax` — an axis-aligned bounding box. We can test sector bounds against the frustum before testing individual entities.

```cpp
// In the render function, before entity iteration:

for (const auto& sector : level.sectors) {
    // Test the sector's bounding box against the frustum
    if (!frustum.isAABBInside(sector.boundsMin, sector.boundsMax)) {
        continue;  // Entire sector is off-screen — skip all its geometry
    }

    // Render this sector's static mesh
    sector.mesh->render();

    // Entities in this sector still get per-entity culling
    // (a visible sector doesn't mean every entity in it is visible)
}
```

Sector culling is **coarser** but **faster** than per-entity culling. A single AABB test rejects an entire room with all its geometry and entities. Per-entity culling still runs on entities in visible sectors for finer-grained rejection.

The two levels of culling work together:

```
For each sector:
  └── Test sector AABB against frustum
       ├── OUTSIDE → skip entire sector (sector culled)
       └── INSIDE → render sector mesh, then:
            └── For each entity in sector:
                 └── Test entity AABB against frustum
                      ├── OUTSIDE → skip entity (entity culled)
                      └── INSIDE → render entity
```

This hierarchical approach is how production engines handle large worlds. The coarse pass eliminates large chunks cheaply, and the fine pass handles what's left.

---

## Complete File Listing

Here's what we built this chapter:

```
src/engine/renderer/frustum.h    — Plane struct, Frustum class, FrustumCorners
src/engine/renderer/frustum.cpp  — Plane extraction, point/sphere/AABB tests, corners
```

Plus modifications to:

```
src/engine/ecs/systems/render_system.h   — CullStats struct, updated signature
src/engine/ecs/systems/render_system.cpp — Frustum culling in the render loop
src/engine/debug/debug_renderer.h        — showCullStats, showFrustum flags
src/engine/debug/debug_renderer.cpp      — Frustum wireframe, cull stats display
```

### Complete frustum.h

```cpp
// In src/engine/renderer/frustum.h

#pragma once

#include <glm/glm.hpp>
#include <array>

struct Plane {
    glm::vec3 normal;
    float distance;

    float distanceTo(const glm::vec3& point) const {
        return glm::dot(normal, point) + distance;
    }
};

struct FrustumCorners {
    glm::vec3 ntl, ntr, nbl, nbr;  // Near face
    glm::vec3 ftl, ftr, fbl, fbr;  // Far face
};

class Frustum {
public:
    void update(const glm::mat4& viewProjection);

    bool isPointInside(const glm::vec3& point) const;
    bool isSphereInside(const glm::vec3& centre, float radius) const;
    bool isAABBInside(const glm::vec3& min, const glm::vec3& max) const;

    FrustumCorners getCorners(const glm::mat4& viewProjection) const;

private:
    std::array<Plane, 6> planes;
};
```

### Complete frustum.cpp

```cpp
// In src/engine/renderer/frustum.cpp

#include "engine/renderer/frustum.h"

void Frustum::update(const glm::mat4& vp) {
    auto extractPlane = [&](int row, float sign) -> Plane {
        Plane p;
        p.normal.x = vp[0][3] + sign * vp[0][row];
        p.normal.y = vp[1][3] + sign * vp[1][row];
        p.normal.z = vp[2][3] + sign * vp[2][row];
        p.distance  = vp[3][3] + sign * vp[3][row];
        return p;
    };

    planes[0] = extractPlane(0,  1.0f);  // Left
    planes[1] = extractPlane(0, -1.0f);  // Right
    planes[2] = extractPlane(1,  1.0f);  // Bottom
    planes[3] = extractPlane(1, -1.0f);  // Top
    planes[4] = extractPlane(2,  1.0f);  // Near
    planes[5] = extractPlane(2, -1.0f);  // Far

    for (auto& plane : planes) {
        float length = glm::length(plane.normal);
        plane.normal   /= length;
        plane.distance /= length;
    }
}

bool Frustum::isPointInside(const glm::vec3& point) const {
    for (const auto& plane : planes) {
        if (plane.distanceTo(point) < 0.0f) {
            return false;
        }
    }
    return true;
}

bool Frustum::isSphereInside(const glm::vec3& centre, float radius) const {
    for (const auto& plane : planes) {
        if (plane.distanceTo(centre) < -radius) {
            return false;
        }
    }
    return true;
}

bool Frustum::isAABBInside(const glm::vec3& min, const glm::vec3& max) const {
    for (const auto& plane : planes) {
        glm::vec3 pVertex;
        pVertex.x = (plane.normal.x >= 0.0f) ? max.x : min.x;
        pVertex.y = (plane.normal.y >= 0.0f) ? max.y : min.y;
        pVertex.z = (plane.normal.z >= 0.0f) ? max.z : min.z;

        if (plane.distanceTo(pVertex) < 0.0f) {
            return false;
        }
    }
    return true;
}

static glm::vec3 unprojectNDC(const glm::vec4& ndc, const glm::mat4& invVP) {
    glm::vec4 world = invVP * ndc;
    return glm::vec3(world) / world.w;
}

FrustumCorners Frustum::getCorners(const glm::mat4& viewProjection) const {
    glm::mat4 invVP = glm::inverse(viewProjection);

    FrustumCorners c;
    c.nbl = unprojectNDC(glm::vec4(-1, -1, -1, 1), invVP);
    c.nbr = unprojectNDC(glm::vec4( 1, -1, -1, 1), invVP);
    c.ntl = unprojectNDC(glm::vec4(-1,  1, -1, 1), invVP);
    c.ntr = unprojectNDC(glm::vec4( 1,  1, -1, 1), invVP);
    c.fbl = unprojectNDC(glm::vec4(-1, -1,  1, 1), invVP);
    c.fbr = unprojectNDC(glm::vec4( 1, -1,  1, 1), invVP);
    c.ftl = unprojectNDC(glm::vec4(-1,  1,  1, 1), invVP);
    c.ftr = unprojectNDC(glm::vec4( 1,  1,  1, 1), invVP);

    return c;
}
```

---

## C++ Concept: `std::array` vs C-Style Arrays

We used `std::array<Plane, 6> planes` for the frustum planes. Why not a C-style array `Plane planes[6]`?

### C-Style Array

```cpp
Plane planes[6];

// No idea how big it is (sizeof trick required):
int count = sizeof(planes) / sizeof(planes[0]);  // Works, but fragile

// Decays to pointer when passed to a function:
void process(Plane planes[6]);   // Actually: void process(Plane* planes)
                                  // The [6] is ignored by the compiler!

// No bounds checking:
planes[10] = {};  // Undefined behaviour — no error, no warning, just corruption
```

### `std::array`

```cpp
#include <array>

std::array<Plane, 6> planes;

// Knows its own size:
planes.size();      // 6 — always correct

// Does NOT decay to a pointer:
void process(const std::array<Plane, 6>& planes);  // Size is part of the type

// Bounds-checked access in debug builds:
planes.at(10);      // Throws std::out_of_range — catches bugs early

// Unchecked access (same speed as C array):
planes[0];          // Fast, no bounds check — use when you're sure the index is valid

// Works with STL algorithms:
std::sort(planes.begin(), planes.end(), compareFn);
auto it = std::find_if(planes.begin(), planes.end(), predicate);

// Range-based for loop (works for both, but std::array also gives .size()):
for (const auto& plane : planes) {
    // ...
}
```

### Performance

`std::array` has **zero runtime overhead** compared to a C array. The size `N` is a compile-time template parameter, not stored at runtime. The data layout is identical — a contiguous block of `N` elements on the stack. The compiler generates the same machine code for `planes[i]` whether `planes` is a `std::array` or a C array.

The difference is entirely at compile time: type safety, bounds checking in debug builds, and compatibility with the standard library. There is no reason to use C-style arrays in modern C++.

```cpp
// Prefer this:
std::array<Plane, 6> planes;

// Over this:
Plane planes[6];
```

---

## What's Next

In **Chapter 33**, we'll add **skeletal animation** — loading bone hierarchies, blending between animation clips, and driving mesh deformation on the GPU. This is what makes enemies walk, doors swing, and weapons reload with smooth, hand-crafted motion rather than the simple procedural animations we've used so far.
