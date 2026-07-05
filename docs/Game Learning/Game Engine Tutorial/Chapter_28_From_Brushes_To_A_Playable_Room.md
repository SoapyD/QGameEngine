# Chapter 28: From Brushes to a Playable Room — Transform, AABB Geometry, and Scene Assembly

## What You'll Learn
- The **one-place-only coordinate transform** (`map_transform.h`): the Z-up(Quake)→Y-up(engine) axis
  swap and the `kMapUnitsPerEngineUnit = 32` scale, and why *every* value that crosses from map space
  to engine space goes through exactly these functions
- The **AABB box-fidelity MVP decision**: representing each brush by its axis-aligned bounding box
  rather than solving general plane∩plane∩plane geometry — why that's *lossless* for `smoke.map`, and
  what it defers
- Turning a brush into **six axis-aligned quad `Surface`s** with the correct **CCW-from-outside**
  winding for back-face culling, and picking a brush's texture by majority vote
- Why the loader emits **`Surface`s, not meshes** — so collision and the nav grid consume them
  directly — and builds GL meshes separately, only when there's a context
- **Per-texture render meshes** (`build_textured_meshes`) so a multi-texture room renders correctly
  through the single-texture draw path, and the **`renderMeshes` VAO-lifetime ownership** on `Level`
- Reusing the **existing `SpawnParams`/`spawnScene` dispatch** (Chapter 18) for map entities via
  `map_to_descriptors`, converting spatial props into engine space on the way
- Assembling the whole scene from a `.map` in `scene_setup_map`, and **wiring `buildWorld` + `main.cpp`**
  so a map path loads your room — with the hard-coded showcase kept as a fallback
- The **`map_scene`** headless scenario that pins the whole conversion end to end

---

## Where We Are

Chapter 26 gave us an editor and a hand-drawn `smoke.map`; Chapter 27 gave us a parser that reads it
into a faithful `MapData` tree of entities, brushes, and faces — in raw Quake units, Z-up, unconverted.
This chapter turns that tree into a **room you can walk around in**. That means four distinct jobs, and
we build them in the order the data flows:

1. **The transform** — the single boundary where map-space numbers become engine-space numbers.
2. **Brush → geometry** — worldspawn brushes become `Surface`s (the MVP: each brush as its AABB).
3. **Geometry → render meshes** — surfaces grouped by texture into GL meshes the `Level` owns.
4. **Entities → the scene** — every non-world entity mapped onto the existing factory dispatch, and the
   whole thing assembled in `scene_setup_map`, then wired into `buildWorld` and `main.cpp`.

By the end, `QEngine.exe assets/maps/smoke.map` drops you into your authored box room, lit, textured,
and collidable — and the hard-coded showcase is still there as a fallback. Everything below is grounded
in `src/engine/level/map_transform.h`, `map_to_level.{h,cpp}`, `build_textured_meshes.{h,cpp}`,
`map_to_descriptors.{h,cpp}`, `src/engine/level/level.h`, `src/engine/app/scene_setup_map.{h,cpp}`,
`simulation.cpp`, and `main.cpp`.

---

## Step 1: The Transform — One Boundary, Crossed Once

Chapter 27's IR deliberately stored raw map-space values so that conversion could happen in exactly one
place. This is that place. TrenchBroom and QEngine disagree about two fundamental things — which axis is
"up," and how big a unit is — and every value that moves between the two worlds must be reconciled the
same way. Create `src/engine/level/map_transform.h`:

```cpp
#pragma once
// Coordinate/scale conversion between TrenchBroom `.map` space and engine space,
// kept in ONE place (per the note in types/map_data.h). Every brush vertex,
// entity origin, and vector-valued property crosses this boundary exactly once.
//
//   TrenchBroom / Quake : X right, Y forward, Z up   (Z-up), integer Quake units
//   QEngine             : X right, Y up,      Z back  (Y-up), small float units
//
// Axis map preserves right-handedness: engine = { m.x, m.z, -m.y } / scale.

#include <glm/glm.hpp>

#include <cmath>

namespace qmap
{
    // One engine unit spans this many Quake/TB units. The showcase room is ~30
    // engine units; smoke.map's ~256-unit room becomes ~8 engine units at 32.
    // Single knob — bump down (e.g. 16) if maps feel cramped for the player scale.
    inline constexpr float kMapUnitsPerEngineUnit = 32.0f;

    // A world POSITION: axis-swap + scale.
    inline glm::vec3 mapPointToEngine(glm::vec3 m)
    {
        return glm::vec3(m.x, m.z, -m.y) / kMapUnitsPerEngineUnit;
    }

    // A DIRECTION (sun vector, etc.): axis-swap only, no scale; caller normalises.
    inline glm::vec3 mapDirToEngine(glm::vec3 d)
    {
        return glm::vec3(d.x, d.z, -d.y);
    }

    // Full EXTENTS (a size, not a point): axis-swap the magnitudes, scale, keep
    // positive. Used for brush-entity collider sizes.
    inline glm::vec3 mapExtentToEngine(glm::vec3 e)
    {
        return glm::abs(glm::vec3(e.x, e.z, e.y)) / kMapUnitsPerEngineUnit;
    }

    // Quake yaw (degrees about Z-up, 0=+X, 90=+Y) → engine yaw about Y-up.
    // NOTE: the current spawnPlayer ignores angle, so this only matters once a
    // factory consumes facing; provided for completeness.
    inline float mapAngleToEngineYaw(float degrees)
    {
        return -degrees;
    }
}
```

The heart of it is the axis map, `{ m.x, m.z, -m.y }`: engine X is map X (both "right"), engine Y (up)
is map Z (up), and engine Z (back) is *negated* map Y (forward). The negation is not cosmetic — it's what
keeps the coordinate system **right-handed** through the swap. Get it wrong and the whole level comes in
mirror-imaged: doors open the wrong way, the sun lights the wrong wall, and text on surfaces reads
backwards.

There are four functions because different *kinds* of value convert differently:

- **`mapPointToEngine`** — a world position: swap axes *and* divide by scale. Vertices and origins.
- **`mapDirToEngine`** — a direction: swap axes only, *no* scale (a direction has no length that should
  shrink); the caller normalises.
- **`mapExtentToEngine`** — a size: swap the *magnitudes* (no negation), scale, keep positive — a size
  is never negative.
- **`mapAngleToEngineYaw`** — a facing angle: Quake yaw about Z becomes `-degrees` about Y.

`kMapUnitsPerEngineUnit = 32` is the single scale knob. `smoke.map`'s room is about 256 Quake units
across, which becomes 8 engine units — the right ballpark next to the ~30-unit showcase, and tuned so
the player (whose step height and bunny-hop speed were dialled in at engine scale over the last dozen
chapters) feels right walking it.

> **Why funnel every conversion through four small inline functions instead of just multiplying by a
> scale wherever a coordinate is used?** Because "which space is this number in?" is the single easiest
> thing to get wrong in a loader, and the cost of getting it wrong is a level that looks *almost* right —
> mirrored, or half-scale, or with the sun on the wrong side — with no error to catch it. Centralising
> the axis swap and scale in one header means there is exactly one definition of what the conversion
> *is*, and every brush vertex, entity origin, `endpos`, `velocity`, and sun `direction` calls the same
> function. Change the scale from 32 to 16 and the *entire* pipeline rescales consistently, because
> there's one knob. Split the conversion across call sites and you guarantee that someday someone adds a
> new spatial property, forgets to convert it, and ships a level with one entity in the wrong place. The
> IR stored raw values (Chapter 27) precisely so that this boundary could be the *only* boundary — this
> header is the other half of that decision.

---

## Step 2: Brushes → Surfaces, at AABB Fidelity

Now the geometry. A worldspawn brush is a convex solid defined by its face planes; the "textbook" way to
turn it into renderable geometry is to intersect every triple of planes (Cramer's rule), clip the
candidate points against all the other planes (Sutherland–Hodgman), and fan-triangulate the resulting
polygons. That's real work, and it was the *planned* approach. What actually shipped is deliberately
simpler: **each brush is represented by its axis-aligned bounding box (AABB).** The header says so
plainly — `src/engine/level/map_to_level.h`:

```cpp
#pragma once

#include "engine/level/level.h"
#include "engine/level/types/map_data.h"

// Build a Level (one sector of axis-aligned quad Surfaces) from a parsed .map's
// worldspawn brushes at MVP fidelity: each brush is represented by its
// axis-aligned bounding box in engine space. Lossless for axis-aligned box
// brushes (angled brushes collapse to their AABB — general brush geometry is a
// documented follow-up). Coordinate/scale conversion goes through map_transform.h.
//
// Surfaces (not meshes) are produced here: they feed collision (createLevelBodies)
// and the nav grid directly, and drive the render-mesh build. GL meshes are built
// separately (build_textured_meshes), only when a GL context exists.
Level mapWorldspawnToLevel(const qmap::MapData& map);
```

The crucial claim is "**lossless for axis-aligned box brushes**." `smoke.map` is *entirely* axis-aligned
boxes — six of them, walls and floor and ceiling. For a box brush, the AABB *is* the brush: no
information is lost. An angled brush (a wedge, a ramp) would collapse to its enclosing box and lose its
slope — but the smoke map has none, so at this stage the representation is exact.

Create `src/engine/level/map_to_level.cpp`. The two helpers first. `majorityTexture` picks a single
texture for the whole box (since one AABB can only carry one texture, and each of its six faces might
name a different one):

```cpp
namespace
{
    // The texture used by most of a brush's faces. For single-texture box brushes
    // (the smoke.map case) this is exact; for mixed-texture brushes the AABB
    // representation can only carry one, so majority wins.
    std::string majorityTexture(const qmap::MapBrush& brush)
    {
        std::unordered_map<std::string, int> counts;
        for (const auto& f : brush.faces) counts[f.texture]++;

        std::string best;
        int bestN = -1;
        for (const auto& [tex, n] : counts)
            if (n > bestN) { bestN = n; best = tex; }
        return best;
    }
```

And `addBoxSurfaces` — the one that pushes a box's six faces as quad `Surface`s, each wound so its front
faces *outward*:

```cpp
    // Push the 6 axis-aligned faces of the box [mn,mx] into the sector, each wound
    // CCW as seen from outside (GL_CCW front-face + GL_BACK cull) with an outward
    // normal. The interior-facing face of a wall slab is what the player sees.
    void addBoxSurfaces(Sector& sector, glm::vec3 mn, glm::vec3 mx, const std::string& tex)
    {
        const float x0 = mn.x, y0 = mn.y, z0 = mn.z;
        const float x1 = mx.x, y1 = mx.y, z1 = mx.z;

        auto push = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, glm::vec3 n)
        {
            Surface s;
            s.vertices[0] = a; s.vertices[1] = b; s.vertices[2] = c; s.vertices[3] = d;
            s.normal = n;
            s.textureName = tex;
            sector.surfaces.push_back(s);
        };

        push({x1,y0,z1}, {x1,y0,z0}, {x1,y1,z0}, {x1,y1,z1}, { 1, 0, 0}); // +X
        push({x0,y0,z0}, {x0,y0,z1}, {x0,y1,z1}, {x0,y1,z0}, {-1, 0, 0}); // -X
        push({x0,y1,z1}, {x1,y1,z1}, {x1,y1,z0}, {x0,y1,z0}, { 0, 1, 0}); // +Y
        push({x0,y0,z0}, {x1,y0,z0}, {x1,y0,z1}, {x0,y0,z1}, { 0,-1, 0}); // -Y
        push({x0,y0,z1}, {x1,y0,z1}, {x1,y1,z1}, {x0,y1,z1}, { 0, 0, 1}); // +Z
        push({x1,y0,z0}, {x0,y0,z0}, {x0,y1,z0}, {x1,y1,z0}, { 0, 0,-1}); // -Z
    }
}
```

Six `push` calls, one per axis-aligned face (+X, −X, +Y, −Y, +Z, −Z), each giving four corners and the
outward normal. The corner *order* matters: each quad is wound **counter-clockwise as seen from
outside** the box. The renderer uses `GL_CCW` front-face with `GL_BACK` culling, so a face is only drawn
when you're looking at its outward side. For a room you stand *inside*, that means the wall slabs' outer
faces are culled (you never see them) and their inner faces — wait, no: the player is inside the *room*,
which is the hollow between the wall slabs. Each wall slab is a thin solid box; the face of it that
points *into* the room is the one wound to face you. Getting this winding right is what makes the room
render as a room instead of an inside-out box that vanishes when you look at it.

Now the driver. Sweep every worldspawn brush, compute its AABB *in engine space*, and emit its box
surfaces:

```cpp
Level mapWorldspawnToLevel(const qmap::MapData& map)
{
    Level level;

    Sector world;
    world.id = 0;

    glm::vec3 lvlMin(std::numeric_limits<float>::max());
    glm::vec3 lvlMax(std::numeric_limits<float>::lowest());

    for (const auto& entity : map.entities)
    {
        if (entity.classname() != "worldspawn") continue;

        for (const auto& brush : entity.brushes)
        {
            // Brush AABB in ENGINE space (convert each face point, then min/max —
            // the axis swap makes this cleaner than converting a map-space AABB).
            glm::vec3 mn(std::numeric_limits<float>::max());
            glm::vec3 mx(std::numeric_limits<float>::lowest());
            for (const auto& face : brush.faces)
                for (const auto& p : face.points)
                {
                    glm::vec3 e = qmap::mapPointToEngine(p);
                    mn = glm::min(mn, e);
                    mx = glm::max(mx, e);
                }

            if (mn.x > mx.x) continue;  // brush with no faces — skip

            addBoxSurfaces(world, mn, mx, majorityTexture(brush));
            lvlMin = glm::min(lvlMin, mn);
            lvlMax = glm::max(lvlMax, mx);
        }
    }

    world.boundsMin = world.surfaces.empty() ? glm::vec3(0.0f) : lvlMin;
    world.boundsMax = world.surfaces.empty() ? glm::vec3(0.0f) : lvlMax;

    level.sectors.push_back(std::move(world));
    return level;
}
```

Note the AABB is computed by converting **each face point to engine space first**, then taking min/max —
not by building a map-space box and converting that. Because the axis swap negates and reorders
components, converting the corners individually and re-min/maxing is the clean way to get a correct
engine-space box. Every worldspawn brush stamps six surfaces into a single sector (`id = 0`), the level
bounds accumulate across all brushes, and one sector's worth of `Surface`s comes back inside a `Level`.
For `smoke.map`: 6 brushes × 6 faces = **36 surfaces** (the number the headless test will assert).

Observe what this function does *not* produce: no GL mesh, no Jolt body, no entity. Just `Surface`s.

> **Why ship the AABB approximation instead of the general plane∩plane∩plane brush geometry that was
> originally planned?** Because for the map we actually have, the AABB is not an approximation at all —
> it's *exact*. `smoke.map` is entirely axis-aligned boxes, and the AABB of an axis-aligned box is the
> box. General brush geometry (Cramer intersection + Sutherland–Hodgman clipping + fan triangulation) is
> a meaningful chunk of code with its own edge cases, and writing it *before there's a single angled
> brush to test it against* would be building machinery on speculation. More importantly, the AABB
> representation slots into everything the engine already has: collision is `AABBCollider`-per-surface,
> the nav grid reads surface Y-extents, the render path draws quads — all of which the box surfaces feed
> directly, unchanged. So the MVP is lossless for today's content, reuses the entire existing pipeline,
> and defers the genuinely-hard geometry to the moment a level actually needs a ramp. The header comment
> names it a "documented follow-up," which is the honest way to defer work: build the exact-enough thing
> now, and leave a signpost for the general thing.

---

## Step 3: The `Level` Owns Its Render Meshes

Before we build meshes we need somewhere for them to *live*, and their lifetime is subtle enough that
`Level` grew a field for it. Look at `src/engine/level/level.h` — the new member at the bottom:

```cpp
struct Level
{
	std::vector<Sector> sectors;
	std::vector<Portal> portals;
	std::vector<LevelEntity> entities;

	// Extra render meshes owned by the level but not tied to a single sector's
	// `mesh` slot — used by the .map loader, which builds one mesh per texture
	// group. Kept alive here for the level's lifetime because ~Mesh frees the VAO
	// the MeshRenderer components reference.
	std::vector<std::unique_ptr<Mesh>> renderMeshes;
};
```

The comment is the whole story. The `.map` loader builds **one mesh per texture** (Step 4), and each
mesh owns an OpenGL VAO. The ECS `MeshRenderer` component that draws it only *references* that VAO by
id — it doesn't own the `Mesh`. If the `Mesh` were a local that went out of scope after scene setup, its
destructor (`~Mesh`) would free the VAO, and the `MeshRenderer` would be left pointing at a freed GL
object — a black hole, or a crash. So the meshes must outlive scene setup and live as long as the level
is drawn. `Level::renderMeshes` is that home: a vector of `unique_ptr<Mesh>` owned by the `Level`, which
lives for the whole time the level is on screen.

> **Why hang the render meshes off `Level` rather than off the entity that draws them, or a sector's
> existing `mesh` slot?** Two reasons. First, ownership vs reference: a `MeshRenderer` component is a
> lightweight handle (a VAO id and an index count), by design — it does not own GL resources, and making
> it own a `Mesh` would break the ECS's value-type components and its move/copy semantics. So *something
> else* has to own the `Mesh`, and it has to outlive the entity's setup. Second, the sector's single
> `mesh` slot holds *one* combined mesh, but the map loader produces *several* — one per texture group —
> which don't fit a single slot. `Level::renderMeshes` is the natural owner: it's the object whose
> lifetime already matches "how long this level is being played," and it can hold an arbitrary number of
> meshes. Tie the GL resource's lifetime to the thing whose lifetime you actually want, and the
> use-after-free simply can't happen.

---

## Step 4: Per-Texture Render Meshes

The room's surfaces don't all share a texture — `smoke.map` has `grid_orange` walls and `grid_grey`
floor/ceiling. But the single-sector render path binds **one** texture per draw. So we group surfaces by
texture name and build one mesh per group, each drawn with its own texture. Declare it in
`src/engine/level/build_textured_meshes.h`:

```cpp
#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

struct Sector;
class Mesh;

// Build one render Mesh per distinct texture name across a sector's surfaces.
// Mirrors build_sector_meshes' vertex/UV layout but groups by Surface::textureName,
// so a multi-texture map (e.g. orange walls + grey floor) renders correctly — the
// single-sector render path can only bind one texture. Needs a live GL context;
// the returned meshes must be kept alive for as long as they are drawn.
std::vector<std::pair<std::string, std::unique_ptr<Mesh>>>
buildTexturedMeshes(const Sector& sector);
```

It returns a list of `(textureName, mesh)` pairs — the name so the caller can resolve the right GL
texture id, the mesh so the caller can draw and own it. The implementation
(`src/engine/level/build_textured_meshes.cpp`) buckets surfaces into per-texture buffers and builds a
`Mesh` from each:

```cpp
std::vector<std::pair<std::string, std::unique_ptr<Mesh>>>
buildTexturedMeshes(const Sector& sector)
{
    struct Buffer
    {
        std::vector<Vertex>       vertices;
        std::vector<unsigned int> indices;
    };
    std::unordered_map<std::string, Buffer> groups;

    for (const auto& s : sector.surfaces)
    {
        Buffer& b = groups[s.textureName];
        unsigned int base = static_cast<unsigned int>(b.vertices.size());

        // Planar UVs from edge lengths (same as build_sector_meshes).
        float uScale = glm::length(s.vertices[1] - s.vertices[0]);
        float vScale = glm::length(s.vertices[3] - s.vertices[0]);

        b.vertices.push_back({s.vertices[0], s.normal, {0.0f,   0.0f}});
        b.vertices.push_back({s.vertices[1], s.normal, {uScale, 0.0f}});
        b.vertices.push_back({s.vertices[2], s.normal, {uScale, vScale}});
        b.vertices.push_back({s.vertices[3], s.normal, {0.0f,   vScale}});

        b.indices.push_back(base + 0);
        b.indices.push_back(base + 1);
        b.indices.push_back(base + 2);
        b.indices.push_back(base + 0);
        b.indices.push_back(base + 2);
        b.indices.push_back(base + 3);
    }

    std::vector<std::pair<std::string, std::unique_ptr<Mesh>>> out;
    out.reserve(groups.size());
    for (auto& [tex, b] : groups)
        if (!b.vertices.empty())
            out.emplace_back(tex, std::make_unique<Mesh>(b.vertices, b.indices));
    return out;
}
```

Each surface's four corners become four vertices with the surface normal and **planar UVs derived from
edge lengths** (`uScale`/`vScale` are the lengths of two edges, so the texture tiles at world scale
rather than being stretched to fit each quad — the same UV scheme the showcase's `build_sector_meshes`
uses). Two triangles (`0,1,2` and `0,2,3`) index the quad. Surfaces sharing a texture accumulate into
one `Buffer`, and each non-empty buffer becomes one `Mesh`. For `smoke.map` that's two meshes:
everything `grid_orange` in one, everything `grid_grey` in the other.

> **Why one mesh per texture rather than one mesh for the whole room, or one mesh per surface?** It's
> the balance the draw path forces. One mesh for the whole room can't work: a single draw call binds a
> single texture, so a room with two textures needs at least two draws, which means at least two meshes.
> One mesh *per surface* would work but is wasteful — 36 separate draw calls and 36 VAOs for a box room,
> when most of those surfaces share a texture and could be batched. Grouping by texture is the sweet
> spot: exactly as many meshes (and draw calls) as there are distinct textures, each a single batched
> VAO. It's the minimum number of draws the single-texture path allows, and it scales with texture
> variety rather than surface count — a hundred-surface room with three textures is still three meshes.

---

## Step 5: Map Entities → the Existing Factory Dispatch

Geometry done; now the *entities* — the spawn, the light, and (in a fuller map) doors, triggers,
pickups, monsters. Here we get to reuse everything: Chapter 18 built a `classname`→factory dispatch that
consumes `factories::SpawnParams`, and Chapter 26's FGD was written so map property keys match the
factory keys 1:1. So the entire job is *translating* `MapEntity` → `SpawnParams` and handing the list to
the existing `spawnScene`. Declare it in `src/engine/level/map_to_descriptors.h`:

```cpp
#pragma once

#include <vector>

#include "engine/level/types/map_data.h"
#include "engine/level/types/spawn_params.h"

// Translate a parsed .map's entities into the SpawnParams the existing entity
// factories consume. worldspawn is skipped (it becomes level geometry). Point
// entities read `origin`; brush entities (doors/lifts/triggers) derive origin +
// size from their brush AABB. Spatial props read back by factories (`endpos`,
// `velocity`, `direction`) are converted into engine space here; the result is
// fed straight to factories::spawnScene for classname dispatch + target linking.
std::vector<factories::SpawnParams> mapEntitiesToDescriptors(const qmap::MapData& map);
```

The implementation (`src/engine/level/map_to_descriptors.cpp`) has a helper for the brush-entity AABB
(door/lift/trigger origin and size come from their brush, exactly like worldspawn geometry) and a
`vec3ToStr` to write converted vectors back into the string `props` map:

```cpp
namespace
{
    std::string vec3ToStr(glm::vec3 v)
    {
        std::ostringstream ss;
        ss << v.x << ' ' << v.y << ' ' << v.z;
        return ss.str();
    }

    // Engine-space AABB over all of an entity's brush face points. Returns false
    // for a point entity (no brushes).
    bool entityBrushAABB(const qmap::MapEntity& e, glm::vec3& mn, glm::vec3& mx)
    {
        mn = glm::vec3(std::numeric_limits<float>::max());
        mx = glm::vec3(std::numeric_limits<float>::lowest());
        bool any = false;
        for (const auto& b : e.brushes)
            for (const auto& f : b.faces)
                for (const auto& p : f.points)
                {
                    glm::vec3 v = qmap::mapPointToEngine(p);
                    mn = glm::min(mn, v);
                    mx = glm::max(mx, v);
                    any = true;
                }
        return any;
    }
}
```

The main loop skips worldspawn (it's geometry, handled in Step 2), fills a `SpawnParams` from each
remaining entity, and — the important part — converts every *spatial* value into engine space:

```cpp
std::vector<factories::SpawnParams> mapEntitiesToDescriptors(const qmap::MapData& map)
{
    std::vector<factories::SpawnParams> out;

    for (const auto& e : map.entities)
    {
        const std::string cls = e.classname();
        if (cls.empty() || cls == "worldspawn") continue;  // geometry, not an entity

        factories::SpawnParams p;
        p.classname  = cls;
        p.targetname = e.getString("targetname");
        p.target     = e.getString("target");
        p.props      = e.props;   // spatial keys overwritten (in engine space) below

        // Origin + size. Brush entities derive both from their AABB; point
        // entities read the `origin` key (size keeps its default extent).
        glm::vec3 mn, mx;
        if (entityBrushAABB(e, mn, mx))
        {
            p.origin = (mn + mx) * 0.5f;
            p.size   = mx - mn;
        }
        else if (e.has("origin"))
        {
            std::istringstream ss(e.getString("origin"));
            glm::vec3 m(0.0f);
            if (ss >> m.x >> m.y >> m.z)
                p.origin = qmap::mapPointToEngine(m);
        }

        // Vector-valued props factories read back by name must cross into engine
        // space here (props are otherwise raw map-space strings).
        auto convertPoint = [&](const char* key)
        {
            if (!e.has(key)) return;
            std::istringstream ss(e.getString(key));
            glm::vec3 m(0.0f);
            if (ss >> m.x >> m.y >> m.z)
                p.props[key] = vec3ToStr(qmap::mapPointToEngine(m));
        };
        auto convertDir = [&](const char* key)
        {
            if (!e.has(key)) return;
            std::istringstream ss(e.getString(key));
            glm::vec3 m(0.0f);
            if (ss >> m.x >> m.y >> m.z)
                p.props[key] = vec3ToStr(qmap::mapDirToEngine(m));
        };
        convertPoint("endpos");    // func_door/func_plat travel target (world pos)
        convertPoint("velocity");  // prop_dynamic initial velocity (linear)
        convertDir("direction");   // light_environment sun vector

        out.push_back(std::move(p));
    }

    return out;
}
```

Walk the logic:

- **classname/targetname/target/props** copy straight across — the FGD keys already match. The raw
  `props` are copied wholesale, then the spatial ones are overwritten below.
- **origin and size** come from the brush AABB for brush entities (a door's position and extents *are*
  its brush), or from the `origin` key for point entities (converted with `mapPointToEngine`).
- **The spatial props** — `endpos` (a mover's travel target, a *position*), `velocity` (a *position*-
  like vector), and `direction` (the sun vector, a *direction*) — are the ones a factory reads back out
  of `props` by name later. They start as raw map-space strings, so they must be re-parsed, converted
  through the right transform (`mapPointToEngine` for positions, `mapDirToEngine` for the direction),
  and written back as engine-space strings. Miss one of these and a door would try to slide to a
  Quake-space coordinate 32× too far away, in the wrong axis.

The output is a `std::vector<SpawnParams>` — exactly what the showcase already produces and exactly what
`spawnScene` already consumes.

> **Why re-parse and re-serialise the spatial props as strings instead of storing them as typed
> vectors?** Because `SpawnParams` is deliberately string-keyed — it's the shape a TrenchBroom entity
> *is* (a bag of `"key" "value"` text), and the factories were built (Chapter 18) to parse their own
> typed values out of that bag on demand via `getFloat`/`getVec3`. That string-bag design is what lets
> the *same* factory serve both the in-code showcase descriptor and the map loader without knowing which
> it came from. So the loader plays by those rules: it converts a spatial value into engine space and
> writes it *back as a string*, so that when the factory later does `getVec3("endpos")` it reads an
> already-engine-space number and needs no knowledge of map space at all. The conversion happens once,
> here, at the boundary; the factory downstream stays blissfully unaware that TrenchBroom or Quake units
> ever existed. It's the Step-1 principle again — cross the boundary once — applied to properties as
> well as vertices.

---

## Step 6: Assembling the Scene — `scene_setup_map`

Now we stitch the pieces into a working scene, mirroring the hard-coded `setupScene` but sourcing
everything from a `.map`. Declare it in `src/engine/app/scene_setup_map.h`:

```cpp
// Build the scene from a TrenchBroom `.map` file instead of the hard-coded
// showcase: worldspawn brushes → level geometry (+ per-texture render meshes),
// every other entity → SpawnParams → the same two-pass factories::spawnScene the
// showcase uses. Returns the Level (which owns its render meshes) so buildWorld's
// collision + nav grid work unchanged. Mirrors setupScene(); on a load/parse
// error the returned level is empty and the error is logged.
Level setupSceneFromMap(entt::registry& registry, const ResourceManager& resources,
                        const std::string& mapPath, bool headless);
```

The implementation (`src/engine/app/scene_setup_map.cpp`) runs the whole pipeline in order — parse,
geometry, render meshes, entities, combat resources. First, fetch the shared assets and parse:

```cpp
    auto litShader  = resources.getShader("lit");
    auto gridOrange = resources.getTexture("grid_orange");
    auto gridRed    = resources.getTexture("grid_red");
    auto cubeMesh   = resources.getMesh("cube");

    // ─── Parse the .map and build level geometry (worldspawn brushes) ──
    std::string err;
    qmap::MapData map = qmap::loadMapFile(mapPath, &err);
    if (!err.empty())
        std::cerr << "[setupSceneFromMap] " << mapPath << ": " << err << "\n";

    Level level = mapWorldspawnToLevel(map);
```

If the parse fails, the error is logged and `mapWorldspawnToLevel` runs on an empty `MapData`, yielding
an empty level — the "on error, empty level" contract from the header. Next, the render meshes, built
**only when there's a GL context** (`!headless`) and stashed into the level's `renderMeshes` for
lifetime safety (Step 3):

```cpp
    // ─── Render entities: one per texture group (needs a GL context) ──
    // The level owns each Mesh (renderMeshes); the MeshRenderer only references
    // its VAO, which ~Mesh would free — so the Mesh must outlive the entity.
    if (!headless && !level.sectors.empty())
    {
        for (auto& [texName, mesh] : buildTexturedMeshes(level.sectors[0]))
        {
            unsigned int texId = resources.getTexture(texName)->getId();

            auto sectorEntity = registry.create();
            registry.emplace<Position>(sectorEntity, glm::vec3(0.0f));
            registry.emplace<MeshRenderer>
            (
                sectorEntity,
                mesh->getVAO(), 0u,
                litShader->getId(), texId,
                true, mesh->getIndexCount()
            );

            level.renderMeshes.push_back(std::move(mesh));
        }
    }
```

For each `(texName, mesh)` pair `buildTexturedMeshes` returned, it resolves the texture name to a GL id,
creates a render entity with a `MeshRenderer` pointing at the mesh's VAO and that texture, and then
`std::move`s the mesh into `level.renderMeshes` so it outlives the entity. This is the exact chain the
Step-3 comment warned about, wired correctly: mesh built → VAO referenced by the component → mesh ownership
parked on the `Level`. Next, the scene entities via the shared dispatch:

```cpp
    // ─── Scene entities: parsed .map entities → SpawnParams → dispatch ──
    // Same SpawnContext + two-pass spawnScene the showcase uses.
    factories::SpawnContext ctx;
    ctx.assets = factories::MeshAssets{ cubeMesh->getVAO(), cubeMesh->getIndexCount(),
                                        litShader->getId() };
    for (int i = 0; i < 7; ++i)
    {
        auto gun = resources.getMesh("gun_" + std::to_string(i));
        ctx.assets.gunVAO[i] = gun->getVAO();
        ctx.assets.gunIndexCount[i] = gun->getIndexCount();
    }
    ctx.texture = [&resources](std::string_view name)
    {
        return resources.getTexture(std::string(name))->getId();
    };
    factories::spawnScene(registry, ctx, mapEntitiesToDescriptors(map));
```

This is the payoff for all the reuse: the `SpawnContext` (shared cube/gun mesh handles and a texture
resolver) is assembled exactly as the showcase does, and the single line
`spawnScene(registry, ctx, mapEntitiesToDescriptors(map))` runs the *same* two-pass classname dispatch
and target-linking that Chapter 18 built — fed by our map descriptors instead of the in-code ones. The
loader added *no* new spawning code; it converts data into the shape the existing dispatch eats.
Finally the combat resources (registry context), identical to `setupScene`:

```cpp
    // ─── Combat resources (registry context) ────────────────────
    auto& combatRes = registry.ctx().emplace<CombatResources>();
    combatRes.cubeVAO = cubeMesh->getVAO();
    combatRes.cubeIndexCount = cubeMesh->getIndexCount();
    combatRes.shaderId = litShader->getId();
    combatRes.projectileTextureId = gridRed->getId();
    combatRes.tracerTextureId = gridOrange->getId();

    return level;
```

The completed `Level` (owning its render meshes) comes back, and — crucially — it's a plain `Level`,
indistinguishable to the rest of the engine from the showcase's. That's what lets the next step wire it
in with almost no change.

> **Why does `setupSceneFromMap` deliberately mirror `setupScene` so closely, rather than sharing more
> code with it?** Because the two build the *same shape of scene* from *different sources*, and the value
> is in the output being interchangeable, not in the input being shared. Everything downstream of scene
> setup — collision, the nav grid, the render loop, every gameplay system — must not care whether the
> level came from a `.map` or from `showcase_descriptor.cpp`. The way to guarantee that is to have both
> setup paths produce an identical `Level` + populated-registry + `CombatResources`, which means both
> paths do the same *ending* work (spawnScene, combat resources) even though their *beginning* work
> (hand-written descriptors vs parsed map) differs. Forcing them to share a common helper would couple
> two things whose only real commonality is their output contract; mirroring keeps each readable as a
> straight-line "here's how this source becomes a scene," and the shared `spawnScene`/`SpawnParams` layer
> already captures the part that genuinely *is* common.

---

## Step 7: Wiring `buildWorld` and `main.cpp`

The last mile: let the engine choose between the showcase and a `.map` at launch. `buildWorld` grows a
`mapPath` parameter and branches on it — from `src/engine/app/simulation.cpp`:

```cpp
    Level buildWorld(entt::registry& registry, ResourceManager& resources,
                     JoltWorld& joltWorld, bool headless, const std::string& mapPath)
    {
        Level level = mapPath.empty()   // empty → showcase; a path → load that .map
            ? setupScene(registry, resources, headless)
            : setupSceneFromMap(registry, resources, mapPath, headless);
```

That single ternary is the entire switch: an empty `mapPath` builds the hard-coded showcase (the
fallback), any path loads that `.map`. Everything after this line in `buildWorld` is **unchanged** —
because `setupSceneFromMap` returns a `Level` shaped exactly like `setupScene`'s, the existing calls all
just work on it:

```cpp
        // Static bodies from level geometry
        createLevelBodies(registry, level);
        joltWorld.physicsSystem->OptimizeBroadPhase();

        // Kinematic bodies for movers (lifts, doors)
        // … enemies … player CharacterVirtual …

        // Enemy pathfinding grid, derived from the now-populated scene.
        registry.ctx().emplace<NavGrid>(buildNavGrid(registry, level));

        return level;
    }
```

`createLevelBodies` builds Jolt static colliders from the level's `Surface`s — which is exactly why
Step 2 emitted `Surface`s and not meshes: collision consumes them directly, so a map-loaded room gets
solid walls with no new collision code. `buildNavGrid` (Chapter 24) reads the same surfaces for enemy
pathing. The map room is a first-class level the instant it exists. The declaration in
`simulation.h` gets the new parameter with defaults that preserve every existing caller:

```cpp
    // `mapPath` empty → the hard-coded showcase; non-empty → load that .map.
    Level buildWorld(entt::registry& registry, ResourceManager& resources,
                     JoltWorld& joltWorld, bool headless = false,
                     const std::string& mapPath = "");
```

Finally `main.cpp` chooses the path from the command line:

```cpp
int main(int argc, char** argv)
{
	std::string mapPath = (argc > 1) ? argv[1] : "assets/maps/smoke.map";  // ""/"showcase" → showcase
	if (mapPath == "showcase") mapPath.clear();
```

and passes it through:

```cpp
	Level level = qengine::buildWorld(registry, resources, joltWorld, false, mapPath);
```

So the *default* launch (`QEngine.exe` with no arguments) now loads `assets/maps/smoke.map` — your
authored room is what you get out of the box. Pass an explicit path to load a different map, or the
literal `showcase` to fall back to the hard-coded arena (which clears `mapPath`, taking the ternary's
showcase branch).

> **Why default `main` to the `.map` but keep the showcase reachable via a `showcase` argument, rather
> than deleting the old path outright?** Because the showcase is still the richest test level in the
> engine — it has doors, lifts, triggers, teleporters, lava, pickups, and enemies wired up — while
> `smoke.map` is a bare box. Making the map the *default* proves the new pipeline is the primary path and
> catches its regressions every launch, but keeping the showcase one argument away preserves a
> known-good, feature-complete scene to compare against and to fall back on while `.map` support matures
> (the loader is still MVP-fidelity geometry and doesn't yet rebuild the *whole* showcase). It's a
> deliberate, reversible cutover: new path in front, old path parked behind a flag, neither deleted
> until the new one has fully earned it. The plan called the showcase a "regression safety" net, and a
> one-word argument is the cheapest possible way to keep that net hanging.

---

## Step 8: Pinning the Conversion — the `map_scene` Scenario

The parser had `map_parse` and `map_file` (Chapter 27); the *conversion* — brushes → surfaces, entities
→ descriptors — gets its own headless scenario, `scenarioMapScene`, in `src/harness/map_scenarios.cpp`.
It's pure data: no GL, no registry, no physics — just load `smoke.map`, run the two converters, and
assert the numbers:

```cpp
    bool scenarioMapScene()
    {
        std::string err;
        qmap::MapData map = qmap::loadMapFile("assets/maps/smoke.map", &err);

        Level lvl = mapWorldspawnToLevel(map);
        std::size_t surfaces = lvl.sectors.empty() ? 0 : lvl.sectors[0].surfaces.size();

        auto descriptors = mapEntitiesToDescriptors(map);
        int players = 0, lights = 0;
        for (const auto& d : descriptors)
        {
            if (d.classname == "info_player_start") ++players;
            if (d.classname == "light")             ++lights;
        }

        bool ok = err.empty()
               && lvl.sectors.size() == 1
               && surfaces == 36
               && descriptors.size() == 2
               && players == 1
               && lights == 1;

        char buf[220];
        std::snprintf(buf, sizeof(buf),
            "sectors=%zu surfaces=%zu descriptors=%zu (player=%d light=%d) err=\"%s\"",
            lvl.sectors.size(), surfaces, descriptors.size(), players, lights, err.c_str());
        return report("map_scene", ok, buf);
    }
```

Every number is a claim about the pipeline: **one sector**, **36 surfaces** (6 box brushes × 6 faces —
the geometry converter did its job), **two descriptors** (the spawn and the light — worldspawn correctly
*excluded* as geometry), of which exactly **one player start** and **one light**. If any stage broke —
the parser miscounted brushes, `mapWorldspawnToLevel` emitted the wrong surface count, or
`mapEntitiesToDescriptors` failed to skip worldspawn or mis-classified an entity — one of these numbers
moves and the scenario fails. It's the whole data path from file to spawn-ready descriptors, asserted in
one place, with no window. Register it in `main`'s dispatch alongside the Chapter-27 scenarios:

```cpp
    else if (scenario == "map_parse")        pass = mapscenarios::scenarioMapParse();
    else if (scenario == "map_file")         pass = mapscenarios::scenarioMapFile(mapArg);
    else if (scenario == "map_scene")        pass = mapscenarios::scenarioMapScene();
```

> **Why assert exact counts (36 surfaces, 2 descriptors) rather than just "the level isn't empty"?**
> Because "not empty" would pass on a level that's subtly wrong — 35 surfaces because a brush was
> dropped, or 3 descriptors because worldspawn leaked into the entity list, or 2 lights because the
> spawn was misread. The specific numbers turn the scenario into a *regression witness*: they encode
> exactly what `smoke.map` is supposed to produce, so any future change to the parser, the transform,
> the AABB geometry, or the descriptor mapping that alters the output by even one surface trips the test
> immediately. Because `smoke.map` is a fixed fixture with a known shape (Chapter 26 drew it
> deliberately), its correct conversion is a fixed set of numbers, and pinning them is what makes the
> whole load-and-convert path safe to refactor later.

---

## What Changed — Summary

| File | Change |
|------|--------|
| `engine/level/map_transform.h` | **New file** — the single map-space↔engine-space boundary: `kMapUnitsPerEngineUnit = 32`, plus `mapPointToEngine` (position: swap `{x,z,-y}` + scale), `mapDirToEngine` (direction: swap only), `mapExtentToEngine` (size), `mapAngleToEngineYaw`. |
| `engine/level/map_to_level.{h,cpp}` | **New files** — `mapWorldspawnToLevel`: worldspawn brushes → one sector of axis-aligned quad `Surface`s (each brush as its engine-space AABB, six CCW-from-outside faces, majority-vote texture). MVP fidelity, lossless for box brushes. |
| `engine/level/level.h` | New `Level::renderMeshes` (`vector<unique_ptr<Mesh>>`) — owns the loader's per-texture meshes for the level's lifetime so the VAOs the `MeshRenderer`s reference aren't freed. |
| `engine/level/build_textured_meshes.{h,cpp}` | **New files** — `buildTexturedMeshes`: group a sector's surfaces by texture name into one render `Mesh` each (planar UVs from edge lengths), so a multi-texture room renders through the single-texture draw path. |
| `engine/level/map_to_descriptors.{h,cpp}` | **New files** — `mapEntitiesToDescriptors`: every non-worldspawn entity → `factories::SpawnParams` (origin/size from brush AABB or `origin` key; `endpos`/`velocity`/`direction` converted into engine space). |
| `engine/app/scene_setup_map.{h,cpp}` | **New files** — `setupSceneFromMap`: parse → geometry → per-texture render meshes (non-headless) → `spawnScene(mapEntitiesToDescriptors(map))` → combat resources. Mirrors `setupScene`; returns a `Level` indistinguishable to the rest of the engine. |
| `engine/app/simulation.{h,cpp}` | `buildWorld` takes `const std::string& mapPath` (default `""`): empty → `setupScene` (showcase), non-empty → `setupSceneFromMap`. Everything after (collision, movers, enemies, player, nav grid) unchanged. |
| `src/main.cpp` | `main(argc, argv)`: default map path `assets/maps/smoke.map`; literal `showcase` clears it to fall back to the hard-coded arena; passed through to `buildWorld`. |
| `CMakeLists.txt` | Add `map_to_level.cpp`, `map_to_descriptors.cpp`, `build_textured_meshes.cpp`, `scene_setup_map.cpp` (and Chapter 27's `map_loader.cpp`) to `qengine_lib`. |
| `harness/map_scenarios.{h,cpp}` | New `scenarioMapScene` — load `smoke.map`, convert, assert 1 sector / 36 surfaces / 2 descriptors (1 player, 1 light). Registered as `map_scene`. |

`map_transform.h` is header-only. The `.map` loader touches no existing gameplay system: it plugs into
`buildWorld` at the scene-setup seam and reuses collision, the nav grid, and `spawnScene` unchanged.

---

## What You Should See

Run `build/QEngine.exe` (no arguments):

1. **You spawn inside `smoke.map`.** The default launch loads your authored box room — `grid_orange`
   walls, `grid_grey` floor and ceiling, lit by the single `light`, with the player at the
   `info_player_start`. This is a TrenchBroom-drawn level running in the engine.
2. **The room is solid.** You collide with the walls, stand on the floor, and can't walk through the
   ceiling — because `createLevelBodies` built Jolt colliders from the same `Surface`s the renderer
   drew, no map-specific collision code required.
3. **Textures render per-group.** Walls and floor show their distinct textures at world scale (planar
   UVs), proving the per-texture mesh split works through the single-texture draw path.
4. **`build/QEngine.exe showcase`** still drops you into the full hard-coded arena — doors, lifts,
   triggers, pickups, enemies — the fallback path intact.
5. **`build/QEngine.exe assets/maps/<your-map>.map`** loads any map you author, so the edit→save→run
   loop is now real: draw a room in TrenchBroom, save it under `assets/maps/`, and run it.

Headless:

6. **`QEngineHeadless map_scene` passes** — `smoke.map` converts to exactly 1 sector, 36 surfaces, and 2
   descriptors (1 player start, 1 light), pinning the whole brushes→surfaces→descriptors path with no
   window.
7. **`map_parse` and `map_file` still pass** (Chapter 27) — the parser under all of this is unmoved.

---

## What's Next

The TrenchBroom pipeline is complete end to end: author a level in a real editor, save a `.map`, and the
engine parses it, converts it into engine space once, builds walkable-and-collidable geometry, renders
it per-texture, and spawns its entities through the same factories the showcase uses. The engine's levels
are now **data**, not code.

What remains is honest, scoped follow-up, all signposted in the code itself. The geometry is
**AABB-fidelity** — lossless for the axis-aligned boxes of `smoke.map`, but an angled brush would collapse
to its bounding box; the general plane∩plane∩plane brush geometry (Cramer intersection, Sutherland–
Hodgman clipping, fan triangulation) is the documented next step, and it slots in behind the same
`Surface` output so nothing downstream changes. Brush **collision** currently rides on the AABB surfaces
through `createLevelBodies`; convex-hull colliders from the brush planes are the matching upgrade. And the
`showcase.map` rebuild — reproducing the full hard-coded arena as an authored map, then deleting the C++
descriptor — is the milestone that retires level-as-code for good. But the spine is in: from this chapter
on, a room in the game is a room someone drew.
