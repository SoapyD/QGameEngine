# Chapter 24: Enemy Pathfinding — a Nav Grid and A*

## What You'll Learn
- Why a **straight-line chase** (Chapter 23) isn't enough — a kinematic body steered directly at
  the player clips walls and jams against corners, because "closer" and "reachable" aren't the same
  thing
- Modelling walkability as a **2D grid** — the `NavGrid` struct over the level's XZ bounds, and its
  cell↔world helpers (`cellOf`, `center`, `isBlocked`)
- **Building the grid** in `buildNavGrid`: deriving bounds from level surfaces, blocking cells under
  *wall* surfaces (tall Y-extent, not floors/ceilings) and under solid prop/mover colliders, inflated
  by an enemy-clearance margin — and *skipping* triggers, the player, enemies, and demo cubes
- Where the grid is built (`buildWorld`) and how it's stashed in the **registry context** so any
  system can reach it
- Writing **A\*** (`findPath`): 8-connected movement, the octile heuristic, blocking diagonal
  corner-cutting, snapping blocked endpoints to open cells, a node budget, and returning waypoint
  centres
- The **`AIPath` component** and how `aiSystem`'s Chase state consumes it — recompute on a timer or
  when the path runs out (throttled by a per-tick repath budget), advance the waypoint cursor, steer
  to the current waypoint, and fall back to straight-at-the-player when there's no path
- The deliberate **v1 aggro shortcut**: pathing to the player's *live* position rather than a
  separate last-known position
- Wiring: the two new `ai/*.cpp` files in CMake, and a `monster_path` headless scenario that proves a
  grunt routes *around* a shelf without clipping

---

## Where We Are

Chapter 22 built the grunt as a physical object — solid, shootable, killable. Chapter 23 gave it a
mind: an `aiSystem` that senses the player through line-of-sight, aggros within detect range, drops
into `Attack` on a cooldown when it's close, and otherwise **chases**. But Chapter 23 chased the
crudest way possible — it pointed the grunt's kinematic body straight at the player and pushed. On an
empty floor that looks fine. Put a wall between them and it falls apart: the grunt drives its nose
into the wall, the kinematic sweep stops it dead, and it sits there grinding against the geometry
forever because *straight at the player* points *into the wall*.

This chapter fixes that. We give the enemy a **pathfinder**: a way to compute a route that goes
*around* obstacles, not through them. It comes in three pieces, built data-first:

1. **The `NavGrid`** — a coarse 2D walkability map of the level.
2. **`buildNavGrid`** — how we derive that map from the level's walls and props.
3. **`findPath` (A\*)** — the search that turns "I'm here, the player's there" into a list of
   waypoints that avoids the blocked cells.

Then we hang it off the behaviour from Chapter 23: an `AIPath` component to hold the route, and a
rewrite of the Chase state so it *follows waypoints* instead of steering blindly. Finally we prove it
with a headless scenario where a grunt has to walk around a shelf to reach you.

---

## Step 1: Why a Straight-Line Chase Clips Walls

It's worth being precise about what breaks, because it explains every design choice that follows.
Chapter 23's Chase state does essentially this: take the flat (XZ) vector from the grunt to the
player, normalise it, and move the kinematic body a small step along it each tick.

```cpp
glm::vec3 flat = playerPos - pos.value; flat.y = 0.0f;
float dist = glm::length(flat);
glm::vec3 toPlayer = dist > 0.001f ? flat / dist : glm::vec3(0.0f);
// ... move the kinematic body toward `toPlayer`
```

`toPlayer` always points at where the player *is*, in a dead straight line. But a straight line is
only walkable if nothing's in the way. The moment a wall, a pillar, or a shelf sits between the grunt
and the player, that straight line runs *through solid geometry*. A kinematic body doesn't tunnel —
Jolt sweeps it and stops it at the wall — so the grunt pins itself against the obstacle and never gets
round it. "Closer to the player" and "makes progress toward reaching the player" have come apart, and
the grunt is optimising the wrong one.

The fix is a classic one: stop steering toward the *goal* and start steering toward the *next
reachable point on a route to the goal*. To compute that route we need two things a raw 3D level
doesn't give us cheaply — a notion of which spots are walkable, and a search over them. That's the
`NavGrid` and A*.

> **Why a grid, rather than a polygon navmesh like commercial engines use?** A navmesh (a set of
> connected walkable polygons) is more precise and produces smoother paths, but it's a lot of
> machinery to generate correctly: you have to walk the level geometry, project floors, subtract
> obstacles, and triangulate — and then implement funnel-smoothing on top. A **uniform grid** is the
> blunt, honest version of the same idea: chop the floor into 1-metre cells, mark the ones an enemy
> can't stand in, and search cell-to-cell. It over-approximates (a diagonal wall becomes a staircase
> of blocked squares) and the paths are a touch blocky, but it's a hundred lines total, it's trivial
> to reason about, and for a boxy arena of walls and shelves it's completely adequate. It's the right
> amount of pathfinding for where the engine is. A navmesh can come later if the levels demand it.

---

## Step 2: The `NavGrid` — a Walkability Map

Data before systems, as always. Before we can build or search anything we need the structure that
*holds* the walkability map. It's a new module — create `src/engine/ai/types/nav_grid.h`:

```cpp
#pragma once

#include <glm/glm.hpp>

#include <cmath>
#include <cstdint>
#include <vector>

// A 2D walkability grid over the level's XZ bounds, used by enemy pathfinding.
// Cell (cx, cz) covers [origin.x + cx*cell, +cell) × [origin.z + cz*cell, +cell);
// `blocked[cz*cols + cx]` is 1 where a wall/prop (inflated by enemy clearance)
// sits. Built once in buildWorld (rebuild when the level changes).
struct NavGrid
{
    glm::vec3 origin{ 0.0f };          // min-corner world position (origin.y = walk height)
    float cell = 1.0f;
    int cols = 0;                      // cells along +X
    int rows = 0;                      // cells along +Z
    std::vector<uint8_t> blocked;      // cols*rows, 1 = blocked

    bool inBounds(int cx, int cz) const { return cx >= 0 && cz >= 0 && cx < cols && cz < rows; }
    int  index(int cx, int cz)   const { return cz * cols + cx; }

    // Out-of-bounds counts as blocked (so search + snap stay inside the grid).
    bool isBlocked(int cx, int cz) const
    {
        return !inBounds(cx, cz) || blocked[index(cx, cz)] != 0;
    }

    void cellOf(const glm::vec3& p, int& cx, int& cz) const
    {
        cx = (int)std::floor((p.x - origin.x) / cell);
        cz = (int)std::floor((p.z - origin.z) / cell);
    }

    glm::vec3 center(int cx, int cz) const
    {
        return { origin.x + (cx + 0.5f) * cell, origin.y, origin.z + (cz + 0.5f) * cell };
    }
};
```

The grid is deliberately flat: a rectangle of `cols × rows` cells laid over the level's XZ footprint,
each `cell` metres square, with a single byte per cell (`0` = walkable, `1` = blocked) stored in a
row-major `blocked` vector. Everything else is a convenience:

- **`origin`** is the world position of the grid's min corner. Note `origin.y` isn't zero — it's the
  *walk height*, roughly a grunt's centre. That way `center(cx, cz)` returns a point at the height the
  enemy actually stands, so waypoints are usable as move targets with no extra Y bookkeeping.
- **`index(cx, cz)`** is the row-major flattening, `cz * cols + cx`.
- **`cellOf(p, cx, cz)`** maps a world point *down* to the cell that contains it (floor-divide by
  `cell`, relative to `origin`). This is how we find "which cell is the grunt in" and "which cell is
  the player in".
- **`center(cx, cz)`** maps a cell *up* to the world point at its centre — the value A* hands back as
  a waypoint.
- **`inBounds`** is a plain range check, but **`isBlocked`** is the one the search actually calls, and
  it folds two rules into one: a cell is blocked if it's off-grid *or* its byte is set.

> **Why does `isBlocked` treat out-of-bounds cells as blocked rather than erroring or clamping?**
> Because the search and the endpoint-snapping both probe *neighbours* of a cell, and neighbours near
> the edge of the grid fall off it. If out-of-bounds were "walkable" the enemy could path off the map;
> if it threw or asserted, every neighbour loop would need an explicit bounds guard first. Folding
> "off-grid ⇒ blocked" into the single accessor everyone already calls means the whole rest of the
> code — the eight-way neighbour expansion, the corner-cut test, the snap rings — can call
> `isBlocked(nx, nz)` fearlessly and get a safe, correct answer at the boundary for free. It turns the
> grid's edge into an implicit wall, which is exactly what you want.

Put this header in a new `types/` folder under a new `src/engine/ai/` module — pathfinding is its own
concern, and keeping it out of the `ecs/` tree signals that it's a library the AI *uses*, not part of
the component/system machinery itself.

---

## Step 3: Building the Grid — `buildNavGrid`

Now the function that fills a `NavGrid` in from a loaded level. Create
`src/engine/ai/build_nav_grid.h`:

```cpp
#pragma once

#include <entt/entt.hpp>

struct Level;
struct NavGrid;

// Build the enemy walkability grid from the level (wall surfaces) and the solid
// prop/mover colliders currently in the registry. Triggers, the player, enemies,
// and dynamic demo props are ignored. Call once the scene is populated.
NavGrid buildNavGrid(entt::registry& registry, const Level& level);
```

and `src/engine/ai/build_nav_grid.cpp`:

```cpp
#include "engine/ai/build_nav_grid.h"
#include "engine/ai/types/nav_grid.h"

#include "engine/ecs/components.h"
#include "engine/level/level.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr float kCell      = 1.0f;
    constexpr float kClearance = 0.5f;   // enemy half-width margin around obstacles
    constexpr float kWallMinY  = 1.0f;   // a surface taller than this blocks (wall, not floor)

    // Mark every cell whose centre lies within the XZ box [mn, mx] inflated by
    // `pad` as blocked.
    void blockBox(NavGrid& g, glm::vec3 mn, glm::vec3 mx, float pad)
    {
        int x0, z0, x1, z1;
        g.cellOf({ mn.x - pad, 0.0f, mn.z - pad }, x0, z0);
        g.cellOf({ mx.x + pad, 0.0f, mx.z + pad }, x1, z1);
        for (int cz = z0; cz <= z1; ++cz)
            for (int cx = x0; cx <= x1; ++cx)
                if (g.inBounds(cx, cz)) g.blocked[g.index(cx, cz)] = 1;
    }
}
```

`blockBox` is the one primitive the builder uses: given a world-space XZ box, inflate it by `pad`,
convert the two corners to cells with `cellOf`, and stamp `1` into every in-bounds cell in that
rectangle. Everything else is deciding *which* boxes to stamp.

The build itself runs in three phases — bounds, then walls, then colliders:

```cpp
NavGrid buildNavGrid(entt::registry& registry, const Level& level)
{
    // XZ bounds from every level surface vertex.
    glm::vec3 mn(1e9f), mx(-1e9f);
    for (const auto& sector : level.sectors)
        for (const auto& s : sector.surfaces)
            for (const auto& v : s.vertices) { mn = glm::min(mn, v); mx = glm::max(mx, v); }
    if (mn.x > mx.x) { mn = glm::vec3(0.0f); mx = glm::vec3(30.0f, 6.0f, 30.0f); }  // fallback

    NavGrid g;
    g.cell   = kCell;
    g.origin = glm::vec3(mn.x, 0.95f, mn.z);   // walk height ~ grunt centre
    g.cols   = std::max(1, (int)std::ceil((mx.x - mn.x) / kCell));
    g.rows   = std::max(1, (int)std::ceil((mx.z - mn.z) / kCell));
    g.blocked.assign((size_t)g.cols * g.rows, 0);
```

**Phase one — bounds.** Sweep every vertex of every surface in every sector to find the level's
axis-aligned XZ extent (`mn`/`mx`). If there were no surfaces at all (`mn.x > mx.x` — the sentinels
never got overwritten), fall back to a 30×30 box so we never build a zero-size grid. Then size the
grid: `cell` is 1 metre, `origin` is the min corner lifted to walk height `0.95`, and `cols`/`rows` is
the extent divided by cell size, rounded up and floored at 1. Start every cell walkable.

```cpp
    // Walls (tall surfaces) block their footprint; floors/ceilings don't.
    for (const auto& sector : level.sectors)
        for (const auto& s : sector.surfaces)
        {
            glm::vec3 smn(1e9f), smx(-1e9f);
            for (const auto& v : s.vertices) { smn = glm::min(smn, v); smx = glm::max(smx, v); }
            if (smx.y - smn.y < kWallMinY) continue;   // floor/ceiling
            blockBox(g, smn, smx, kClearance);
        }
```

**Phase two — walls.** Here's the crux of the whole builder: **not every surface should block.** A
floor is a surface. A ceiling is a surface. If we blocked every surface's footprint the entire floor
would be marked unwalkable and no path would ever exist. So we look at each surface's Y-extent
(`smx.y - smn.y`): a floor or ceiling is nearly flat (tiny Y-extent) and is *skipped*; a wall is tall
(Y-extent ≥ `kWallMinY = 1.0`) and gets its XZ footprint stamped blocked. That single "is it taller
than a metre?" test is what separates the geometry an enemy walks *on* from the geometry it must walk
*around*.

```cpp
    // Solid props/movers block. Skip triggers, the player, enemies, demo cubes.
    for (auto [e, pos, col] : registry.view<Position, AABBCollider>().each())
    {
        if (col.isTrigger) continue;
        if (registry.any_of<TagPlayer, AIState, DemoReset>(e)) continue;
        blockBox(g, pos.value - col.halfExtents, pos.value + col.halfExtents, kClearance);
    }

    return g;
}
```

**Phase three — colliders.** Walls come from the level's static surfaces, but *props* — the shelf,
pillars, door frames, crates — are entities with an `AABBCollider`. Sweep them and block each one's
box, with three exclusions that matter:

- **`col.isTrigger` → skip.** Trigger volumes (lava, teleporters, jump pads) are sensors, not solid
  geometry. An enemy should be able to walk across the *cell* a trigger occupies (it just won't react
  to the trigger — see Chapter 22), so triggers must not block the grid.
- **`TagPlayer`, `AIState`, `DemoReset` → skip.** The player and the enemies *move*; baking their
  current cell into a static grid would leave a phantom blocked square wherever they happened to be
  standing at build time. `DemoReset` is the showcase's respawning demo cube — dynamic clutter that
  shouldn't wall anything off either.
- Everything else that's solid — the shelf, decor pillars, mover doors — gets blocked.

Every box in phases two and three is inflated by `kClearance = 0.5`. The grunt isn't a point; it has
roughly a half-metre half-width. If we blocked only the literal footprint of a wall, A* would happily
route a path through a cell whose *centre* is walkable but whose edge overlaps the wall — and the
grunt's body would clip the corner. Padding every obstacle by the enemy's half-width means a cell is
only left open if the enemy can actually stand there without intersecting anything.

> **Why derive walls from *surface Y-extent* instead of tagging walls explicitly in the level data?**
> Because the level format (Chapter-era sectors and surfaces) has no "this is a wall" flag — a surface
> is just four vertices and a normal, and floors, walls, and ceilings are all the same struct. We
> could add a wall flag, but that pushes work onto every level author and every surface, to encode
> something the geometry already tells us: a surface you can stand *on* is flat, a surface you'd walk
> *into* is tall. The `kWallMinY` height test reads that intent straight off the vertices with zero
> new data. It's a heuristic, and a slanted ramp taller than a metre would be misclassified as a wall
> — but the arena has none, and the honesty of "no hidden metadata, just measure the geometry" is
> worth more than the edge case here.

> **Why build the grid from a *snapshot* of the registry and level, knowing it goes stale the moment a
> door opens or a prop moves?** Because the arena's obstacles are, in practice, static enough: the
> shelf and pillars never move, and while a door *is* a mover, treating its closed footprint as
> permanently blocked is a safe over-approximation — the worst case is an enemy waits for a route it
> thinks is walled when a door happens to be open. A grid that tracked live geometry would have to be
> rebuilt (or incrementally patched) every time anything moved, which is real cost for little gain at
> this stage. Building once, from the fully-populated scene, is the pragmatic v1. Rebuilding the grid
> on level change (or on a door state flip) is a clearly-scoped future improvement, and the struct's
> comment says as much.

### Wiring the grid into `buildWorld`

The grid has to be built *after* the scene is fully populated — every surface loaded, every prop and
enemy spawned, every kinematic body created — because it reads all of them. That's the tail end of
`buildWorld` in `src/engine/app/simulation.cpp`. Add the includes:

```cpp
#include "engine/ai/build_nav_grid.h"
#include "engine/ai/types/nav_grid.h"
```

and build the grid just before `buildWorld` returns, right after Jolt's broadphase is optimised:

```cpp
        joltWorld.physicsSystem->OptimizeBroadPhase();

        // Enemy pathfinding grid, derived from the now-populated scene.
        registry.ctx().emplace<NavGrid>(buildNavGrid(registry, level));

        return level;
}
```

The finished `NavGrid` goes into the **registry context** (`registry.ctx().emplace<NavGrid>(...)`) —
the same singleton-storage mechanism the engine already uses for `PhysicsConfig` and `JoltWorld`. That
makes exactly one grid, owned by the registry, reachable from any system that asks for it, without
threading it through function signatures.

> **Why store the grid in the registry context instead of as a component, a global, or a parameter to
> `aiSystem`?** It's a single, world-scoped resource — there is one nav grid, not one per entity — so
> a component (which lives *on* an entity) is the wrong shape. A global would work but couples the AI
> module to a translation-unit singleton and makes tests that spin up a fresh registry awkward. The
> context is EnTT's blessed home for exactly this: per-registry singletons that systems fetch by type.
> `aiSystem` reaches it with `registry.ctx().find<NavGrid>()` — `find`, not `get`, so it returns a
> pointer that's null if no grid was built (a headless test that skips `buildWorld`, say), and the
> system degrades gracefully instead of crashing.

---

## Step 4: A* Over the Grid — `findPath`

With a grid of blocked/open cells, computing a route is a textbook **A\*** search. Create
`src/engine/ai/find_path.h`:

```cpp
#pragma once

#include <glm/glm.hpp>
#include <vector>

struct NavGrid;

// A* over the walkability grid from `from` to `to`. Returns the waypoint centres
// to walk (excluding the start cell), or empty if unreachable, within one cell,
// or the node budget is exceeded. Blocked start/goal cells snap to the nearest
// open cell first. 8-connected, no diagonal corner-cutting.
std::vector<glm::vec3> findPath(const NavGrid& grid, const glm::vec3& from, const glm::vec3& to);
```

The signature is deliberately world-space in and world-space out: hand it two 3D points, get back a
list of 3D waypoint centres. The caller never touches cell indices. Now
`src/engine/ai/find_path.cpp`, starting with the constants and the endpoint-snapping helper:

```cpp
#include "engine/ai/find_path.h"
#include "engine/ai/types/nav_grid.h"

#include <algorithm>
#include <cmath>
#include <queue>
#include <utility>
#include <vector>

namespace
{
    constexpr float kDiag = 1.41421356f;
    constexpr int   kMaxExpanded = 4000;   // node budget — bail on pathological maps

    // Nudge a blocked cell to the nearest open one within a few rings.
    void snapToOpen(const NavGrid& g, int& cx, int& cz)
    {
        if (!g.isBlocked(cx, cz)) return;
        for (int r = 1; r <= 4; ++r)
            for (int dz = -r; dz <= r; ++dz)
                for (int dx = -r; dx <= r; ++dx)
                {
                    if (std::abs(dx) != r && std::abs(dz) != r) continue;  // ring only
                    if (!g.isBlocked(cx + dx, cz + dz)) { cx += dx; cz += dz; return; }
                }
    }
}
```

`snapToOpen` handles a real-world nuisance: the start or goal point often lands *in a blocked cell*.
Remember every obstacle was inflated by `kClearance`, and the player can stand right up against a wall
— so the cell the player occupies may well be marked blocked. If we searched from or to a blocked
cell, A* would instantly fail. Instead we nudge a blocked endpoint outward in expanding square
*rings* (radius 1, then 2, up to 4) and take the first open cell we find. The `if (std::abs(dx) != r
&& std::abs(dz) != r) continue;` line is what makes it a *ring* and not a filled square — it skips any
cell that isn't on the current radius's perimeter, so we genuinely search nearest-first.

Now the search:

```cpp
std::vector<glm::vec3> findPath(const NavGrid& grid, const glm::vec3& from, const glm::vec3& to)
{
    if (grid.cols <= 0 || grid.rows <= 0) return {};

    int sx, sz, gx, gz;
    grid.cellOf(from, sx, sz);
    grid.cellOf(to,   gx, gz);
    snapToOpen(grid, sx, sz);
    snapToOpen(grid, gx, gz);
    if (grid.isBlocked(sx, sz) || grid.isBlocked(gx, gz)) return {};
    if (sx == gx && sz == gz) return {};

    const int   n     = grid.cols * grid.rows;
    const int   start = grid.index(sx, sz);
    const int   goal  = grid.index(gx, gz);

    std::vector<float>   gScore(n, 1e18f);
    std::vector<int>     cameFrom(n, -1);
    std::vector<uint8_t> closed(n, 0);

    auto heuristic = [&](int x, int z)
    {
        float dx = (float)std::abs(x - gx), dz = (float)std::abs(z - gz);
        return (dx + dz) + (kDiag - 2.0f) * std::min(dx, dz);   // octile distance
    };
```

The preamble converts both endpoints to cells, snaps each to open ground, and bails early on three
edge cases: an empty grid, an endpoint that's *still* blocked after snapping (walled in), or start and
goal being the same cell (nothing to walk — return empty and let the caller steer straight). Then it
allocates the three per-cell arrays A* needs — `gScore` (best cost found to reach each cell,
initialised to effectively infinity), `cameFrom` (each cell's predecessor, for reconstructing the
path), and `closed` (already-finalised cells) — indexed by the flattened cell index.

The `heuristic` is the **octile distance**: the exact length of the shortest 8-connected path across
*empty* ground, where diagonal steps cost `kDiag ≈ 1.414` and straight steps cost 1. Reading it,
`(dx + dz)` is the Manhattan distance and `(kDiag - 2) * min(dx, dz)` is the discount for the moves
you can take diagonally instead of as two separate axis steps.

> **Why octile distance and not straight-line (Euclidean) distance for the heuristic?** A* is optimal
> and fast only when the heuristic never *over*-estimates the true remaining cost (it's "admissible").
> Our movement is 8-connected with those specific 1-and-1.414 step costs, and octile distance is the
> exact cost of crossing open ground under *those* rules — it's the tightest admissible estimate
> available, so A* explores the fewest cells. Euclidean distance is also admissible but looser (it
> assumes you can move in any direction, which our grid can't), so it would expand more nodes for the
> same answer. Matching the heuristic to the actual move-set is what keeps the search cheap.

```cpp
    using PQItem = std::pair<float, int>;   // (fScore, cellIndex)
    std::priority_queue<PQItem, std::vector<PQItem>, std::greater<PQItem>> open;
    gScore[start] = 0.0f;
    open.push({ heuristic(sx, sz), start });

    static const int dxs[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
    static const int dzs[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };

    int expanded = 0;
    bool found = false;
    while (!open.empty() && expanded < kMaxExpanded)
    {
        int cur = open.top().second; open.pop();
        if (closed[cur]) continue;
        closed[cur] = 1;
        ++expanded;
        if (cur == goal) { found = true; break; }

        int cx = cur % grid.cols, cz = cur / grid.cols;
        for (int i = 0; i < 8; ++i)
        {
            int nx = cx + dxs[i], nz = cz + dzs[i];
            if (grid.isBlocked(nx, nz)) continue;
            bool diag = dxs[i] != 0 && dzs[i] != 0;
            if (diag && (grid.isBlocked(cx + dxs[i], cz) || grid.isBlocked(cx, cz + dzs[i])))
                continue;   // don't cut blocked corners
            int ni = grid.index(nx, nz);
            float ng = gScore[cur] + (diag ? kDiag : 1.0f);
            if (ng < gScore[ni])
            {
                gScore[ni] = ng;
                cameFrom[ni] = cur;
                open.push({ ng + heuristic(nx, nz), ni });
            }
        }
    }

    if (!found) return {};
```

This is the standard A* loop. The `open` set is a min-priority-queue keyed on `fScore = gScore +
heuristic` (the `std::greater` comparator makes `priority_queue` pop the *smallest* f first). Each
iteration pops the most promising cell, marks it closed, and — if it's the goal — stops. Otherwise it
relaxes the eight neighbours: for each open neighbour, the tentative cost `ng` to reach it is the
current cell's cost plus one step (`kDiag` diagonally, `1.0` straight); if that beats the neighbour's
best-known `gScore`, we record the improvement and push it. The `dxs`/`dzs` arrays enumerate the four
orthogonal then four diagonal offsets.

Two guards make it robust and correct:

- **The node budget.** `expanded < kMaxExpanded` (4000 cells) caps the search. On a pathological or
  genuinely unreachable target A* would otherwise churn through the whole grid; the budget bounds the
  per-call cost so one confused enemy can't stall the frame.
- **No diagonal corner-cutting.** When taking a diagonal step, we *also* check the two orthogonal
  cells that flank it (`cx+dxs[i], cz` and `cx, cz+dzs[i]`). If *either* is blocked, the diagonal
  would clip the corner of an obstacle — the enemy would visually cut through a wall's corner — so we
  forbid it. A diagonal is only allowed when both flanking cells are open.

Finally, reconstruct and trim the path:

```cpp
    std::vector<glm::vec3> path;
    for (int c = goal; c != -1; c = cameFrom[c])
        path.push_back(grid.center(c % grid.cols, c / grid.cols));
    std::reverse(path.begin(), path.end());
    if (!path.empty()) path.erase(path.begin());   // drop the start cell
    return path;
}
```

Walk `cameFrom` back from the goal to the start, converting each cell to its world-space centre with
`grid.center`, then reverse so it reads start→goal. The last touch: `erase(path.begin())` drops the
*start* cell — the enemy is already standing there, so the first waypoint it should walk toward is the
*second* cell on the route. What comes back is a clean list of "go here, then here, then here"
world points ending at the (snapped) goal.

> **Why forbid corner-cutting at all — wouldn't a diagonal past a single blocked cell be a shorter,
> fine-looking path?** Geometrically the diagonal line clips the corner of the blocked cell, and our
> obstacles are *already* inflated by clearance precisely so the enemy's body doesn't intersect them.
> Allow the cut and the grunt shaves that corner, its half-metre-wide body catches the wall, and the
> kinematic sweep stalls it — the exact failure this whole chapter exists to prevent, reintroduced by
> a "harmless" shortcut. The two-cell flank test costs almost nothing and guarantees every diagonal in
> the returned path has open ground on both sides, so the route is walkable by a body with width, not
> just by an idealised point.

---

## Step 5: The `AIPath` Component

The pathfinder is a pure function — hand it two points, get waypoints. But an enemy following a path
needs to *remember* where it is along that route between ticks: which waypoints remain, which one it's
currently walking toward, and when to recompute. That's per-enemy state, so it's a component. Add it
to `src/engine/ecs/components/gameplay.h`, just after `AIState`:

```cpp
// Enemy navigation path: the waypoints (grid-cell centres) the grunt is walking
// toward its target, plus the follow cursor and a repath timer. Filled by
// aiSystem via A* over the NavGrid.
struct AIPath
{
	std::vector<glm::vec3> waypoints;
	size_t index = 0;           // current waypoint being walked toward
	float  repathTimer = 0.0f;  // counts down; a new path is computed at ≤ 0
};
```

Three fields, one job each:

- **`waypoints`** — the list `findPath` returned, the cells to walk in order.
- **`index`** — the *follow cursor*: which waypoint the grunt is currently heading for. It advances as
  the grunt reaches each one, so the grunt doesn't re-target waypoints it's already passed.
- **`repathTimer`** — counts down each tick; when it hits zero the path is stale and gets recomputed
  (the player has moved since we last searched).

Give every grunt one in the archetype. In `src/engine/level/spawn_monster.cpp`, right after the
`AIState` line:

```cpp
        reg.emplace<AIState>(e);
        reg.emplace<AIPath>(e);
```

`AIState` (Chapter 22) marks the entity as an enemy and holds its behaviour state; `AIPath` is the
companion that holds its *route*. Every grunt now has both, and the Chase state in the next step
requires both.

> **Why split the route into its own `AIPath` component instead of adding the waypoint vector to
> `AIState`?** They have different lifetimes and different owners. `AIState` is the small, cheap,
> always-relevant "what is this enemy doing" — `state`, `target`, `attackCooldown` — that plenty of
> systems read (rendering the state, the death system, the aggro logic). `AIPath` is a heavier,
> allocation-owning `std::vector` that *only* the chase logic ever touches. Keeping them separate means
> the many readers of `AIState` don't drag a `std::vector` around, the pathfinding state is cleanly
> scoped to the one system that manages it, and a future non-pathfinding enemy (a turret, a flyer)
> could have `AIState` without `AIPath`. It's the same "one component, one concern" discipline the rest
> of the ECS follows.

---

## Step 6: Chase Follows the Path

Now the payoff — rewiring `aiSystem`'s Chase state (from Chapter 23) to *follow the path* instead of
steering blindly at the player. This is all in `src/engine/ecs/systems/enemy/ai_system.cpp`. First,
the new includes and pacing constants at the top:

```cpp
#include "engine/ai/find_path.h"
#include "engine/ai/types/nav_grid.h"
```

```cpp
    constexpr float kRepathPeriod = 0.4f;   // seconds between A* recomputes
    constexpr float kWaypointHit  = 0.5f;   // distance at which a waypoint is "reached"
    constexpr int   kRepathBudget = 4;      // A* recomputes allowed per tick (stagger enemies)
```

At the top of `aiSystem`, fetch the grid from the context and seed the per-tick repath budget:

```cpp
void aiSystem(entt::registry& registry, const Level& level)
{
    const float dt = registry.ctx().get<PhysicsConfig>().fixedDeltaTime;
    auto& bodyInterface = registry.ctx().get<JoltWorld>().getBodyInterface();
    const NavGrid* nav = registry.ctx().find<NavGrid>();
```

```cpp
    int repathBudget = kRepathBudget;

    for (auto [entity, ai, pos, body, path] : registry.view<AIState, Position, JoltBody, AIPath>().each())
    {
```

Note `nav` uses `find` (pointer, possibly null) and the enemy view now includes `AIPath`, so only
grunts that have a path component are driven. `repathBudget` is declared *outside* the loop — it's a
shared allowance across all enemies this tick, which we'll get to.

The aggro, idle, and attack branches are unchanged from Chapter 23 — sense the player via
line-of-sight, acquire within detect range, drop when they escape pursue range, halt-and-hit in attack
range — with one addition: whenever the grunt *isn't* chasing (idle, or attacking) it clears its
waypoints, so a stale route can't linger:

```cpp
        if (ai.target == entt::null)
        {
            ai.state = AIStateKind::Idle;
            path.waypoints.clear();
            haltInPlace();
            continue;
        }
```

```cpp
        // ─── Attack: in range with a clear shot ──────────────────
        if (los && dist <= kAttackRange)
        {
            ai.state = AIStateKind::Attack;
            path.waypoints.clear();
            faceDir(registry, entity, toPlayer);
            haltInPlace();
            // ... apply melee damage on the cooldown ...
            continue;
        }
```

The new heart of the system is the Chase branch:

```cpp
        // ─── Chase: path toward the player, routing around obstacles ─
        ai.state = AIStateKind::Chase;
        path.repathTimer -= dt;
        bool atEnd = path.index >= path.waypoints.size();
        if (nav && repathBudget > 0 && (path.waypoints.empty() || atEnd || path.repathTimer <= 0.0f))
        {
            path.waypoints = findPath(*nav, pos.value, playerPos);
            path.index = 0;
            path.repathTimer = kRepathPeriod;
            --repathBudget;
        }
```

Every chasing tick, count the repath timer down. Then decide whether to **recompute** the path. We
repath when any of three things is true: there's no path yet (`waypoints.empty()`), we've walked off
the end of the one we had (`atEnd`), or the timer expired (`repathTimer <= 0`, the player's moved and
the route is stale). But two gates guard the recompute: `nav` must be non-null (there *is* a grid),
and `repathBudget > 0`. When we do repath, we call `findPath` from the grunt's live position to the
player's live position, reset the cursor to the first waypoint, restart the timer, and spend one unit
of budget.

```cpp
        // Advance the follow cursor, then steer to the current waypoint (or, with
        // no path, straight at the player).
        glm::vec3 stepDir = toPlayer;
        if (path.index < path.waypoints.size())
        {
            glm::vec3 toWp = path.waypoints[path.index] - pos.value; toWp.y = 0.0f;
            if (glm::length(toWp) < kWaypointHit) ++path.index;
            if (path.index < path.waypoints.size())
            {
                toWp = path.waypoints[path.index] - pos.value; toWp.y = 0.0f;
                if (glm::length(toWp) > 0.001f) stepDir = glm::normalize(toWp);
            }
        }

        glm::vec3 target = pos.value + stepDir * kMoveSpeed * dt;
        bodyInterface.MoveKinematic(body.id,
            JPH::RVec3(target.x, target.y, target.z), JPH::Quat::sIdentity(), dt);
        faceDir(registry, entity, stepDir);
```

Then follow it. `stepDir` starts as `toPlayer` — the straight-line direction, our *fallback*. If we
have a live waypoint, we override that: measure the flat vector to the current waypoint, and if we're
within `kWaypointHit` (0.5 m) of it, **advance the cursor** (`++path.index`) — that waypoint is
reached. Re-read the (possibly new) current waypoint and, if it's a real distance away, steer toward
*it* instead. Finally, move the kinematic body one `kMoveSpeed * dt` step along `stepDir` with
`MoveKinematic` (exactly as Chapter 23 did — only the *direction* is smarter now) and turn the model
to face where it's going.

The fallback is the safety net that makes this robust. If `findPath` returned empty — start and goal
in the same cell, the player unreachable, the node budget blown, or no grid at all — `stepDir` stays
`toPlayer` and the grunt behaves exactly like the old straight-line chase. Pathfinding *improves* the
chase when it can and never *breaks* it when it can't.

> **Why cap A* recomputes with a per-tick `repathBudget` shared across all enemies, on top of the
> per-enemy 0.4 s timer?** The timer staggers *one* enemy's searches over time, but it doesn't stop a
> whole *room* of enemies from all deciding to repath on the same tick — if ten grunts spawned
> together, their timers expire together, and ten A* searches in one frame is a spike. The budget
> (`kRepathBudget = 4`) caps how many enemies may recompute *this* tick regardless of their timers;
> the ones that don't get budget simply keep following their existing path and try again next tick
> (their timer stays expired, so they're first in line). It smears the cost across frames and turns a
> synchronised spike into a steady trickle. The two throttles are complementary: the timer controls
> *how often* a given enemy repaths, the budget controls *how many* repath at once.

> **Why does the enemy path to the player's *live* position every time, rather than to a "last known
> position" it remembers from when it last had line-of-sight?** This is a deliberate v1 shortcut. A
> more sophisticated AI separates *seeing* the player (which needs LoS) from *chasing* to a remembered
> spot (which doesn't) — so the enemy runs to where it last saw you and gives up if you're not there,
> which reads as genuine "searching" behaviour. We collapse that: once a grunt is aggroed (it saw you
> within detect range) it keeps pathing to your *current* position until you break pursue range,
> effectively tracking you through walls. It's a simplification — the grunt is a little clairvoyant —
> but it keeps the state machine small and the pathing target trivial (`playerPos`, already in hand),
> and for a first pursuing enemy it plays fine. Last-known-position search is a clean future
> refinement, noted here so it's a conscious choice and not an oversight.

---

## Step 7: CMake and the `monster_path` Scenario

The two new pathfinding translation units join the `qengine_lib` target in `CMakeLists.txt`, next to
`simulation.cpp`:

```cmake
add_library(qengine_lib STATIC
	src/engine/app/simulation.cpp
	src/engine/ai/build_nav_grid.cpp
	src/engine/ai/find_path.cpp
```

`nav_grid.h`, `build_nav_grid.h`, and `find_path.h` are header-only and compile into their includers,
so they need no CMake entry. (If Chapter 23 hasn't already added `ai_system.cpp` to the target, it
goes in the systems block too — `src/engine/ecs/systems/enemy/ai_system.cpp`.)

The real proof is a new headless scenario. The showcase level has a **shelf** — a solid prop spanning
roughly `x[18,22] z[3,7]` — that Chapter 22's grunts stand near. `scenario_monster_path` in
`src/harness/headless_main.cpp` puts a grunt on one side of that shelf and the player on the other,
and asserts the grunt reaches the player **without ever entering the shelf's footprint** — i.e. it
routed *around*, not *through*:

```cpp
    // Pathfinding: an aggroed grunt with the shelf between it and the player must
    // route AROUND the shelf (never through it) and reach attack range.
    bool scenario_monster_path(entt::registry& reg, JoltWorld& jolt, const Level& level, float dt)
    {
        entt::entity player = findPlayer(reg);
        entt::entity grunt = entt::null; float bestZ = 1e9f;
        for (auto [e, ai, pos] : reg.view<AIState, Position>().each())
            if (pos.value.z < bestZ) { bestZ = pos.value.z; grunt = e; }
        if (grunt == entt::null) return report("monster_path", false, "no grunt");

        float halfY = reg.get<AABBCollider>(player).halfExtents.y;

        // Grunt just west of the shelf (shelf spans x[18,22] z[3,7]).
        teleportKinematic(jolt, reg, grunt, glm::vec3(16.0f, 0.95f, 5.0f));

        // Aggro it with an open line of sight to the north.
        teleportPlayer(reg, player, glm::vec3(16.0f, halfY + 0.05f, 10.0f));
        for (int i = 0; i < 40; i++) { applyInput(reg, player, Input{}); qengine::stepSimulation(reg, jolt, level, dt); }
        bool aggroed = reg.get<AIState>(grunt).target != entt::null;

        // Now hide the player on the far side of the shelf. The grunt is aggroed,
        // so it must path AROUND the shelf to reach the player.
        teleportPlayer(reg, player, glm::vec3(26.0f, halfY + 0.05f, 5.0f));

        bool enteredShelf = false;
        for (int i = 0; i < 600; i++)
        {
            applyInput(reg, player, Input{});
            qengine::stepSimulation(reg, jolt, level, dt);
            if (!reg.valid(grunt)) break;
            glm::vec3 gp = reg.get<Position>(grunt).value;
            if (gp.x > 18.0f && gp.x < 22.0f && gp.z > 3.0f && gp.z < 7.0f) enteredShelf = true;
        }

        float finalDist = reg.valid(grunt)
            ? glm::length(reg.get<Position>(grunt).value - reg.get<Position>(player).value) : 1e9f;
        bool reached = finalDist < 3.5f;

        char buf[220];
        std::snprintf(buf, sizeof(buf),
            "aggroed=%d; routed around shelf (no-clip=%d); final dist=%.1f reached=%d",
            aggroed ? 1 : 0, enteredShelf ? 0 : 1, finalDist, reached ? 1 : 0);
        return report("monster_path", aggroed && !enteredShelf && reached, buf);
    }
```

The scenario reads as a little story:

1. **Pick the front grunt.** Scan `AIState` entities for the lowest-`z` one — a stable way to grab a
   known grunt.
2. **Place it west of the shelf**, at `(16, 0.95, 5)`, hard-teleporting its *kinematic* body with a
   new helper (below), since a kinematic body's position is script-owned.
3. **Aggro it** by standing the player in clear sight to the north for 40 ticks; assert
   `ai.target != entt::null`.
4. **Hide the player on the far (east) side** of the shelf at `(26, ..., 5)`. Now the straight line
   from grunt to player runs *straight through the shelf* — the exact situation Step 1 described.
5. **Run 600 ticks** and watch. Every tick, check whether the grunt's position ever falls inside the
   shelf's XZ box `x∈(18,22) z∈(3,7)`. If it does, `enteredShelf` latches true — the grunt clipped
   through, and the test fails.
6. **Assert it arrived.** `finalDist < 3.5` means the grunt got within attack range of the player.

The scenario passes only on `aggroed && !enteredShelf && reached` — the grunt woke up, went *around*
the shelf (never once inside it), and still closed the distance. That's pathfinding proven end to end,
headless, with no window.

It leans on a small new harness helper, `teleportKinematic`, since the existing `teleportPlayer` only
moves a `CharacterVirtual`:

```cpp
    // Hard-teleport a kinematic body (enemy) + its ECS mirror.
    void teleportKinematic(JoltWorld& jolt, entt::registry& reg, entt::entity e, glm::vec3 p)
    {
        jolt.getBodyInterface().SetPosition(reg.get<JoltBody>(e).id,
            JPH::RVec3(p.x, p.y, p.z), JPH::EActivation::Activate);
        reg.get<Position>(e).value = p;
    }
```

It sets both the Jolt body's position *and* the ECS `Position` mirror so the two don't disagree on the
first tick. Register the scenario in `main`'s dispatch:

```cpp
    else if (scenario == "monster_grunt")    pass = scenario_monster_grunt(registry, jolt, level, dt);
    else if (scenario == "monster_ai")       pass = scenario_monster_ai(registry, jolt, level, dt);
    else if (scenario == "monster_path")     pass = scenario_monster_path(registry, jolt, level, dt);
```

(`monster_ai` is Chapter 23's behaviour scenario; `monster_path` is ours.)

> **Why assert the negative — "never entered the shelf" — rather than just "reached the player"?**
> Because "reached the player" alone doesn't prove the *pathfinding*. A grunt with the old straight-line
> chase could, on some maps, still end up near the player by grinding along a wall until it slips past.
> The whole point of this chapter is *how* it gets there: around obstacles, not through them. Latching
> `enteredShelf` on any tick the grunt's centre is inside the obstacle turns "routed correctly" into a
> hard, observable invariant. If a future refactor breaks corner-cutting, or the clearance padding, or
> the grid build, the grunt will start clipping the shelf and this test fails immediately — even if it
> still, by luck, ends up close to the player.

---

## Step 8: A Moving Enemy Exposed a Movement Bug

Chapter 22's grunt stood still; this chapter's grunt *walks* — and a moving kinematic body that walks
into the player can *carry* them, the way a moving platform does. That shove turned out to be enough to
trip a latent run-away bug in the player controller's platform-carry code, where the player's own
horizontal speed could compound instead of settle. The diagnosis and fix — the reference-frame rewrite
of `playerCharacterSystem`, the new `CharacterPhysics.maxHorizontalSpeed` cap, and the `speed_cap`
regression scenario (plus a `clearEnemies` harness helper that keeps wandering AI out of pure
player-physics measurements) — are a story in their own right, so they live in **Chapter 25:
Debugging a Speed Run-Away — Reference Frames**. All we need to know here is that giving the grunt
legs is what surfaced the bug.

---

## What Changed — Summary

| File | Change |
|------|--------|
| `engine/ai/types/nav_grid.h` | **New file** — the `NavGrid` walkability-grid struct + cell↔world helpers (`cellOf`, `center`, `isBlocked`). |
| `engine/ai/build_nav_grid.{h,cpp}` | **New files** — `buildNavGrid`: derive XZ bounds from surfaces, block tall (wall) surfaces + solid props inflated by clearance, skip triggers/player/enemies/demo cubes. |
| `engine/ai/find_path.{h,cpp}` | **New files** — `findPath`: 8-connected A*, octile heuristic, no corner-cutting, endpoint snapping, node budget; returns waypoint centres. |
| `engine/ecs/components/gameplay.h` | **New** `AIPath` component (waypoints + follow cursor + repath timer). |
| `engine/level/spawn_monster.cpp` | Grunt archetype also emplaces `AIPath`. |
| `engine/app/simulation.cpp` | `buildWorld` builds the `NavGrid` from the populated scene and stores it in the registry context. |
| `engine/ecs/systems/enemy/ai_system.cpp` | Chase state now fetches the grid, repaths on a timer/at path-end (throttled by a per-tick budget), advances the waypoint cursor, and steers to the current waypoint (falling back to straight-at-player). |
| `CMakeLists.txt` | Add `ai/build_nav_grid.cpp` and `ai/find_path.cpp` (and `enemy/ai_system.cpp` if not already present). |
| `harness/headless_main.cpp` | **New** `scenario_monster_path` (routes around the shelf, no clip) + `teleportKinematic` helper. |

> The player-controller movement fix that shipped alongside this work — `player_character_system.cpp`,
> `CharacterPhysics.maxHorizontalSpeed`, and the `speed_cap` / `clearEnemies` harness additions — is a
> self-contained debugging story rather than part of pathfinding, so it's documented in **Chapter 25**,
> not counted here.

---

## What You Should See

Run `build/QEngine.exe`:

1. **Grunts route around cover.** Aggro a grunt from across a pillar or the shelf, then step behind the
   obstacle. Instead of grinding its face into the wall (as a straight-line chase would), the grunt
   walks *around* the corner and comes at you from the open side.
2. **The chase still works in the open.** With clear ground between you, the grunt beelines straight at
   you exactly as before — pathfinding only changes its behaviour when something's in the way.
3. **It keeps tracking you as you move.** Because it repaths every 0.4 s to your live position, backing
   away or circling makes the grunt continually re-plan and follow — no getting stuck on a stale route.
4. **The headless harness passes `monster_path`** (and Chapter 23's `monster_ai`) — silently, no window
   — asserting the grunt routed around the shelf without clipping and still closed the distance to
   attack range.

---

## What's Next

The grunt can now think its way around the arena: it sees you, wakes up, plans a route around cover,
and follows it to your throat. That completes the enemy loop — physical (Ch. 22), behavioural
(Ch. 23), and navigational (this chapter). The one loose end it directly created is a physics bug — a
*moving* grunt can shove the player fast enough to trip a speed run-away in the platform-carry code —
which the next chapter (Chapter 25) chases down and fixes. Beyond that, the seams left open here are all
clearly scoped: the nav grid is built *once* and never rebuilt, so a door opening doesn't reopen a
route (rebuild-on-level-change, or on a mover state flip, is the natural follow-up); the pursue logic is
clairvoyant, pathing to your live position rather than a last-known spot it has to search; the enemy
still slides its kinematic box along waypoints rather than *walking* with a `CharacterVirtual` (which
would let it handle stairs, slopes, and step-ups the grid can't express); and a grid will always be
blockier than a polygon navmesh. Any of those is a worthy later chapter — but with a shootable, feeling,
thinking, path-finding enemy in the arena, the engine finally has a real fight in it.
