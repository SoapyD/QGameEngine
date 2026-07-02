# Chapter 37: Pathfinding (A* & Nav Mesh)

## What You'll Learn
- Why enemies need pathfinding and what happens without it
- Two approaches to navigation: grid-based and nav mesh
- The A* algorithm in detail with step-by-step walkthrough
- Building a NavGrid from level geometry
- Building a NavMesh from QEngine's sector data (Ch 8)
- A* on both grids and polygon meshes
- Path smoothing with the funnel (string-pulling) algorithm
- ECS integration: PathFollower component and pathFollowingSystem
- Updating Chapter 14's AI to use computed paths instead of direct movement
- Debug visualisation for navigation data and live paths

---

## Why Pathfinding

In Chapter 14 we gave enemies basic AI: chase the player when visible, patrol between fixed points otherwise. The chase behaviour moves directly toward the player's position. This works in open rooms, but the moment a wall or pillar sits between the enemy and the player, everything breaks:

```
    Player (P)                        Player (P)
       .                                 .
       .  <-- direct line                .
       .                                 .
    ########                          ########
    #      #                          #      #
    ########                          ########
       .                                 .
       . <-- enemy walks into wall       .
       .                                 .
    Enemy (E)                         Enemy (E)
                                         |
    WITHOUT PATHFINDING               The enemy needs to go AROUND
    Enemy walks into the wall         the wall to reach the player
    and gets stuck forever
```

Pathfinding solves this by computing a route from A to B that avoids obstacles. Instead of "walk toward the player", the enemy gets a list of waypoints: "walk to this corner, then around the wall, then toward the player." The movement code follows the waypoints in sequence.

---

## Two Approaches

There are two main ways to represent walkable space for pathfinding:

### Grid-Based Navigation

Divide the world into a uniform grid of cells. Each cell is either walkable or blocked. Pathfinding operates on grid coordinates.

```
    Grid-based navigation (top-down view)

    . . . . . . . . . . . .
    . . . . . . . . . . . .
    . . # # # # . . . . . .
    . . # # # # . . . . . .
    . . # # # # . . . . . .
    . . . . . . . . . . . .
    . . . . . . # # . . . .
    . . . . . . # # . . . .
    . . . . . . . . . . . .

    . = walkable cell    # = blocked cell
```

**Pros**: Simple to implement, easy to understand, good for 2D or simple 3D layouts.
**Cons**: Memory scales with world size (a 500x500 grid is 250,000 cells). Fixed resolution means paths hug grid lines. Diagonal movement requires extra handling.

### Nav Mesh Navigation

Cover the walkable floor with convex polygons. Pathfinding operates on the polygon graph -- each polygon is a node, and shared edges between adjacent polygons are connections.

```
    Nav mesh (top-down view)

    +-------+---+-------+
    |       |   |       |
    |  P0   |P1 |  P2   |
    |       |   |       |
    +---+---+   +---+---+
        |  WALL     |
    +---+---+   +---+---+
    |       |   |       |
    |  P3   |P4 |  P5   |
    |       |   |       |
    +-------+---+-------+

    6 convex polygons covering the walkable floor
    P0 connects to P1, P3
    P1 connects to P0, P2, P4
    P2 connects to P1, P5
    ...
```

**Pros**: Memory-efficient for large levels (a few hundred polygons vs millions of cells). Handles variable-width corridors naturally. Paths can be smoothed to look natural.
**Cons**: More complex to implement. Generating the mesh from arbitrary geometry is non-trivial.

We will implement both. The grid approach is simpler and teaches the core algorithm. The nav mesh approach is what you would use in a shipping game.

---

## The A* Algorithm

A* (A-star) is the workhorse of game pathfinding. It finds the shortest path between two nodes in a weighted graph. It is a best-first search that uses a heuristic to guide expansion toward the goal.

### Core Concepts

Every node in the search has three costs:

- **g-cost**: The actual cost of the path from the start to this node.
- **h-cost**: The heuristic estimate of the cost from this node to the goal.
- **f-cost**: `g + h`. The estimated total cost of the cheapest path through this node.

A* maintains two sets:

- **Open set**: Nodes discovered but not yet evaluated. Stored as a priority queue sorted by f-cost (lowest first).
- **Closed set**: Nodes already evaluated. We never revisit these.

### The Algorithm Step by Step

```
1. Add the start node to the open set with g=0, h=heuristic(start, goal)
2. While the open set is not empty:
   a. Pop the node with the lowest f-cost from the open set. Call it "current".
   b. If current is the goal, reconstruct the path and return it.
   c. Add current to the closed set.
   d. For each neighbour of current:
      i.   If neighbour is in the closed set, skip it.
      ii.  Calculate tentative_g = current.g + cost(current, neighbour).
      iii. If neighbour is not in the open set, add it.
      iv.  If tentative_g < neighbour.g, this is a better path:
           - Set neighbour.parent = current
           - Set neighbour.g = tentative_g
           - Set neighbour.f = tentative_g + heuristic(neighbour, goal)
3. If we exhaust the open set without finding the goal, no path exists.
```

### Heuristics

The heuristic must never overestimate the true cost (it must be **admissible**). Common choices:

- **Manhattan distance** (for 4-connected grids): `|dx| + |dz|`
- **Euclidean distance** (for 8-connected grids or nav meshes): `sqrt(dx*dx + dz*dz)`
- **Chebyshev distance** (for 8-connected grids with uniform cost): `max(|dx|, |dz|)`

### ASCII Walkthrough

Here is A* finding a path around an obstacle. Numbers show the order nodes are expanded. `S` = start, `G` = goal, `#` = wall, `.` = unexplored, numbers = expansion order:

```
    Step-by-step A* expansion (4-connected grid):

    Start state:              After expansion:

    . . . . . . . .          . . . . . . . .
    . . . . . . . .          . . . 9 8 7 . .
    . . # # # . . .          . .1# # # 6 . .
    . . # # # . . .          . .2# # # 5 . .
    . S # # # . G .          . S3# # #④ . .
    . . # # # . . .          . .④# # #③ . .
    . . . . . . . .          . .⑤⑥⑦⑧⑨ .  .
    . . . . . . . .          . . . . . .10 .

    S = start (0,4)                          Path found:
    G = goal  (6,4)                          S -> down -> down -> right -> right
                                             -> right -> right -> up -> up -> up -> G

    The algorithm explores around the bottom of the wall
    because the heuristic guides it toward the goal.
```

---

## NavGrid Implementation

The NavGrid divides the world's XZ plane into a uniform grid of cells. Each cell is marked walkable or blocked based on level geometry.

### src/engine/ai/nav_grid.h

```cpp
// In src/engine/ai/nav_grid.h
#pragma once

#include <vector>
#include <glm/glm.hpp>

class NavGrid {
public:
    NavGrid() = default;
    NavGrid(float worldWidth, float worldDepth, float cellSize);

    // Query a cell
    bool isWalkable(int x, int z) const;
    bool inBounds(int x, int z) const;

    // Mark cells
    void setBlocked(int x, int z);
    void setWalkable(int x, int z);

    // Coordinate conversion
    glm::ivec2 worldToGrid(const glm::vec3& worldPos) const;
    glm::vec3 gridToWorld(const glm::ivec2& gridPos) const;

    // Get walkable neighbours (8-connected)
    std::vector<glm::ivec2> getNeighbours(const glm::ivec2& pos) const;

    // Dimensions
    int getWidth() const { return m_width; }
    int getDepth() const { return m_depth; }
    float getCellSize() const { return m_cellSize; }
    const glm::vec3& getOrigin() const { return m_origin; }

private:
    std::vector<bool> m_cells;   // true = walkable
    int m_width = 0;
    int m_depth = 0;
    float m_cellSize = 1.0f;
    glm::vec3 m_origin{0.0f};   // World-space origin (min corner)

    int index(int x, int z) const { return z * m_width + x; }
};
```

### src/engine/ai/nav_grid.cpp

```cpp
// In src/engine/ai/nav_grid.cpp
#include "engine/ai/nav_grid.h"
#include <cmath>

NavGrid::NavGrid(float worldWidth, float worldDepth, float cellSize)
    : m_cellSize(cellSize)
{
    m_width = static_cast<int>(std::ceil(worldWidth / cellSize));
    m_depth = static_cast<int>(std::ceil(worldDepth / cellSize));

    // Origin at the negative corner of the world
    m_origin = glm::vec3(-worldWidth * 0.5f, 0.0f, -worldDepth * 0.5f);

    // All cells start walkable
    m_cells.resize(m_width * m_depth, true);
}

bool NavGrid::isWalkable(int x, int z) const {
    if (!inBounds(x, z)) return false;
    return m_cells[index(x, z)];
}

bool NavGrid::inBounds(int x, int z) const {
    return x >= 0 && x < m_width && z >= 0 && z < m_depth;
}

void NavGrid::setBlocked(int x, int z) {
    if (inBounds(x, z)) {
        m_cells[index(x, z)] = false;
    }
}

void NavGrid::setWalkable(int x, int z) {
    if (inBounds(x, z)) {
        m_cells[index(x, z)] = true;
    }
}

glm::ivec2 NavGrid::worldToGrid(const glm::vec3& worldPos) const {
    float relX = worldPos.x - m_origin.x;
    float relZ = worldPos.z - m_origin.z;
    int gx = static_cast<int>(std::floor(relX / m_cellSize));
    int gz = static_cast<int>(std::floor(relZ / m_cellSize));
    return glm::ivec2(gx, gz);
}

glm::vec3 NavGrid::gridToWorld(const glm::ivec2& gridPos) const {
    // Return the centre of the cell
    float wx = m_origin.x + (gridPos.x + 0.5f) * m_cellSize;
    float wz = m_origin.z + (gridPos.y + 0.5f) * m_cellSize;
    return glm::vec3(wx, 0.0f, wz);
}

std::vector<glm::ivec2> NavGrid::getNeighbours(const glm::ivec2& pos) const {
    std::vector<glm::ivec2> neighbours;
    neighbours.reserve(8);

    // 8-connected: cardinal + diagonal
    static const glm::ivec2 offsets[] = {
        { 0,  1}, { 0, -1}, { 1,  0}, {-1,  0},  // Cardinal
        { 1,  1}, { 1, -1}, {-1,  1}, {-1, -1}   // Diagonal
    };

    for (const auto& offset : offsets) {
        glm::ivec2 n = pos + offset;
        if (isWalkable(n.x, n.y)) {
            // For diagonal movement, ensure both adjacent cardinal cells
            // are walkable to prevent cutting through wall corners
            if (offset.x != 0 && offset.y != 0) {
                if (!isWalkable(pos.x + offset.x, pos.y) ||
                    !isWalkable(pos.x, pos.y + offset.y)) {
                    continue;  // Can't cut this corner
                }
            }
            neighbours.push_back(n);
        }
    }

    return neighbours;
}
```

### Building the Grid from Level Geometry

When a level loads, we build the NavGrid by checking which cells are blocked by solid geometry. Using the collision data from the spatial hash (Ch 9):

```cpp
// In src/engine/ai/nav_grid_builder.h
#pragma once

#include "engine/ai/nav_grid.h"
#include <entt/entt.hpp>

// Build a navigation grid from entities with AABBCollider components.
// Marks cells as blocked if they overlap any non-trigger collider.
NavGrid buildNavGrid(const entt::registry& registry,
                     float worldWidth, float worldDepth, float cellSize);
```

```cpp
// In src/engine/ai/nav_grid_builder.cpp
#include "engine/ai/nav_grid_builder.h"
#include "engine/ecs/components.h"

NavGrid buildNavGrid(const entt::registry& registry,
                     float worldWidth, float worldDepth, float cellSize) {
    NavGrid grid(worldWidth, worldDepth, cellSize);

    // Iterate all static collidable entities
    auto view = registry.view<const Position, const AABBCollider>();
    for (auto [entity, pos, col] : view.each()) {
        // Skip triggers — they don't block movement
        if (registry.all_of<TriggerVolume>(entity)) continue;

        // Skip entities with velocity — they are dynamic (enemies, projectiles)
        if (registry.all_of<Velocity>(entity)) continue;

        // Calculate the AABB in world space
        glm::vec3 minCorner = pos.value - col.halfExtents;
        glm::vec3 maxCorner = pos.value + col.halfExtents;

        // Convert to grid coordinates
        glm::ivec2 gridMin = grid.worldToGrid(minCorner);
        glm::ivec2 gridMax = grid.worldToGrid(maxCorner);

        // Mark all overlapping cells as blocked
        for (int z = gridMin.y; z <= gridMax.y; z++) {
            for (int x = gridMin.x; x <= gridMax.x; x++) {
                grid.setBlocked(x, z);
            }
        }
    }

    return grid;
}
```

---

## A* on the Grid

Now the core pathfinding algorithm, operating on the NavGrid.

### src/engine/ai/pathfinder.h

```cpp
// In src/engine/ai/pathfinder.h
#pragma once

#include "engine/ai/nav_grid.h"
#include <vector>
#include <glm/glm.hpp>

struct PathNode {
    glm::ivec2 position;
    float gCost = FLT_MAX;     // Cost from start
    float hCost = 0.0f;        // Heuristic to goal
    float fCost() const { return gCost + hCost; }
    PathNode* parent = nullptr;
};

// Find a path on the grid. Returns world-space waypoints.
// Returns an empty vector if no path exists.
std::vector<glm::vec3> findGridPath(const glm::vec3& start,
                                     const glm::vec3& goal,
                                     const NavGrid& grid);
```

### src/engine/ai/pathfinder.cpp

```cpp
// In src/engine/ai/pathfinder.cpp
#include "engine/ai/pathfinder.h"
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <cmath>
#include <algorithm>
#include <functional>

// Hash for glm::ivec2 so we can use it in unordered containers
struct IVec2Hash {
    std::size_t operator()(const glm::ivec2& v) const {
        std::size_t h1 = std::hash<int>{}(v.x);
        std::size_t h2 = std::hash<int>{}(v.y);
        return h1 ^ (h2 << 16);
    }
};

static float heuristic(const glm::ivec2& a, const glm::ivec2& b) {
    // Euclidean distance — admissible for 8-connected grid
    float dx = static_cast<float>(a.x - b.x);
    float dy = static_cast<float>(a.y - b.y);
    return std::sqrt(dx * dx + dy * dy);
}

static float moveCost(const glm::ivec2& from, const glm::ivec2& to) {
    // Diagonal moves cost sqrt(2), cardinal moves cost 1
    int dx = std::abs(from.x - to.x);
    int dy = std::abs(from.y - to.y);
    if (dx + dy == 2) return 1.41421356f;  // sqrt(2)
    return 1.0f;
}

std::vector<glm::vec3> findGridPath(const glm::vec3& start,
                                     const glm::vec3& goal,
                                     const NavGrid& grid) {
    glm::ivec2 startGrid = grid.worldToGrid(start);
    glm::ivec2 goalGrid  = grid.worldToGrid(goal);

    // If start or goal is not walkable, fail immediately
    if (!grid.isWalkable(startGrid.x, startGrid.y) ||
        !grid.isWalkable(goalGrid.x, goalGrid.y)) {
        return {};
    }

    // If start == goal, return a single-point path
    if (startGrid == goalGrid) {
        return { goal };
    }

    // Node storage — keyed by grid position
    std::unordered_map<glm::ivec2, PathNode, IVec2Hash> nodes;
    std::unordered_set<glm::ivec2, IVec2Hash> closedSet;

    // Comparator: lowest f-cost first (min-heap)
    auto cmp = [&nodes](const glm::ivec2& a, const glm::ivec2& b) {
        return nodes[a].fCost() > nodes[b].fCost();
    };
    std::priority_queue<glm::ivec2, std::vector<glm::ivec2>, decltype(cmp)>
        openQueue(cmp);

    // Initialise start node
    PathNode& startNode = nodes[startGrid];
    startNode.position = startGrid;
    startNode.gCost = 0.0f;
    startNode.hCost = heuristic(startGrid, goalGrid);
    startNode.parent = nullptr;

    openQueue.push(startGrid);

    // Safety limit to prevent infinite loops on huge grids
    constexpr int MAX_ITERATIONS = 10000;
    int iterations = 0;

    while (!openQueue.empty() && iterations < MAX_ITERATIONS) {
        iterations++;

        glm::ivec2 currentPos = openQueue.top();
        openQueue.pop();

        // Skip if already evaluated (duplicates in the queue)
        if (closedSet.count(currentPos)) continue;
        closedSet.insert(currentPos);

        // Goal reached — reconstruct path
        if (currentPos == goalGrid) {
            std::vector<glm::vec3> path;
            PathNode* node = &nodes[currentPos];

            while (node != nullptr) {
                path.push_back(grid.gridToWorld(node->position));
                // Follow parent chain
                if (node->parent) {
                    node = node->parent;
                } else {
                    break;
                }
            }

            std::reverse(path.begin(), path.end());

            // Replace the last waypoint with the exact goal position
            // so the entity doesn't stop at the cell centre
            if (!path.empty()) {
                path.back() = goal;
            }

            return path;
        }

        // Expand neighbours
        PathNode& current = nodes[currentPos];
        auto neighbours = grid.getNeighbours(currentPos);

        for (const glm::ivec2& neighbourPos : neighbours) {
            if (closedSet.count(neighbourPos)) continue;

            float tentativeG = current.gCost + moveCost(currentPos, neighbourPos);

            PathNode& neighbour = nodes[neighbourPos];

            if (tentativeG < neighbour.gCost) {
                neighbour.position = neighbourPos;
                neighbour.gCost = tentativeG;
                neighbour.hCost = heuristic(neighbourPos, goalGrid);
                neighbour.parent = &nodes[currentPos];

                openQueue.push(neighbourPos);
            }
        }
    }

    // No path found
    return {};
}
```

**Important note on the parent pointer**: We store `PathNode` objects in an `unordered_map`. If the map rehashes (because it grows), all pointers are invalidated. This implementation is safe because we only read parent pointers during path reconstruction, which happens after the search loop completes and no further insertions occur. An alternative is to store indices instead of pointers, which we will do in the nav mesh version.

---

## Nav Mesh

For larger and more complex levels, a navigation mesh is far more efficient. Instead of thousands of grid cells, we use a handful of convex polygons that cover the walkable floor.

### What a Nav Mesh Looks Like

```
    Level layout (top-down):

    ################################
    #          #                   #
    #  Room A  #      Room C       #
    #          #                   #
    ###  #######         ##########
      #  #               #
      #  #   Corridor    #
      #  #               #
    ###  #######         ##########
    #          #                   #
    #  Room B  #      Room D       #
    #          #                   #
    ################################

    Nav mesh overlay:

    ################################
    #  +------+#+-----------------+#
    #  | P0   |#|      P2        |#
    #  |      |#|                |#
    ###+ +#####+|       +#########+
      #| |     ||       |#
      #|P|P1   ||       |#
      #| |     ||       |#
    ###+ +#####+|       +#########+
    #  |      |#|                |#
    #  | P3   |#|      P4        |#
    #  +------+#+-----------------+#
    ################################

    5 polygons: P0, P1, P2, P3, P4
    Adjacency: P0-P1, P1-P3, P0-P2 (via doorway), P1-P2, P1-P4, P3-P4
```

### src/engine/ai/nav_mesh.h

```cpp
// In src/engine/ai/nav_mesh.h
#pragma once

#include <vector>
#include <glm/glm.hpp>

struct NavPoly {
    std::vector<glm::vec3> vertices;    // Convex polygon vertices (in order)
    std::vector<int> neighbours;        // Indices of adjacent polygons
    glm::vec3 centroid{0.0f};           // Pre-computed centre point

    // Shared edges with neighbours: for each neighbour, the two endpoints
    // of the portal (shared edge) between this polygon and that neighbour
    struct Portal {
        int neighbourIndex;             // Index into NavMesh::polygons
        glm::vec3 left;                 // Left edge of shared boundary
        glm::vec3 right;               // Right edge of shared boundary
    };
    std::vector<Portal> portals;
};

class NavMesh {
public:
    std::vector<NavPoly> polygons;

    // Find which polygon contains a world-space point (XZ plane test)
    int findPolyContaining(const glm::vec3& point) const;

    // Find a path from start to goal. Returns world-space waypoints.
    std::vector<glm::vec3> findPath(const glm::vec3& start,
                                     const glm::vec3& goal) const;

    // Compute centroids for all polygons (call after building)
    void computeCentroids();

private:
    // A* on the polygon graph
    std::vector<int> findPolygonPath(int startPoly, int goalPoly) const;

    // Point-in-convex-polygon test (XZ plane)
    bool pointInPoly(const glm::vec3& point, const NavPoly& poly) const;
};
```

### src/engine/ai/nav_mesh.cpp

```cpp
// In src/engine/ai/nav_mesh.cpp
#include "engine/ai/nav_mesh.h"
#include <queue>
#include <unordered_set>
#include <algorithm>
#include <cmath>
#include <cfloat>

void NavMesh::computeCentroids() {
    for (auto& poly : polygons) {
        glm::vec3 sum{0.0f};
        for (const auto& v : poly.vertices) {
            sum += v;
        }
        poly.centroid = sum / static_cast<float>(poly.vertices.size());
    }
}

bool NavMesh::pointInPoly(const glm::vec3& point, const NavPoly& poly) const {
    // 2D point-in-convex-polygon test on the XZ plane.
    // For a convex polygon, the point is inside if it is on the same
    // side of every edge.
    int n = static_cast<int>(poly.vertices.size());
    if (n < 3) return false;

    bool allPositive = true;
    bool allNegative = true;

    for (int i = 0; i < n; i++) {
        const glm::vec3& a = poly.vertices[i];
        const glm::vec3& b = poly.vertices[(i + 1) % n];

        // 2D cross product on XZ plane
        float cross = (b.x - a.x) * (point.z - a.z)
                    - (b.z - a.z) * (point.x - a.x);

        if (cross < 0.0f) allPositive = false;
        if (cross > 0.0f) allNegative = false;
    }

    return allPositive || allNegative;
}

int NavMesh::findPolyContaining(const glm::vec3& point) const {
    for (int i = 0; i < static_cast<int>(polygons.size()); i++) {
        if (pointInPoly(point, polygons[i])) {
            return i;
        }
    }
    return -1;  // Point not on any polygon
}

std::vector<int> NavMesh::findPolygonPath(int startPoly, int goalPoly) const {
    if (startPoly == goalPoly) return { startPoly };

    int numPolys = static_cast<int>(polygons.size());

    // g-cost and parent arrays
    std::vector<float> gCost(numPolys, FLT_MAX);
    std::vector<int> parent(numPolys, -1);
    std::unordered_set<int> closedSet;

    // Min-heap by f-cost
    struct OpenEntry {
        int polyIndex;
        float fCost;
    };
    auto cmp = [](const OpenEntry& a, const OpenEntry& b) {
        return a.fCost > b.fCost;
    };
    std::priority_queue<OpenEntry, std::vector<OpenEntry>, decltype(cmp)>
        openQueue(cmp);

    gCost[startPoly] = 0.0f;
    float h = glm::distance(polygons[startPoly].centroid,
                             polygons[goalPoly].centroid);
    openQueue.push({ startPoly, h });

    while (!openQueue.empty()) {
        OpenEntry current = openQueue.top();
        openQueue.pop();

        int ci = current.polyIndex;
        if (closedSet.count(ci)) continue;
        closedSet.insert(ci);

        if (ci == goalPoly) {
            // Reconstruct polygon path
            std::vector<int> path;
            int node = goalPoly;
            while (node != -1) {
                path.push_back(node);
                node = parent[node];
            }
            std::reverse(path.begin(), path.end());
            return path;
        }

        for (int ni : polygons[ci].neighbours) {
            if (closedSet.count(ni)) continue;

            float edgeCost = glm::distance(polygons[ci].centroid,
                                            polygons[ni].centroid);
            float tentativeG = gCost[ci] + edgeCost;

            if (tentativeG < gCost[ni]) {
                gCost[ni] = tentativeG;
                parent[ni] = ci;
                float hCost = glm::distance(polygons[ni].centroid,
                                             polygons[goalPoly].centroid);
                openQueue.push({ ni, tentativeG + hCost });
            }
        }
    }

    return {};  // No path
}

std::vector<glm::vec3> NavMesh::findPath(const glm::vec3& start,
                                          const glm::vec3& goal) const {
    int startPoly = findPolyContaining(start);
    int goalPoly  = findPolyContaining(goal);

    if (startPoly < 0 || goalPoly < 0) return {};

    // Find the corridor of polygons
    std::vector<int> polyPath = findPolygonPath(startPoly, goalPoly);
    if (polyPath.empty()) return {};

    // If start and goal are in the same polygon, go directly
    if (polyPath.size() == 1) {
        return { start, goal };
    }

    // Build centroid path (will be smoothed by the funnel algorithm)
    std::vector<glm::vec3> path;
    path.push_back(start);
    for (size_t i = 1; i < polyPath.size() - 1; i++) {
        path.push_back(polygons[polyPath[i]].centroid);
    }
    path.push_back(goal);

    return path;
}
```

---

## Simple Nav Mesh Generation from Sectors

QEngine's levels are built from sectors (Chapter 8). Each sector already defines a floor polygon. We can use the sector data directly as a starting point for the nav mesh:

```cpp
// In src/engine/ai/nav_mesh_builder.h
#pragma once

#include "engine/ai/nav_mesh.h"
#include "engine/level/level_data.h"  // Contains Sector definition from Ch 8

// Build a nav mesh from level sector data.
// Each sector floor becomes a NavPoly.
// Sectors sharing an edge become nav mesh neighbours.
NavMesh buildNavMeshFromSectors(const std::vector<Sector>& sectors);
```

```cpp
// In src/engine/ai/nav_mesh_builder.cpp
#include "engine/ai/nav_mesh_builder.h"
#include <cmath>

// Check if two edges are shared (same endpoints within tolerance)
static bool edgesMatch(const glm::vec3& a1, const glm::vec3& a2,
                       const glm::vec3& b1, const glm::vec3& b2,
                       float tolerance = 0.01f) {
    // Edges can match in either direction
    return (glm::distance(a1, b1) < tolerance && glm::distance(a2, b2) < tolerance) ||
           (glm::distance(a1, b2) < tolerance && glm::distance(a2, b1) < tolerance);
}

NavMesh buildNavMeshFromSectors(const std::vector<Sector>& sectors) {
    NavMesh mesh;
    mesh.polygons.resize(sectors.size());

    // Step 1: Convert each sector floor to a NavPoly
    for (size_t i = 0; i < sectors.size(); i++) {
        NavPoly& poly = mesh.polygons[i];

        // Copy the sector's floor vertices
        // Sectors store their floor polygon as a list of 2D points (XZ)
        // with a floor height. Convert to 3D.
        for (const auto& vertex2D : sectors[i].floorVertices) {
            poly.vertices.push_back(
                glm::vec3(vertex2D.x, sectors[i].floorHeight, vertex2D.y));
        }
    }

    // Step 2: Find adjacent sectors by checking for shared edges
    for (size_t i = 0; i < sectors.size(); i++) {
        const auto& vertsA = mesh.polygons[i].vertices;
        int nA = static_cast<int>(vertsA.size());

        for (size_t j = i + 1; j < sectors.size(); j++) {
            const auto& vertsB = mesh.polygons[j].vertices;
            int nB = static_cast<int>(vertsB.size());

            // Check every edge pair
            for (int ea = 0; ea < nA; ea++) {
                const glm::vec3& a1 = vertsA[ea];
                const glm::vec3& a2 = vertsA[(ea + 1) % nA];

                for (int eb = 0; eb < nB; eb++) {
                    const glm::vec3& b1 = vertsB[eb];
                    const glm::vec3& b2 = vertsB[(eb + 1) % nB];

                    if (edgesMatch(a1, a2, b1, b2)) {
                        // Found a shared edge — these sectors are neighbours
                        mesh.polygons[i].neighbours.push_back(
                            static_cast<int>(j));
                        mesh.polygons[j].neighbours.push_back(
                            static_cast<int>(i));

                        // Store the portal (shared edge)
                        mesh.polygons[i].portals.push_back(
                            { static_cast<int>(j), a1, a2 });
                        mesh.polygons[j].portals.push_back(
                            { static_cast<int>(i), b1, b2 });

                        goto nextSectorPair;  // One shared edge is enough
                    }
                }
            }
            nextSectorPair:;
        }
    }

    mesh.computeCentroids();
    return mesh;
}
```

This gives us a basic nav mesh for free from the existing level data. Each room becomes a polygon, and doorways between rooms become the portal edges that connect them.

For more complex scenarios (large open areas that need subdivision, dynamic obstacles, multi-level floors), professional tools like **Recast/Detour** generate nav meshes from arbitrary 3D geometry. Integrating Recast is beyond the scope of this chapter, but knowing it exists is useful. Many shipped games use it.

---

## Path Smoothing: The Funnel Algorithm

Raw A* paths through a nav mesh go through polygon centroids. This produces an unnatural zigzag:

```
    Centroid path (zigzag):         Smoothed path (funnel):

    +-------+-------+               +-------+-------+
    |       |       |               |       |       |
    |   S---+-->C1  |               |   S   |       |
    |       |   |   |               |    \  |       |
    +-------+---+---+               +-----\-+-------+
    |       |   |   |               |      \|       |
    |   C2<-+---+   |               |       +       |
    |   |   |       |               |       |       |
    +---+---+-------+               +-------+-------+
    |   |   |       |               |       |       |
    |   +---+-->G   |               |       +-->G   |
    |       |       |               |       |       |
    +-------+-------+               +-------+-------+

    Path goes: S -> C1 -> C2 -> G   Path goes: S -> portal edge -> G
    Wasteful zigzag                  Tight, natural-looking path
```

The **funnel algorithm** (also called **string-pulling**) takes the corridor of polygons and the portal edges between them, then finds the shortest path that stays within the corridor.

### How the Funnel Algorithm Works

Think of it as holding a string at the start point and pulling it taut through the corridor. The string catches on portal edges where the corridor turns.

```
    Funnel algorithm step by step:

    1. Start with the "funnel" apex at S
       Left and right boundaries point to the first portal's endpoints

            apex = S
           /         \
          L           R      <-- funnel boundaries
          |           |
          +---portal--+      <-- first portal (shared edge)

    2. Process each portal in sequence:
       - Narrow the funnel by moving L or R inward
       - If the funnel collapses (L crosses R), the apex moved:
         add the previous boundary point to the path,
         restart the funnel from there

    3. After all portals, connect the last apex to the goal
```

### src/engine/ai/funnel.h

```cpp
// In src/engine/ai/funnel.h
#pragma once

#include "engine/ai/nav_mesh.h"
#include <vector>
#include <glm/glm.hpp>

// Given a corridor of polygons (from A* on the nav mesh), smooth the
// path using the funnel algorithm. Returns world-space waypoints.
std::vector<glm::vec3> funnelSmooth(
    const glm::vec3& start,
    const glm::vec3& goal,
    const std::vector<int>& polyPath,  // Polygon indices from A*
    const NavMesh& mesh);
```

### src/engine/ai/funnel.cpp

```cpp
// In src/engine/ai/funnel.cpp
#include "engine/ai/funnel.h"
#include <cmath>

// 2D cross product on XZ plane (used to determine left/right of a line)
static float cross2D(const glm::vec3& origin,
                     const glm::vec3& a,
                     const glm::vec3& b) {
    return (a.x - origin.x) * (b.z - origin.z)
         - (a.z - origin.z) * (b.x - origin.x);
}

// Find the portal between two adjacent polygons
static bool findPortal(const NavMesh& mesh, int fromPoly, int toPoly,
                       glm::vec3& outLeft, glm::vec3& outRight) {
    const NavPoly& poly = mesh.polygons[fromPoly];
    for (const auto& portal : poly.portals) {
        if (portal.neighbourIndex == toPoly) {
            outLeft  = portal.left;
            outRight = portal.right;
            return true;
        }
    }
    return false;
}

std::vector<glm::vec3> funnelSmooth(
    const glm::vec3& start,
    const glm::vec3& goal,
    const std::vector<int>& polyPath,
    const NavMesh& mesh)
{
    if (polyPath.size() <= 1) {
        return { start, goal };
    }

    // Build the portal list from the polygon corridor
    struct Portal {
        glm::vec3 left, right;
    };
    std::vector<Portal> portals;

    for (size_t i = 0; i + 1 < polyPath.size(); i++) {
        glm::vec3 left, right;
        if (findPortal(mesh, polyPath[i], polyPath[i + 1], left, right)) {
            portals.push_back({ left, right });
        }
    }

    // Add the goal as the final degenerate portal (both sides = goal)
    portals.push_back({ goal, goal });

    // Funnel algorithm
    std::vector<glm::vec3> path;
    path.push_back(start);

    glm::vec3 apex  = start;
    glm::vec3 left  = start;
    glm::vec3 right = start;
    int apexIndex  = 0;
    int leftIndex  = 0;
    int rightIndex = 0;

    for (int i = 0; i < static_cast<int>(portals.size()); i++) {
        const glm::vec3& newLeft  = portals[i].left;
        const glm::vec3& newRight = portals[i].right;

        // Try to narrow the funnel from the right side
        if (cross2D(apex, right, newRight) <= 0.0f) {
            if (apex == right || cross2D(apex, left, newRight) > 0.0f) {
                // Tighten the right side
                right = newRight;
                rightIndex = i;
            } else {
                // Right side crossed over left — left becomes new apex
                path.push_back(left);
                apex = left;
                apexIndex = leftIndex;

                // Reset funnel
                left  = apex;
                right = apex;
                leftIndex  = apexIndex;
                rightIndex = apexIndex;

                // Restart scanning from the apex portal
                i = apexIndex;
                continue;
            }
        }

        // Try to narrow the funnel from the left side
        if (cross2D(apex, left, newLeft) >= 0.0f) {
            if (apex == left || cross2D(apex, right, newLeft) < 0.0f) {
                // Tighten the left side
                left = newLeft;
                leftIndex = i;
            } else {
                // Left side crossed over right — right becomes new apex
                path.push_back(right);
                apex = right;
                apexIndex = rightIndex;

                // Reset funnel
                left  = apex;
                right = apex;
                leftIndex  = apexIndex;
                rightIndex = apexIndex;

                i = apexIndex;
                continue;
            }
        }
    }

    // Add the goal
    if (path.empty() || path.back() != goal) {
        path.push_back(goal);
    }

    return path;
}
```

To use the funnel algorithm, update `NavMesh::findPath` to call it:

```cpp
// In src/engine/ai/nav_mesh.cpp — updated findPath

std::vector<glm::vec3> NavMesh::findPath(const glm::vec3& start,
                                          const glm::vec3& goal) const {
    int startPoly = findPolyContaining(start);
    int goalPoly  = findPolyContaining(goal);

    if (startPoly < 0 || goalPoly < 0) return {};

    std::vector<int> polyPath = findPolygonPath(startPoly, goalPoly);
    if (polyPath.empty()) return {};

    if (polyPath.size() == 1) {
        return { start, goal };
    }

    // Use funnel algorithm for smooth paths
    return funnelSmooth(start, goal, polyPath, *this);
}
```

---

## PathFollower Component

The PathFollower is pure data -- a list of waypoints and tracking state. No behaviour.

```cpp
// In src/engine/ecs/components.h (add alongside existing components)

struct PathFollower {
    std::vector<glm::vec3> waypoints;  // World-space path from pathfinder
    int currentWaypoint = 0;           // Index of the waypoint we're heading toward
    float waypointRadius = 0.5f;       // Distance threshold to advance to next
    bool hasPath = false;              // Whether we currently have a valid path

    float repathTimer = 0.0f;          // Countdown to next path recalculation
    float repathInterval = 1.0f;       // Seconds between repath attempts

    glm::vec3 targetPosition{0.0f};    // Final destination (for repathing)
};
```

How this integrates with the existing movement components:

```
    ┌──────────────┐     ┌──────────────┐     ┌──────────────┐
    │  AIBrain     │────>│ PathFollower  │────>│  Velocity    │
    │              │     │              │     │              │
    │ Sets target  │     │ Holds path   │     │ Set by path  │
    │ position     │     │ and waypoints│     │ system       │
    └──────────────┘     └──────────────┘     └──────────────┘
                                                    │
                                                    ▼
                                              ┌──────────────┐
                                              │  Position    │
                                              │              │
                                              │ Updated by   │
                                              │ physics      │
                                              └──────────────┘
```

The AI brain decides **where** to go. The PathFollower stores the computed **route**. The path following system sets the **velocity** to steer along the route. The physics system moves the entity.

---

## pathFollowingSystem

A free function with no state, as required by the ECS architecture:

```cpp
// In src/engine/ecs/systems/path_following_system.h
#pragma once

#include <entt/entt.hpp>

// Advance entities along their computed paths.
// Sets Velocity toward the current waypoint. Advances to the next
// waypoint when the entity is within waypointRadius.
void pathFollowingSystem(entt::registry& registry, float dt);
```

```cpp
// In src/engine/ecs/systems/path_following_system.cpp
#include "engine/ecs/systems/path_following_system.h"
#include "engine/ecs/components.h"
#include "engine/ai/nav_mesh.h"
#include "engine/ai/pathfinder.h"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

void pathFollowingSystem(entt::registry& registry, float dt) {
    auto view = registry.view<Position, Velocity, PathFollower>();

    for (auto [entity, pos, vel, follower] : view.each()) {
        // ─── Repath timer ────────────────────────────────────────
        follower.repathTimer -= dt;

        if (follower.repathTimer <= 0.0f) {
            follower.repathTimer = follower.repathInterval;

            // Request a new path to the target position.
            // The AI system sets targetPosition; here we recompute the route.
            // Choose grid or nav mesh based on what is available:

            auto* navMesh = registry.ctx().find<NavMesh>();
            if (navMesh) {
                auto newPath = navMesh->findPath(pos.value,
                                                  follower.targetPosition);
                if (!newPath.empty()) {
                    follower.waypoints = std::move(newPath);
                    follower.currentWaypoint = 0;
                    follower.hasPath = true;
                }
            } else {
                auto* navGrid = registry.ctx().find<NavGrid>();
                if (navGrid) {
                    auto newPath = findGridPath(pos.value,
                                                 follower.targetPosition,
                                                 *navGrid);
                    if (!newPath.empty()) {
                        follower.waypoints = std::move(newPath);
                        follower.currentWaypoint = 0;
                        follower.hasPath = true;
                    }
                }
            }
        }

        // ─── Follow the path ────────────────────────────────────
        if (!follower.hasPath || follower.waypoints.empty()) {
            continue;  // No path — do nothing
        }

        if (follower.currentWaypoint >= static_cast<int>(
                follower.waypoints.size())) {
            // Reached end of path
            follower.hasPath = false;
            vel.value.x = 0.0f;
            vel.value.z = 0.0f;
            continue;
        }

        // Steer toward the current waypoint
        glm::vec3 target = follower.waypoints[follower.currentWaypoint];
        glm::vec3 toTarget = target - pos.value;
        toTarget.y = 0.0f;  // Ignore vertical difference for XZ movement

        float distance = glm::length(toTarget);

        if (distance < follower.waypointRadius) {
            // Close enough — advance to next waypoint
            follower.currentWaypoint++;

            if (follower.currentWaypoint >= static_cast<int>(
                    follower.waypoints.size())) {
                // Path complete
                follower.hasPath = false;
                vel.value.x = 0.0f;
                vel.value.z = 0.0f;
            }
            continue;
        }

        // Set velocity toward waypoint
        // Use the entity's movement speed from AIBrain (if present)
        float speed = 3.0f;  // Default speed
        if (registry.all_of<AIBrain>(entity)) {
            speed = registry.get<AIBrain>(entity).moveSpeed;
        }

        glm::vec3 direction = toTarget / distance;  // Normalise
        vel.value.x = direction.x * speed;
        vel.value.z = direction.z * speed;
    }
}
```

---

## Integration with AI System

Chapter 14's AI system had enemies moving directly toward the player during the Chase state. Now we update it to request a path instead:

```cpp
// In src/engine/ecs/systems/ai_system.cpp — updated snippets

void aiSystem(entt::registry& registry, float dt) {
    // Find the player position (needed for AI decisions)
    glm::vec3 playerPos{0.0f};
    auto playerView = registry.view<TagPlayer, Position>();
    for (auto [entity, tag, pos] : playerView.each()) {
        playerPos = pos.value;
        break;
    }

    auto view = registry.view<Position, Velocity, AIBrain, PathFollower>();
    for (auto [entity, pos, vel, brain, follower] : view.each()) {
        // Update timers, sight checks, etc. (existing code from Ch 14)
        // ...

        switch (brain.state) {
            case AIState::Idle:
                // No movement
                vel.value.x = 0.0f;
                vel.value.z = 0.0f;
                follower.hasPath = false;
                break;

            case AIState::Patrolling: {
                // Follow path between patrol waypoints
                if (!follower.hasPath && !brain.patrolPoints.empty()) {
                    // Set target to next patrol point
                    follower.targetPosition =
                        brain.patrolPoints[brain.currentPatrolIndex];
                    follower.repathTimer = 0.0f;  // Request path immediately
                }

                // Check if we reached the patrol point
                if (follower.hasPath && follower.currentWaypoint >=
                        static_cast<int>(follower.waypoints.size())) {
                    // Advance to next patrol point
                    brain.currentPatrolIndex =
                        (brain.currentPatrolIndex + 1)
                        % static_cast<int>(brain.patrolPoints.size());
                    follower.hasPath = false;
                }
                break;
            }

            case AIState::Chasing: {
                // BEFORE (Ch 14 — direct movement):
                //   glm::vec3 dir = glm::normalize(playerPos - pos.value);
                //   vel.value.x = dir.x * brain.moveSpeed;
                //   vel.value.z = dir.z * brain.moveSpeed;

                // AFTER (with pathfinding):
                // Set the target to the player's current position.
                // The pathFollowingSystem handles repathing periodically.
                follower.targetPosition = playerPos;

                // If we don't have a path yet, request one immediately
                if (!follower.hasPath) {
                    follower.repathTimer = 0.0f;
                }
                break;
            }

            case AIState::Attacking:
                // Stop moving, face the player, attack
                vel.value.x = 0.0f;
                vel.value.z = 0.0f;
                follower.hasPath = false;
                // ... attack logic from Ch 14
                break;
        }
    }
}
```

The key change: `AIState::Chasing` no longer sets velocity directly. It sets `follower.targetPosition` and lets the path following system handle the route. The enemy re-paths every `repathInterval` seconds (default 1.0), so it tracks a moving player without running A* every frame.

---

## Debug Visualisation

Using the debug renderer and console from Chapter 27, we add commands to visualise navigation data:

```cpp
// In src/game/debug_commands.cpp — inside registerDebugCommands()

// ─── show_navgrid ────────────────────────────────────────────
console.registerCommand("show_navgrid", "Toggle nav grid overlay",
    [&registry, &console](const std::vector<std::string>& args) {
        auto* flags = registry.ctx().find<DebugFlags>();
        if (flags) {
            flags->showNavGrid = !flags->showNavGrid;
            console.print(flags->showNavGrid ? "Nav grid: ON" : "Nav grid: OFF");
        }
    });

// ─── show_navmesh ────────────────────────────────────────────
console.registerCommand("show_navmesh", "Toggle nav mesh overlay",
    [&registry, &console](const std::vector<std::string>& args) {
        auto* flags = registry.ctx().find<DebugFlags>();
        if (flags) {
            flags->showNavMesh = !flags->showNavMesh;
            console.print(flags->showNavMesh ? "Nav mesh: ON" : "Nav mesh: OFF");
        }
    });

// ─── show_paths ──────────────────────────────────────────────
console.registerCommand("show_paths", "Toggle entity path lines",
    [&registry, &console](const std::vector<std::string>& args) {
        auto* flags = registry.ctx().find<DebugFlags>();
        if (flags) {
            flags->showPaths = !flags->showPaths;
            console.print(flags->showPaths ? "Paths: ON" : "Paths: OFF");
        }
    });
```

The rendering for these goes in the debug render pass:

```cpp
// In src/engine/debug/debug_renderer.cpp — inside renderDebugOverlays()

void renderDebugOverlays(const entt::registry& registry,
                          const DebugFlags& flags,
                          DebugRenderer& dbg) {
    // ─── Nav Grid ────────────────────────────────────────────
    if (flags.showNavGrid) {
        auto* grid = registry.ctx().find<NavGrid>();
        if (grid) {
            float cs = grid->getCellSize();
            for (int z = 0; z < grid->getDepth(); z++) {
                for (int x = 0; x < grid->getWidth(); x++) {
                    glm::vec3 centre = grid->gridToWorld(glm::ivec2(x, z));
                    centre.y += 0.05f;  // Slight offset above floor

                    glm::vec4 colour = grid->isWalkable(x, z)
                        ? glm::vec4(0.0f, 0.6f, 0.0f, 0.3f)   // Green
                        : glm::vec4(0.8f, 0.0f, 0.0f, 0.5f);  // Red

                    float half = cs * 0.45f;
                    dbg.drawQuad(centre, half, half, colour);
                }
            }
        }
    }

    // ─── Nav Mesh ────────────────────────────────────────────
    if (flags.showNavMesh) {
        auto* mesh = registry.ctx().find<NavMesh>();
        if (mesh) {
            glm::vec4 edgeColour(0.0f, 0.8f, 1.0f, 0.7f);
            for (const auto& poly : mesh->polygons) {
                int n = static_cast<int>(poly.vertices.size());
                for (int i = 0; i < n; i++) {
                    glm::vec3 a = poly.vertices[i];
                    glm::vec3 b = poly.vertices[(i + 1) % n];
                    a.y += 0.05f;
                    b.y += 0.05f;
                    dbg.drawLine(a, b, edgeColour);
                }
            }
        }
    }

    // ─── Entity Paths ────────────────────────────────────────
    if (flags.showPaths) {
        glm::vec4 pathColour(1.0f, 1.0f, 0.0f, 0.9f);      // Yellow line
        glm::vec4 waypointColour(1.0f, 0.5f, 0.0f, 0.9f);   // Orange dot

        auto view = registry.view<const PathFollower>();
        for (auto [entity, follower] : view.each()) {
            if (!follower.hasPath) continue;

            for (size_t i = 0; i + 1 < follower.waypoints.size(); i++) {
                glm::vec3 a = follower.waypoints[i];
                glm::vec3 b = follower.waypoints[i + 1];
                a.y += 0.1f;
                b.y += 0.1f;
                dbg.drawLine(a, b, pathColour);
            }

            // Highlight current waypoint
            if (follower.currentWaypoint < static_cast<int>(
                    follower.waypoints.size())) {
                glm::vec3 wp = follower.waypoints[follower.currentWaypoint];
                wp.y += 0.1f;
                dbg.drawPoint(wp, 8.0f, waypointColour);
            }
        }
    }
}
```

Add the new flags to the DebugFlags struct:

```cpp
// In src/engine/debug/debug_flags.h — add these fields

struct DebugFlags {
    // ... existing flags from Ch 27 ...
    bool showNavGrid = false;
    bool showNavMesh = false;
    bool showPaths   = false;
};
```

---

## Wiring It All Together

During level loading, build the navigation data and store it as a registry context variable:

```cpp
// In your level loading code (LevelManager or similar)

// After level geometry and entities are loaded:

// Option A: Grid-based navigation
NavGrid navGrid = buildNavGrid(registry, levelWidth, levelDepth, 1.0f);
registry.ctx().emplace<NavGrid>(std::move(navGrid));

// Option B: Nav mesh from sectors
NavMesh navMesh = buildNavMeshFromSectors(sectors);
registry.ctx().emplace<NavMesh>(std::move(navMesh));

// Both can coexist — pathFollowingSystem checks for NavMesh first,
// falls back to NavGrid
```

And in the game loop, call pathFollowingSystem after aiSystem:

```cpp
// In PlayingState::update()
aiSystem(m_registry, dt);
pathFollowingSystem(m_registry, dt);   // NEW — after AI sets targets
physicsSystem(m_registry, dt);
// ... rest of systems
```

During level teardown (Chapter 34), the navigation data is cleaned up automatically when the registry context is cleared.

---

## C++ Concept: `std::priority_queue` and Custom Comparators

The A* algorithm needs an open set sorted by f-cost, with the **lowest** f-cost at the top. C++'s `std::priority_queue` is a max-heap by default -- it puts the **largest** element on top. We need to flip the comparison.

### The Default (Max-Heap)

```cpp
#include <queue>

// Default: largest element on top
std::priority_queue<int> maxHeap;
maxHeap.push(3);
maxHeap.push(1);
maxHeap.push(5);
maxHeap.top();  // Returns 5 (largest)
```

### Min-Heap with std::greater

The simplest fix for built-in types is `std::greater`:

```cpp
#include <queue>
#include <functional>

// Min-heap: smallest element on top
std::priority_queue<int, std::vector<int>, std::greater<int>> minHeap;
minHeap.push(3);
minHeap.push(1);
minHeap.push(5);
minHeap.top();  // Returns 1 (smallest)
```

The three template parameters are: element type, underlying container, comparator. The comparator returns `true` if the first argument should go **below** the second in the heap. So `greater<int>` means "larger values sink to the bottom" -- a min-heap.

### Custom Comparator Struct

For A*, we compare custom objects by their f-cost:

```cpp
struct OpenEntry {
    int nodeIndex;
    float fCost;
};

// Comparator struct: operator() returns true if a should be below b
struct CompareF {
    bool operator()(const OpenEntry& a, const OpenEntry& b) const {
        return a.fCost > b.fCost;  // Higher f-cost sinks down = min-heap
    }
};

std::priority_queue<OpenEntry, std::vector<OpenEntry>, CompareF> openSet;
```

### Lambda Comparator

You can also use a lambda, but the syntax is more verbose because the priority_queue needs the comparator's type:

```cpp
auto cmp = [](const OpenEntry& a, const OpenEntry& b) {
    return a.fCost > b.fCost;
};
std::priority_queue<OpenEntry, std::vector<OpenEntry>, decltype(cmp)>
    openSet(cmp);
```

### The Decrease-Key Problem

`std::priority_queue` does not support updating the priority of an element already in the queue. In A*, when we find a shorter path to a node already in the open set, we need to update its f-cost. There are two common workarounds:

**Approach 1: Lazy deletion** (what we used). Push duplicates and skip stale entries:

```cpp
// Push a new entry even if the node is already in the queue
openSet.push({ nodeIndex, newFCost });

// When popping, skip nodes that are already in the closed set
while (!openSet.empty()) {
    auto current = openSet.top();
    openSet.pop();
    if (closedSet.count(current.nodeIndex)) continue;  // Stale — skip
    // ... process node
}
```

This is simple and fast in practice. The queue may contain a few duplicates, but they are filtered out cheaply.

**Approach 2: Use `std::set` instead**. It supports `erase` + `insert` for decrease-key:

```cpp
// std::set is sorted and supports efficient removal
struct OpenEntry {
    float fCost;
    int nodeIndex;
    bool operator<(const OpenEntry& other) const {
        if (fCost != other.fCost) return fCost < other.fCost;
        return nodeIndex < other.nodeIndex;  // Tie-break for strict ordering
    }
};

std::set<OpenEntry> openSet;

// Decrease-key: erase old entry, insert new one
openSet.erase({ oldFCost, nodeIndex });
openSet.insert({ newFCost, nodeIndex });

// Pop minimum:
auto it = openSet.begin();  // Smallest element
OpenEntry current = *it;
openSet.erase(it);
```

The `std::set` approach gives true O(log n) decrease-key, but the constant factor is higher than `priority_queue` due to tree node allocations. For game pathfinding with hundreds or low thousands of nodes, the lazy deletion approach with `priority_queue` is almost always fast enough and simpler to implement.

---

## What's Next

Chapter 38 adds **Instanced Rendering** -- drawing hundreds or thousands of identical objects (grass tufts, debris, torches, bullet casings) with a single draw call. Instead of submitting each object separately to the GPU, we pack their transforms into a buffer and let the hardware do the work. It is the difference between a level that runs at 15 FPS with a thousand decorative objects and one that runs at 60.
