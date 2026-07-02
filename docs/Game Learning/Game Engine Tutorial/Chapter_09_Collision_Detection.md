# Chapter 9: Collision Detection

## What You'll Learn
- AABB (Axis-Aligned Bounding Box) collision
- Ray casting — shooting a line through the world
- Swept AABB — detecting collisions during movement
- Spatial hashing — making collision checks efficient
- Integrating collision into the ECS

---

## Why Collision Detection?

Without collision detection:
- The player walks through walls
- Bullets pass through enemies
- Objects fall through floors

Collision detection answers two questions:
1. **Are these two things overlapping?** (static test)
2. **Will these two things overlap if one moves?** (dynamic/swept test)

---

## AABB — The Simplest Collision Shape

An Axis-Aligned Bounding Box is a rectangle (2D) or box (3D) whose edges are aligned with the X, Y, Z axes. It's defined by a min point and a max point:

```
         max (5, 4, 3)
         ╱───────────┐
        ╱            ╱│
       ╱            ╱ │
      ┌────────────┐  │
      │            │  │
      │            │  ╱
      │            │ ╱
      └────────────┘
  min (-1, 0, -2)
```

AABBs are fast to test but imprecise for non-box shapes. That's fine — Quake used AABBs for all entity collision.

### AABB Data Structure

```cpp
// src/engine/physics/aabb.h
#pragma once

#include <glm/glm.hpp>
#include <optional>

struct AABB {
    glm::vec3 min;
    glm::vec3 max;

    // Create from center and half-extents
    static AABB fromCenterSize(const glm::vec3& center, const glm::vec3& halfExtents) {
        return { center - halfExtents, center + halfExtents };
    }

    glm::vec3 center() const { return (min + max) * 0.5f; }
    glm::vec3 size() const { return max - min; }
    glm::vec3 halfExtents() const { return size() * 0.5f; }

    // Does this AABB contain a point?
    bool contains(const glm::vec3& point) const {
        return point.x >= min.x && point.x <= max.x &&
               point.y >= min.y && point.y <= max.y &&
               point.z >= min.z && point.z <= max.z;
    }

    // Do two AABBs overlap?
    bool intersects(const AABB& other) const {
        return (min.x <= other.max.x && max.x >= other.min.x) &&
               (min.y <= other.max.y && max.y >= other.min.y) &&
               (min.z <= other.max.z && max.z >= other.min.z);
    }

    // Expand this AABB to include a point
    void encapsulate(const glm::vec3& point) {
        min = glm::min(min, point);
        max = glm::max(max, point);
    }

    // Translate (move) the AABB
    AABB translated(const glm::vec3& offset) const {
        return { min + offset, max + offset };
    }
};
```

### AABB-AABB Overlap Test

The `intersects` check is the most common collision test in game engines. Two boxes overlap if and only if they overlap on **all three axes**:

```
Overlap on X?    min.x <= other.max.x AND max.x >= other.min.x
Overlap on Y?    min.y <= other.max.y AND max.y >= other.min.y
Overlap on Z?    min.z <= other.max.z AND max.z >= other.min.z
```

If any axis has no overlap, the boxes don't collide. This is called the **Separating Axis Theorem** (simplified for AABBs).

---

## Ray Casting

A ray is a point (origin) plus a direction. Ray casting asks: "starting from here, going in this direction, what do I hit first?"

Used for:
- **Hitscan weapons** (shotgun, railgun)
- **Line of sight** checks (can the enemy see the player?)
- **Mouse picking** (what did the player click on?)

### Ray Data Structure

`Ray` isn't an ECS component — you'd never attach it to an entity. It's a utility struct used as a parameter to raycasting functions, so it belongs with the physics types in `src/engine/physics/raycast.h` (shown in the next section).

`t` is a scalar — it says how far along the ray the point is. `t = 0` is the origin, `t = 1` is one unit along the direction.

### Ray-AABB Intersection

```cpp
// src/engine/physics/raycast.h
#pragma once

#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include "engine/physics/aabb.h"
#include <optional>

struct Ray {
    glm::vec3 origin;
    glm::vec3 direction;    // Should be normalised

    glm::vec3 pointAt(float t) const {
        return origin + direction * t;
    }
};

struct RayHit {
    float distance;         // How far along the ray
    glm::vec3 point;        // World-space hit point
    glm::vec3 normal;       // Surface normal at hit point
    entt::entity entity;    // What was hit (if applicable)
};

// Returns the distance along the ray where it enters the AABB,
// or std::nullopt if it misses.
std::optional<float> rayIntersectsAABB(const Ray& ray, const AABB& box);
```

```cpp
// src/engine/physics/raycast.cpp
#include "engine/physics/raycast.h"
#include <algorithm>
#include <cmath>

std::optional<float> rayIntersectsAABB(const Ray& ray, const AABB& box) {
    // Slab method: find the overlap of ray intervals on each axis

    float tmin = 0.0f;
    float tmax = std::numeric_limits<float>::max();

    for (int i = 0; i < 3; i++) {
        float origin = ray.origin[i];
        float dir = ray.direction[i];
        float bmin = box.min[i];
        float bmax = box.max[i];

        if (std::abs(dir) < 1e-8f) {
            // Ray is parallel to this axis
            if (origin < bmin || origin > bmax) {
                return std::nullopt;  // Ray misses entirely
            }
        } else {
            float t1 = (bmin - origin) / dir;
            float t2 = (bmax - origin) / dir;

            if (t1 > t2) std::swap(t1, t2);

            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);

            if (tmin > tmax) {
                return std::nullopt;  // No overlap
            }
        }
    }

    if (tmin < 0.0f) return std::nullopt;  // Hit is behind the ray

    return tmin;
}
```

### The Slab Method Explained

Think of each axis as defining two parallel planes (the min and max faces of the AABB). The ray enters one plane and exits the other. That gives an interval `[t1, t2]` on each axis:

```
X axis: ray enters at t=2, exits at t=8    → interval [2, 8]
Y axis: ray enters at t=3, exits at t=7    → interval [3, 7]
Z axis: ray enters at t=1, exits at t=10   → interval [1, 10]

Overlap of all intervals: [3, 7]
→ Ray is inside the box from t=3 to t=7
→ Hit point is at t=3 (entry point)
```

If any axis interval doesn't overlap with the others, the ray misses.

### C++ Concept: `std::optional`

```cpp
std::optional<float> result = rayIntersectsAABB(ray, box);
if (result.has_value()) {
    float distance = result.value();
    // or shorter: float distance = *result;
}
```

`std::optional<T>` is a container that either holds a value of type `T` or is empty. It's perfect for "this operation might not have a result" — much cleaner than returning -1 or using output parameters.

```cpp
std::optional<float> findRoot(float a, float b, float c);
// Returns the root if it exists, or std::nullopt if there's no real root
```

---

## Ray-Triangle Intersection

For more precise collision with level geometry (walls, floors), we need ray-triangle tests.

Add the declaration to `src/engine/physics/raycast.h`:

```cpp
std::optional<float> rayIntersectsTriangle(
    const Ray& ray,
    const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2);
```

Add the implementation to `src/engine/physics/raycast.cpp`:

```cpp
// Möller–Trumbore intersection algorithm
std::optional<float> rayIntersectsTriangle(
    const Ray& ray,
    const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2)
{
    const float EPSILON = 1e-7f;

    glm::vec3 edge1 = v1 - v0;
    glm::vec3 edge2 = v2 - v0;
    glm::vec3 h = glm::cross(ray.direction, edge2);
    float a = glm::dot(edge1, h);

    // Ray is parallel to triangle
    if (std::abs(a) < EPSILON) return std::nullopt;

    float f = 1.0f / a;
    glm::vec3 s = ray.origin - v0;
    float u = f * glm::dot(s, h);

    if (u < 0.0f || u > 1.0f) return std::nullopt;

    glm::vec3 q = glm::cross(s, edge1);
    float v = f * glm::dot(ray.direction, q);

    if (v < 0.0f || u + v > 1.0f) return std::nullopt;

    float t = f * glm::dot(edge2, q);

    if (t > EPSILON) {
        return t;
    }

    return std::nullopt;  // Behind the ray
}
```

The Möller-Trumbore algorithm is the standard approach. It computes whether the ray hits the triangle and at what distance, using cross products and dot products. The `u` and `v` values are barycentric coordinates — they tell you where on the triangle the hit is.

---

## Swept AABB — Collision During Movement

Static overlap tests aren't enough. An entity moving fast might pass through a thin wall in a single frame (the "tunneling" problem). Swept collision tests the entire path of movement.

```cpp
// src/engine/physics/collision.h
#pragma once

#include "engine/physics/aabb.h"

struct SweepResult {
    float time;             // 0.0 to 1.0 — how far along the movement
    glm::vec3 normal;       // Surface normal of what was hit
    bool hit;
};

// Test if a moving AABB hits a static AABB
SweepResult sweepAABB(const AABB& moving, const glm::vec3& velocity,
                       const AABB& stationary);
```

```cpp
// src/engine/physics/collision.cpp
#include "engine/physics/collision.h"
#include <algorithm>
#include <cmath>

SweepResult sweepAABB(const AABB& moving, const glm::vec3& velocity,
                       const AABB& stationary) {
    SweepResult result;
    result.time = 1.0f;
    result.normal = glm::vec3(0.0f);
    result.hit = false;

    // Minkowski difference: expand the stationary box by the moving box's size
    // Then do a ray cast from the moving box's center
    AABB expanded;
    expanded.min = stationary.min - moving.halfExtents();
    expanded.max = stationary.max + moving.halfExtents();

    glm::vec3 origin = moving.center();

    // Find entry and exit times for each axis
    float entryTime = 0.0f;
    float exitTime = 1.0f;
    glm::vec3 entryNormal(0.0f);

    for (int i = 0; i < 3; i++) {
        if (std::abs(velocity[i]) < 1e-8f) {
            // Not moving on this axis — must already be overlapping
            if (origin[i] < expanded.min[i] || origin[i] > expanded.max[i]) {
                return result;  // No collision possible
            }
        } else {
            float t1 = (expanded.min[i] - origin[i]) / velocity[i];
            float t2 = (expanded.max[i] - origin[i]) / velocity[i];

            glm::vec3 normal(0.0f);
            if (t1 > t2) {
                std::swap(t1, t2);
                normal[i] = 1.0f;
            } else {
                normal[i] = -1.0f;
            }

            if (t1 > entryTime) {
                entryTime = t1;
                entryNormal = normal;
            }
            exitTime = std::min(exitTime, t2);
        }
    }

    // Check for valid collision
    if (entryTime <= exitTime && entryTime >= 0.0f && entryTime < 1.0f) {
        result.time = entryTime;
        result.normal = entryNormal;
        result.hit = true;
    }

    return result;
}
```

### Minkowski Difference — The Trick

Instead of sweeping a box against another box (complex), we:
1. **Expand** the stationary box by the half-size of the moving box
2. **Shrink** the moving box to a single point (its center)
3. Now it's a **ray vs expanded box** test — which we already know how to do

```
Before:                     After Minkowski expansion:
 ┌──┐
 │  │ → moving              • → ray from center
 └──┘
      ┌──────┐              ┌──────────┐
      │      │ stationary   │ expanded │
      └──────┘              └──────────┘
```

This is one of the most elegant tricks in collision detection.

---

## Spatial Hashing — Making It Fast

Testing every entity against every other entity is O(n²). For 100 entities, that's 10,000 tests per frame. Spatial hashing reduces this by dividing the world into a grid:

```cpp
// src/engine/physics/spatial_hash.h
#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>

class SpatialHash {
public:
    SpatialHash(float cellSize = 4.0f);

    // Clear all entries (call at start of each frame)
    void clear();

    // Insert an entity at a position
    void insert(entt::entity entity, const glm::vec3& position,
                const glm::vec3& halfExtents);

    // Query: get all entities that might overlap with this AABB
    std::vector<entt::entity> query(const glm::vec3& position,
                                     const glm::vec3& halfExtents) const;

private:
    float m_cellSize;

    // Hash a 3D cell coordinate to a single integer
    struct CellHash {
        size_t operator()(const glm::ivec3& cell) const {
            // Simple hash combining all three coordinates
            size_t h = std::hash<int>()(cell.x);
            h ^= std::hash<int>()(cell.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<int>()(cell.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };

    struct CellEqual {
        bool operator()(const glm::ivec3& a, const glm::ivec3& b) const {
            return a.x == b.x && a.y == b.y && a.z == b.z;
        }
    };

    std::unordered_map<glm::ivec3, std::vector<entt::entity>,
                       CellHash, CellEqual> m_cells;

    glm::ivec3 toCell(const glm::vec3& position) const;
};
```

```cpp
// src/engine/physics/spatial_hash.cpp
#include "engine/physics/spatial_hash.h"
#include <cmath>

SpatialHash::SpatialHash(float cellSize) : m_cellSize(cellSize) {}

void SpatialHash::clear() {
    m_cells.clear();
}

glm::ivec3 SpatialHash::toCell(const glm::vec3& position) const {
    return glm::ivec3(
        static_cast<int>(std::floor(position.x / m_cellSize)),
        static_cast<int>(std::floor(position.y / m_cellSize)),
        static_cast<int>(std::floor(position.z / m_cellSize))
    );
}

void SpatialHash::insert(entt::entity entity, const glm::vec3& position,
                          const glm::vec3& halfExtents) {
    // An entity might span multiple cells — insert into all of them
    glm::ivec3 minCell = toCell(position - halfExtents);
    glm::ivec3 maxCell = toCell(position + halfExtents);

    for (int x = minCell.x; x <= maxCell.x; x++) {
        for (int y = minCell.y; y <= maxCell.y; y++) {
            for (int z = minCell.z; z <= maxCell.z; z++) {
                m_cells[glm::ivec3(x, y, z)].push_back(entity);
            }
        }
    }
}

std::vector<entt::entity> SpatialHash::query(
    const glm::vec3& position, const glm::vec3& halfExtents) const {

    std::vector<entt::entity> result;
    glm::ivec3 minCell = toCell(position - halfExtents);
    glm::ivec3 maxCell = toCell(position + halfExtents);

    for (int x = minCell.x; x <= maxCell.x; x++) {
        for (int y = minCell.y; y <= maxCell.y; y++) {
            for (int z = minCell.z; z <= maxCell.z; z++) {
                auto it = m_cells.find(glm::ivec3(x, y, z));
                if (it != m_cells.end()) {
                    for (auto entity : it->second) {
                        result.push_back(entity);
                    }
                }
            }
        }
    }

    // Note: result may contain duplicates if an entity spans multiple cells.
    // The caller should handle this (e.g. skip self, check unique).
    return result;
}
```

### How Spatial Hashing Works

The world is divided into a virtual grid. Each cell is `cellSize` units across:

```
┌────┬────┬────┬────┐
│    │    │    │    │
│    │ A  │    │    │
├────┼────┼────┼────┤
│    │    │ B  │    │
│  C │    │    │    │
├────┼────┼────┼────┤
│    │    │    │    │
│    │    │    │    │
└────┴────┴────┴────┘
```

To check what entity A might collide with:
1. Find which cell(s) A occupies
2. Look up those cells in the hash map
3. Only test A against entities in those cells

A and C are in different cells — no test needed. A and B are close — test needed.

**Cell size** should be roughly the size of your largest common entity. Too small → entities span many cells (expensive inserts). Too large → too many entities per cell (defeats the purpose).

---

## The Collision System

### src/engine/ecs/systems/collision_system.h

```cpp
#pragma once

#include <entt/entt.hpp>
#include "engine/physics/spatial_hash.h"
#include "engine/level/level.h"

void collisionSystem(entt::registry& registry, SpatialHash& spatialHash,
                      const Level& level, float dt);
```

### Updating Components

Add the collider component to `src/engine/ecs/components.h` if not already there:

```cpp
struct AABBCollider {
    glm::vec3 halfExtents = glm::vec3(0.5f);
    bool isTrigger = false;    // Triggers detect overlap but don't block
};
```

Typical sizes:
- Player: `halfExtents = (0.4, 0.9, 0.4)` — roughly human-sized
- Crate: `halfExtents = (0.5, 0.5, 0.5)` — 1m cube
- Bullet: no collider (use raycast instead for hitscan)

### src/engine/ecs/systems/collision_system.cpp

```cpp
#include "engine/ecs/systems/collision_system.h"
#include "engine/ecs/components.h"
#include "engine/physics/aabb.h"
#include "engine/physics/collision.h"

void collisionSystem(entt::registry& registry, SpatialHash& spatialHash,
                      const Level& level, float dt) {

    // 1. Rebuild the spatial hash each frame
    spatialHash.clear();

    auto collidables = registry.view<Position, AABBCollider>();
    for (auto [entity, pos, col] : collidables.each()) {
        spatialHash.insert(entity, pos.value, col.halfExtents);
    }

    // 2. For each entity with velocity, sweep against nearby entities and level
    auto movers = registry.view<Position, Velocity, AABBCollider>();

    for (auto [entity, pos, vel, col] : movers.each()) {
        glm::vec3 movement = vel.value * dt;

        if (glm::length(movement) < 1e-6f) continue;  // Not moving

        AABB entityBox = AABB::fromCenterSize(pos.value, col.halfExtents);

        // Check against level surfaces (simplified: treat each surface AABB)
        for (const auto& sector : level.sectors) {
            for (const auto& surface : sector.surfaces) {
                // Build an AABB for the surface (thin slab)
                glm::vec3 surfMin = glm::min(
                    glm::min(surface.vertices[0], surface.vertices[1]),
                    glm::min(surface.vertices[2], surface.vertices[3]));
                glm::vec3 surfMax = glm::max(
                    glm::max(surface.vertices[0], surface.vertices[1]),
                    glm::max(surface.vertices[2], surface.vertices[3]));

                // Fatten thin dimensions slightly
                for (int i = 0; i < 3; i++) {
                    if (surfMax[i] - surfMin[i] < 0.01f) {
                        surfMin[i] -= 0.01f;
                        surfMax[i] += 0.01f;
                    }
                }

                AABB surfaceBox = { surfMin, surfMax };

                SweepResult hit = sweepAABB(entityBox, movement, surfaceBox);
                if (hit.hit) {
                    // Slide along the surface: remove the velocity component
                    // that goes into the wall
                    float dot = glm::dot(vel.value, hit.normal);
                    if (dot < 0.0f) {
                        vel.value -= hit.normal * dot;
                    }
                }
            }
        }

        // Check against other entities
        auto nearby = spatialHash.query(pos.value, col.halfExtents + glm::vec3(2.0f));
        for (auto other : nearby) {
            if (other == entity) continue;
            if (!registry.all_of<Position, AABBCollider>(other)) continue;

            auto& otherPos = registry.get<Position>(other);
            auto& otherCol = registry.get<AABBCollider>(other);
            AABB otherBox = AABB::fromCenterSize(otherPos.value, otherCol.halfExtents);

            // If it's a trigger, don't resolve — just detect
            if (otherCol.isTrigger) {
                if (entityBox.intersects(otherBox)) {
                    // Trigger detected — Chapter 11 will handle this
                }
                continue;
            }

            SweepResult hit = sweepAABB(entityBox, movement, otherBox);
            if (hit.hit) {
                float dot = glm::dot(vel.value, hit.normal);
                if (dot < 0.0f) {
                    vel.value -= hit.normal * dot;
                }
            }
        }
    }
}
```

### Collision Response: Sliding

When you hit a wall, you don't stop dead — you **slide** along it. This is done by removing the component of velocity that points into the wall:

```
     velocity ──→
                  ╲
wall ═════════════════
                  ╱
     slide ──────→
```

```cpp
float dot = glm::dot(velocity, wallNormal);  // How much velocity goes into the wall
velocity -= wallNormal * dot;                  // Remove that component
```

This is the standard FPS wall-sliding behaviour. Quake does exactly this.

---

## The Updated Tick Order

```
1. InputSystem
2. AISystem
3. PhysicsSystem        ← Applies gravity to velocity (Chapter 10)
4. CollisionSystem      ← NEW: adjusts velocity to avoid penetration
5. MovementSystem       ← Applies (now-corrected) velocity to position
6. TriggerSystem
7. ...
8. RenderSystem
```

Collision runs **before** movement so that the velocity is corrected before being applied. The entity never actually enters the wall.

### Wiring It Up in main.cpp

Add the new includes at the top of `src/main.cpp`:

```cpp
#include "engine/ecs/systems/collision_system.h"
#include "engine/physics/spatial_hash.h"
```

Create a `SpatialHash` before the game loop (after the `Level level = ...` line):

```cpp
SpatialHash spatialHash(4.0f);  // 4-unit cells — roughly room-sized divisions
```

Then update the ECS tick section inside the game loop. The collision system must run **before** the movement system:

```cpp
// ─── ECS Systems (tick order!) ───────────────────────────
collisionSystem(registry, spatialHash, level, deltaTime);  // adjust velocities
movementSystem(registry, deltaTime);                        // apply velocities to positions
```

---

## Testing It

The collision system only affects entities with `Position`, `Velocity`, and `AABBCollider`. Right now nothing has those last two components, so there's nothing to test. Let's give one of the test cubes a velocity and a collider so it slides across the room and stops at the wall.

In `src/engine/ecs/scene_setup.cpp`, add `Velocity` and `AABBCollider` to the first test cube:

```cpp
// Keep the test cubes inside the room as visual references
auto cube = registry.create();
registry.emplace<Position>(cube, glm::vec3(-3.0f, 0.5f, -3.0f));
registry.emplace<Velocity>(cube, glm::vec3(2.0f, 0.0f, 0.0f));  // slides along +X
registry.emplace<AABBCollider>(cube, glm::vec3(0.5f), false);    // 1m cube collider
registry.emplace<MeshRenderer>(cube,
    cubeMesh->getVAO(), 0u,
    litShader->getId(), wallTexture->getId(),
    true, cubeMesh->getIndexCount()
);
```

Run the engine. You should see the cube slide to the right and stop when it hits the wall at x=5. Without the collision system it would pass straight through.

Once you've verified it works, you can remove the `Velocity` component from the cube (or set it to zero) to keep things tidy for Chapter 10.

---

## What's Next

In **Chapter 10**, we'll add physics — gravity, friction, jumping, and Quake-style air control. The collision system we just built will keep the player grounded while the physics system makes the world feel right.
