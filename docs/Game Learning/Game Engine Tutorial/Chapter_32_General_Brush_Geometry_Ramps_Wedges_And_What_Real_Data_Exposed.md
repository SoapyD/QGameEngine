# Chapter 32: General Brush Geometry — Ramps, Wedges, and What Real Data Exposed

## What You'll Learn
- Why the `.map` loader's MVP — **brush → AABB → 6 quads** — is lossless for a box but *flattens* every
  angled brush, so ramps and wedges came in as boxes and couldn't be walked up
- The geometric idea that fixes it: a **convex brush is the intersection of its face half-spaces**, and its
  corners are the **triple-plane intersections** that lie inside every one of them
- The three-plane intersection formula, and why the map→engine transform being a **rotation (det +1)**
  means the source winding — and therefore the outward normal — survives the coordinate swap
- Building `buildBrushGeometry`: face planes from the winding, corner vertices, then per-face **dedup +
  CCW-from-outside sort** into convex polygons — plus the full corner set for a collider
- Emitting those polygons as engine `Surface`s while **keeping `Surface` a quad** (a 4-gon maps 1:1, an
  N-gon *fans*), so box maps are byte-for-byte unchanged and the render/hitscan/LoS/nav paths never learn
  about polygons
- Colliding angled brushes by their **true shape** — one Jolt `ConvexHullShape` per brush — with the old
  AABB-per-surface path kept as an either/or fallback for the hull-less C++ showcase
- **War story #1:** a winding bug that passed a hand-built unit test *and* the box smoke map but silently
  dropped a real authored ramp — and why a loader like this needs a **real-data** test, not just synthetic
  brushes
- **War story #2:** a shutdown use-after-free that a `grep PASS` smoke check completely masked — and the
  lesson that you must read the binary's **real exit code** (139 = segfault), not just its stdout

---

## Where We Are

Chapters 27–30 built the TrenchBroom `.map` pipeline end to end: a parser (27) turning text into an IR, a
coordinate transform and geometry builder (28) turning that IR into a walkable room, the full showcase map
(29), and the ground-placement and debug-draw fixes that made it feel right to stand in (30). But every one
of those chapters shipped on a deliberate simplification, stated plainly back in Chapter 28: each brush was
resolved to its **axis-aligned bounding box**, and that box became six quads.

For a box-shaped brush — a wall slab, a floor, a pillar — the AABB *is* the brush, so the MVP was lossless.
The old `mapWorldspawnToLevel` said exactly that in its comment:

```cpp
    // The texture used by most of a brush's faces. For single-texture box brushes
    // (the smoke.map case) this is exact; for mixed-texture brushes the AABB
    // representation can only carry one, so majority wins.
```

The trouble starts the moment a mapper draws a **ramp**. A wedge brush — a triangular prism whose top face
slopes — has an AABB that is a full box: the loader would fill in the empty corner the ramp cuts away, and
the player would meet an invisible vertical wall instead of a walkable slope. The showcase map already
*contained* an authored ramp, and it simply wasn't there in the engine. Angled geometry was the single
largest piece of fidelity the loader still threw away.

This chapter closes that gap. We replace the brush→AABB approximation with **true brush resolution**: given
a brush's faces, compute its real convex shape — every corner vertex and every face polygon — in engine
space. Ramps become ramps; box maps stay exactly as they were. Along the way the feature surfaces two
genuinely instructive bugs, and both are in here, because the debugging is as much the lesson as the
geometry.

The work, built data-first as always:

1. **The idea** — a convex brush is the intersection of its face half-spaces (`brush_geometry.h`).
2. **The output types** — `BrushFacePolygon` and `BrushGeometry` (`types/brush_geometry.h`).
3. **The builder** — `buildBrushGeometry`: planes → corner vertices → per-face convex polygons.
4. **Emitting surfaces** — `map_to_level.cpp`: polygons → `Surface`s (quad 1:1, N-gon fans), corners → hulls.
5. **Colliding by true shape** — `create_level_bodies.cpp`: one `ConvexHullShape` per brush, or the AABB
   fallback.
6. **War story #1** — the winding bug real data found, and the real-data test that now guards it.
7. **War story #2** — the shutdown segfault a `grep PASS` hid.

Everything below is grounded in the files changed this commit:
`src/engine/level/brush_geometry.{h,cpp}`, `src/engine/level/types/brush_geometry.h`,
`src/engine/level/map_to_level.cpp`, `src/engine/level/level.h`,
`src/engine/physics/bodies/create_level_bodies.cpp`, `src/harness/map_scenarios.{h,cpp}`,
`src/harness/headless_main.cpp`, and `src/main.cpp`.

---

## Step 1: The Idea — a Convex Brush Is the Intersection of Its Faces

The IR from Chapter 27 already told us that a brush is a *list of faces*, and each face is a *plane* given as
three points. `MapBrush`'s comment said what that means:

```cpp
    // A convex brush = the intersection of its face half-spaces. Solid world
    // geometry and brush entities (doors/lifts/triggers) are made of these.
    struct MapBrush
    {
        std::vector<MapFace> faces;
    };
```

That one line is the whole geometry of a brush. Every face defines an infinite plane, and the plane splits
space into two half-spaces — an *inside* and an *outside*. The brush is the set of points that are inside
*every* face's plane at once: the **intersection of the half-spaces**. Because a half-space is convex and an
intersection of convex sets is convex, a brush is always a convex solid — which is exactly why level editors
build the world out of them.

That definition hands us a constructive recipe for the corners. A corner of a convex polyhedron is a point
where (at least) **three** faces meet. So every corner vertex is the intersection of three of the brush's
face planes — and it's a *real* corner only if it also lies inside all the *other* faces' half-spaces
(otherwise it's a "virtual" intersection out past the edge of the solid, where three planes cross but the
brush doesn't actually reach). Resolve the brush, then, in three moves:

1. Turn each face into a plane (a normal `n` and an offset `d`, with the brush interior on the `dot(n,x) ≤ d`
   side).
2. For every triple of planes, compute their single intersection point, and **keep it only if it lies inside
   every face half-space**. Those kept points are the brush's corners.
3. For each face, gather the corners that lie *on* it and sort them into a convex polygon.

That's the entire algorithm. The next steps are just careful C++ around those three ideas, in engine space.

> **Why resolve the brush from its planes instead of storing corner vertices in the `.map` file?** Because
> the `.map` format simply doesn't have them — a brush is stored as a *bag of planes*, three points per face,
> and nothing else. TrenchBroom edits brushes by dragging faces (planes), so the plane representation is the
> authoritative one and the corners are derived on load. That's why the parser (Chapter 27) kept only the
> three plane-defining points per face and deliberately computed no normal: the geometry the renderer and the
> collider want — corners, edges, polygons — is a thing you *reconstruct* from the planes, and this chapter is
> where that reconstruction finally happens. The MVP skipped it by taking the AABB; doing it properly is the
> price of admitting a brush that isn't a box.

---

## Step 2: The Output Types

Data before code. Before writing the builder, we define what it *produces*: a per-face polygon (for render
surfaces) and the full corner set (for a collider). Create `src/engine/level/types/brush_geometry.h`:

```cpp
#pragma once

#include <glm/glm.hpp>

#include <string>
#include <vector>

// Output types for buildBrushGeometry (declared in ../brush_geometry.h). A brush
// resolved from its face half-spaces into explicit geometry, in ENGINE space.

namespace qmap
{
    // One brush face as a convex polygon, wound CCW as seen from OUTSIDE the brush
    // (GL_CCW front + back-face cull), with an outward normal.
    struct BrushFacePolygon
    {
        std::vector<glm::vec3> vertices;
        glm::vec3   normal{0.0f};
        std::string texture;
    };

    // Per-face polygons (for render surfaces) + the full set of corner vertices
    // (for a convex-hull collider).
    struct BrushGeometry
    {
        std::vector<BrushFacePolygon> faces;
        std::vector<glm::vec3>        vertices;
    };
}
```

Two structs, matching the two consumers. A `BrushFacePolygon` is a convex ring of vertices with an outward
normal and a texture name — everything the render path needs to make a surface, and unlike the MVP's
majority-per-brush texture, **each face keeps its own texture**. A `BrushGeometry` bundles the per-face
polygons (which become `Surface`s) with the brush's complete corner-vertex set (which becomes a collider).
Both are stored in **engine space** — the axis-swap and scale from `map_transform.h` have already been
applied — so a consumer never has to think about map units again.

> **Why hand back both a per-face polygon list *and* a flat vertex set, rather than one and deriving the
> other?** Because the two consumers want different things and deriving one from the other is lossy or
> wasteful either way. The renderer needs faces *grouped and wound* — a polygon per surface, with its normal
> and texture — and reconstructing that grouping from a flat vertex soup would mean redoing the sort we're
> about to do anyway. The collider (Step 5) wants the *opposite*: it doesn't care about faces or winding at
> all, it just needs the point cloud to feed Jolt's `ConvexHullShape`, which re-derives the hull itself.
> Producing both in one pass, while we already have the corners in hand, means neither consumer pays to
> recover what the builder already knew.

---

## Step 3: The Builder — `buildBrushGeometry`

Now the code that turns a `MapBrush` into a `BrushGeometry`. Its public face is one function, declared in
`src/engine/level/brush_geometry.h`:

```cpp
#pragma once

#include "engine/level/types/map_data.h"
#include "engine/level/types/brush_geometry.h"

// Resolve a convex brush (the intersection of its face half-spaces) into explicit
// geometry via plane∩plane∩plane — the "general geometry" upgrade over the MVP's
// brush→AABB→6-quads approximation. Handles arbitrary angled brushes (ramps,
// wedges) losslessly. Output is in ENGINE space (mapPointToEngine applied).

namespace qmap
{
    // Build the geometry for one brush. Returns empty faces/vertices for a
    // degenerate or unbounded brush (fewer than 4 faces, or no valid corners) —
    // the caller should skip such a brush.
    BrushGeometry buildBrushGeometry(const MapBrush& brush);
}
```

The implementation lives in `src/engine/level/brush_geometry.cpp`. Start with the two anonymous-namespace
helpers: a `Plane` type, and the three-plane intersection.

```cpp
namespace
{
    constexpr float kEps = 1e-3f;   // engine-space tolerance (units are small)

    // A face plane: interior of the brush is dot(n, x) <= d.
    struct Plane { glm::vec3 n; float d; };

    // Intersection point of three planes; false if (near-)parallel / singular.
    bool intersect3(const Plane& a, const Plane& b, const Plane& c, glm::vec3& out)
    {
        glm::vec3 bc = glm::cross(b.n, c.n);
        float denom = glm::dot(a.n, bc);
        if (std::fabs(denom) < 1e-6f) return false;
        out = (a.d * bc
             + b.d * glm::cross(c.n, a.n)
             + c.d * glm::cross(a.n, b.n)) / denom;
        return true;
    }
}
```

`intersect3` is the closed-form solution to "where do three planes `dot(n,x)=d` meet?" — the numerator is a
combination of the cross-products of the plane normals weighted by their offsets, over the scalar triple
product `dot(a.n, b.n × c.n)` in the denominator. When that denominator is near zero the planes are parallel
or share a line (no single intersection), and the function bails with `false`. `kEps` is a small tolerance —
engine units are tiny (32 map units per engine unit), so a millimetre-scale epsilon is generous.

Now the builder proper. It opens by rejecting anything that can't be a bounded solid, then builds a `Plane`
per face:

```cpp
    BrushGeometry buildBrushGeometry(const MapBrush& brush)
    {
        BrushGeometry geo;
        const size_t nf = brush.faces.size();
        if (nf < 4) return geo;   // a bounded solid needs ≥ 4 faces

        // Face planes in engine space. Quake/TrenchBroom writes each face's three
        // points CLOCKWISE as seen from the front, so cross(p2-p0, p1-p0) is the
        // OUTWARD normal and the brush interior is dot(n, x) <= d. (Deriving "inside"
        // from the average of the face points fails: TrenchBroom emits each face as a
        // tiny plane-defining triangle, so those points cluster at a couple of corners
        // rather than spanning the brush, and their average can fall outside it.)
        // The map→engine transform is a rotation (det +1), so the winding — hence the
        // outward direction — is preserved.
        std::vector<Plane> planes(nf);
        for (size_t i = 0; i < nf; ++i)
        {
            glm::vec3 p0 = mapPointToEngine(brush.faces[i].points[0]);
            glm::vec3 p1 = mapPointToEngine(brush.faces[i].points[1]);
            glm::vec3 p2 = mapPointToEngine(brush.faces[i].points[2]);
            glm::vec3 n  = glm::normalize(glm::cross(p2 - p0, p1 - p0));
            planes[i] = { n, glm::dot(n, p0) };
        }
```

Read that comment carefully — it's the heart of the chapter, and Step 6 is the story of how it came to say
what it says. The `.map` Standard format writes each face's three points **clockwise as seen from the
front** (the outside). Under that convention `cross(p2 - p0, p1 - p0)` points *outward*, so `n` is the
outward normal and the brush interior is the `dot(n, x) ≤ d` side, with `d = dot(n, p0)` since `p0` is on the
plane. Each face point is run through `mapPointToEngine` first, so the whole computation happens in engine
space.

The parenthetical warning — *don't derive "inside" from the average of the face points* — and the closing
note that *the map→engine transform is a rotation (det +1), so the winding is preserved* are both hard-won.
Hold them; Step 6 explains them from the crash inward.

With planes in hand, the `inside` test and the corner search:

```cpp
        auto inside = [&](const glm::vec3& v)
        {
            for (const auto& pl : planes)
                if (glm::dot(pl.n, v) > pl.d + kEps) return false;
            return true;
        };

        // Corner vertices = triple-plane intersections that lie inside every
        // half-space. Record each on the (up to 3) faces it sits on.
        std::vector<std::vector<glm::vec3>> faceVerts(nf);
        for (size_t i = 0; i < nf; ++i)
          for (size_t j = i + 1; j < nf; ++j)
            for (size_t k = j + 1; k < nf; ++k)
            {
                glm::vec3 v;
                if (!intersect3(planes[i], planes[j], planes[k], v)) continue;
                if (!inside(v)) continue;
                geo.vertices.push_back(v);
                for (size_t f : { i, j, k })
                    if (std::fabs(glm::dot(planes[f].n, v) - planes[f].d) < kEps)
                        faceVerts[f].push_back(v);
            }

        if (geo.vertices.size() < 4) { geo = {}; return geo; }
```

`inside(v)` is the half-space test straight from Step 1: `v` is inside the brush if it's on the interior side
(`dot(n,v) ≤ d + kEps`) of every face. The triple loop enumerates every combination of three distinct faces,
intersects their planes, discards intersections that miss (`intersect3` returned `false`) or fall outside the
solid (`!inside(v)`), and records each survivor as a corner. Crucially it also files that corner under **each
face it actually lies on** — the `for (size_t f : { i, j, k })` inner loop, guarded by a check that `v` sits
on face `f`'s plane — so we build up, per face, the list of its own corners. If fewer than four corners
survive, the brush is degenerate or unbounded and we return empty (the caller skips it).

Finally, assemble each face's corners into a properly wound convex polygon:

```cpp
        glm::vec3 centroid(0.0f);
        for (const auto& v : geo.vertices) centroid += v;
        centroid /= static_cast<float>(geo.vertices.size());

        // Each face: dedup its corner points, orient the normal outward (insurance
        // against a mis-wound source face), then sort the points CCW-from-outside.
        for (size_t i = 0; i < nf; ++i)
        {
            std::vector<glm::vec3> pts;
            for (const auto& v : faceVerts[i])
            {
                bool dup = false;
                for (const auto& q : pts) if (glm::length(v - q) < kEps) { dup = true; break; }
                if (!dup) pts.push_back(v);
            }
            if (pts.size() < 3) continue;   // sliver / degenerate face

            glm::vec3 fc(0.0f);
            for (const auto& v : pts) fc += v;
            fc /= static_cast<float>(pts.size());

            glm::vec3 n = planes[i].n;
            if (glm::dot(n, fc - centroid) < 0.0f) n = -n;   // point away from the brush

            // In-plane basis (u, w) with u×w = n, so increasing atan2(w,u) is CCW
            // when viewed from the +n (outside) side.
            glm::vec3 ref = std::fabs(n.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
            glm::vec3 u = glm::normalize(glm::cross(n, ref));
            glm::vec3 w = glm::cross(n, u);
            std::sort(pts.begin(), pts.end(), [&](const glm::vec3& a, const glm::vec3& b)
            {
                return std::atan2(glm::dot(a - fc, w), glm::dot(a - fc, u))
                     < std::atan2(glm::dot(b - fc, w), glm::dot(b - fc, u));
            });

            BrushFacePolygon poly;
            poly.vertices = std::move(pts);
            poly.normal   = n;
            poly.texture  = brush.faces[i].texture;
            geo.faces.push_back(std::move(poly));
        }

        return geo;
    }
```

Three things happen per face. **Dedup:** a single corner can be produced by more than one plane-triple, so
duplicates within `kEps` are collapsed. A face left with fewer than three points is a sliver and skipped.
**Orient:** the normal is `planes[i].n`, flipped if it happens to point *toward* the brush centroid — belt
and braces against a mis-wound source face, so the polygon's normal reliably faces out. **Sort:** to wind the
corners into a convex ring, build an in-plane basis `(u, w)` whose cross product is the outward normal, then
sort the points by `atan2(w-component, u-component)` about the face centre — which orders them
counter-clockwise as seen from outside, matching the engine's `GL_CCW` front-face + back-face-cull
convention. The result is one `BrushFacePolygon` per real face, carrying that face's own texture.

> **Why sort the face's corners by angle in an in-plane basis rather than trusting the order they were
> found?** Because the order corners are *discovered* in is meaningless — it's whatever the `i<j<k` triple
> loop happened to visit — and a renderer that draws a polygon's vertices in a scrambled order produces a
> self-intersecting bow-tie, not a face. A convex polygon has exactly one correct cyclic order, and the
> cheapest way to recover it is to project every corner into the face's own plane, measure each one's angle
> about the face centre with `atan2`, and sort. Choosing the basis so that `u × w = n` makes "increasing
> angle" mean "counter-clockwise from outside," which is precisely the winding the back-face cull expects, so
> the faces we emit render front-side-out with no extra bookkeeping. It's O(V log V) per face on a handful of
> corners — free — and it's the standard way to wind a convex face from an unordered vertex set.

---

## Step 4: Emitting Surfaces — Keep `Surface` a Quad, Fan the N-gons

The builder produces convex polygons of *any* vertex count. The engine's render/collision/nav paths, though,
have known about a `Surface` being a **quad** since Chapter 8:

```cpp
struct Surface
{
	glm::vec3 vertices[4]; // quad corners
	glm::vec3 normal; // computed from vertices
	std::string textureName;
	unsigned int textureId = 0;
};
```

Rather than widen `Surface` to an arbitrary polygon — which would ripple through the renderer, the hitscan
raycast, the enemy line-of-sight test, and the nav-grid rasteriser, every one of which assumes four corners —
`map_to_level.cpp` keeps `Surface` a quad and adapts the polygon *to* it. A 4-gon (the overwhelmingly common
case — every face of a box brush) maps 1:1; an N-gon is triangle-**fanned**, each triangle stored as a
degenerate quad. The new `emitPolygon` helper:

```cpp
    // Emit a convex face polygon as engine Surfaces. Surface is a quad, so a 4-gon
    // maps 1:1 (box maps are unchanged — smoke.map still yields 6 surfaces/brush),
    // and an N-gon fans into (N-2) triangle-surfaces stored as degenerate quads
    // (v0, vt, vt+1, vt+1). The renderer/AABB paths treat the repeated corner
    // harmlessly.
    void emitPolygon(Sector& sector, const qmap::BrushFacePolygon& face)
    {
        const auto& v = face.vertices;
        if (v.size() < 3) return;

        auto push = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d)
        {
            Surface s;
            s.vertices[0] = a; s.vertices[1] = b; s.vertices[2] = c; s.vertices[3] = d;
            s.normal = face.normal;
            s.textureName = face.texture;
            sector.surfaces.push_back(s);
        };

        if (v.size() == 4)
        {
            push(v[0], v[1], v[2], v[3]);   // exact quad — the box-brush common case
            return;
        }
        for (size_t t = 1; t + 1 < v.size(); ++t)   // fan from v[0]
            push(v[0], v[t], v[t + 1], v[t + 1]);
    }
```

A quad becomes one `Surface`. An N-gon becomes `N−2` surfaces, each a triangle `(v[0], v[t], v[t+1])` stored
as a quad by repeating the last corner (`v[t+1]` in both slots 2 and 3). A degenerate quad with two identical
corners renders as the triangle it is, and the AABB-from-corners maths in the collision fallback treats the
repeated point harmlessly. The tri fan is valid because the polygon is convex — every triangle from `v[0]`
lies inside the face.

The new `mapWorldspawnToLevel` calls the builder per brush, emits each face polygon, and — this is new —
accumulates the brush's corner set into the level's collision hulls:

```cpp
        for (const auto& brush : entity.brushes)
        {
            qmap::BrushGeometry geo = qmap::buildBrushGeometry(brush);
            if (geo.faces.empty()) continue;   // degenerate / unbounded — skip

            for (const auto& face : geo.faces)
            {
                emitPolygon(world, face);
                for (const auto& p : face.vertices) { lvlMin = glm::min(lvlMin, p); lvlMax = glm::max(lvlMax, p); }
            }

            // True convex hull for collision (angled brushes collide by their real
            // shape, not a fattened AABB).
            level.collisionHulls.push_back(std::move(geo.vertices));
        }
```

The sector bounds are grown from the real polygon vertices, and each brush's corners are pushed onto
`level.collisionHulls` (a new field — Step 5). The old `majorityTexture` helper and the whole
`addBoxSurfaces` AABB path are gone: textures are now per-face, and geometry is the real brush.

Note the deliberate invariant baked into the comment: **a box brush still yields six quad surfaces.** A box
has six 4-gon faces, each of which takes the `v.size() == 4` fast path and emits one `Surface`. So
`smoke.map` — six-faced box brushes throughout — produces byte-for-byte the same surface count it did under
the MVP, and the Chapter 28 `map_scene` scenario still counts its 36 surfaces. General geometry is strictly a
*superset*: it changes nothing for the maps the MVP already handled, and it adds the ones it couldn't.

> **Why keep `Surface` a quad and fan N-gons, instead of making `Surface` a real polygon?** Because a
> `Surface` isn't just a render primitive — it's the shared currency of five subsystems. The mesh builder
> uploads it, the weapon hitscan ray-tests it, the enemy line-of-sight test ray-tests it, the nav-grid
> rasteriser projects it, and the AABB collider bounds it. Widening it to N vertices would mean touching and
> re-verifying every one of those, for a payoff that only matters to the small minority of faces that aren't
> quads. Fanning at the loader boundary confines the whole change to `map_to_level.cpp`: downstream, a
> triangle is just a quad with a doubled corner, which every existing path already tolerates. It's the same
> principle as the coordinate transform (Chapter 28) — absorb the awkwardness at one boundary so the rest of
> the engine keeps its simple, stable contract. The cost is a couple of extra degenerate quads per non-box
> face, which is nothing; the saving is not destabilising four other subsystems.

---

## Step 5: Colliding by True Shape — a Convex Hull per Brush

An angled brush that *renders* as a ramp but still *collides* as its bounding box would be worse than
useless — you'd see a slope and walk into an invisible wall. So collision has to use the real shape too. The
corner sets we accumulated in Step 4 are exactly what Jolt's `ConvexHullShape` wants. `Level` gains a field
to carry them, in `src/engine/level/level.h`:

```cpp
	// Per-brush corner-vertex sets for convex-hull collision (the .map loader fills
	// these; the C++ showcase leaves it empty and uses per-surface AABB colliders).
	// When non-empty, createLevelBodies builds one Jolt ConvexHullShape per entry
	// instead of AABB-per-surface — so angled brushes collide by their true shape.
	std::vector<std::vector<glm::vec3>> collisionHulls;
```

`createLevelBodies` then branches on it — an either/or. `src/engine/physics/bodies/create_level_bodies.cpp`
grows a hull path alongside the existing box path:

```cpp
    // One convex-hull static body per brush (true angled-brush collision).
    void addBrushHulls(JPH::BodyInterface& bi, const Level& level)
    {
        for (const auto& hull : level.collisionHulls)
        {
            if (hull.size() < 4) continue;   // ConvexHull needs a bounded solid

            JPH::Array<JPH::Vec3> points;
            points.reserve(hull.size());
            for (const glm::vec3& p : hull) points.push_back(JPH::Vec3(p.x, p.y, p.z));

            JPH::ConvexHullShapeSettings shapeSettings(points);
            shapeSettings.SetEmbedded();
            auto result = shapeSettings.Create();
            if (!result.IsValid()) continue;   // co-planar/degenerate — skip
            addStaticBody(bi, result.Get());
        }
    }
```

Each brush's corner cloud becomes one `ConvexHullShape` — Jolt computes the hull from the points itself, so
we don't even need the face grouping here, just the vertices. Degenerate or co-planar point sets (which can't
form a bounded hull) are skipped rather than crashing. The old per-surface AABB path is kept verbatim as
`addSurfaceBoxes`, and the entry point simply chooses:

```cpp
// Static Jolt bodies for the level geometry. A `.map` level carries per-brush
// convex hulls (angled-brush accurate); the hard-coded showcase carries none, so
// it falls back to the AABB-per-surface path it has always used.
void createLevelBodies(entt::registry& registry, const Level& level)
{
    auto& bodyInterface = registry.ctx().get<JoltWorld>().getBodyInterface();

    if (!level.collisionHulls.empty())
        addBrushHulls(bodyInterface, level);
    else
        addSurfaceBoxes(bodyInterface, level);
}
```

`.map` levels fill `collisionHulls`, so they take the hull path and collide by true shape. The hard-coded C++
showcase (Chapters 8–25) never fills it, so it stays on the AABB-per-surface path it has always used — no
regression for the game's original level, real geometry for authored maps.

> **Why one convex hull per *brush* rather than one big hull for the whole level, or a hull per surface?**
> Because the brush is the natural unit of convexity and the other two options are each wrong. A single hull
> for the whole level would be the convex hull of *every* corner — it would fill in every doorway, alcove and
> concavity, turning a room into a solid block. A hull per *surface* is degenerate: a single face is flat, it
> has no volume, and `ConvexHullShape` needs a bounded solid (hence the `hull.size() < 4` guard). A brush, by
> construction (Step 1), is convex and solid — it is *exactly* what a convex hull represents — so one hull per
> brush is both correct and cheap: the level's concavities are preserved because they're the *gaps between*
> brushes, and each brush collides as the tight solid the mapper drew. It also mirrors how the renderer treats
> a brush (a group of faces) and how the source data is authored (brush by brush), so the collision shape and
> the visible shape can never drift apart.

---

## Step 6: War Story #1 — the Winding Bug Real Data Found

Go back to Step 3's plane comment and read the parenthetical again — *deriving "inside" from the average of
the face points fails*. That warning is a scar. Here's how it happened.

The **first** version of `buildBrushGeometry` didn't use the winding to decide which way was out. It computed
each brush's interior direction from the **average of all its face points**: take the mean of every point in
every face, call that the brush's "inside," and orient each face's normal to point away from it. That's a
perfectly reasonable-sounding heuristic, and it **passed every test we had**:

- It passed a hand-built **box** unit test — a synthetic cube whose eight corners are spread evenly, so their
  average lands dead centre, and every face's normal orients correctly.
- It passed a hand-built **wedge** unit test — same story, points spread across the solid, average inside.
- It passed the box `smoke.map` on disk — again all box brushes, all well-behaved averages.

Three green tests, two of them on real geometry types (box *and* wedge), one of them a real on-disk file. And
it was still wrong. The moment we loaded a **real authored ramp** from the showcase map, the ramp silently
didn't render — zero vertices, skipped as degenerate.

The cause is a property of how TrenchBroom actually writes a `.map` that no synthetic test reproduced.
TrenchBroom stores each face as a **tiny plane-defining triangle** — three points just big enough to pin down
the plane, clustered at a corner or two of the face, *not* spread across the whole brush the way a hand-typed
fixture spreads them. So on a real ramp the union of all face points is lopsided: they bunch up at a couple
of the brush's corners, and their **average falls outside the solid**. With the "inside" point outside the
brush, every face's outward test flipped, the half-space `inside()` check rejected every candidate corner,
and the brush resolved to nothing.

The fix is the code you already read in Step 3: don't *infer* inside from the points' average — use the
**winding convention the format guarantees**. The `.map` Standard format writes each face's three points
clockwise-from-front, so `cross(p2 - p0, p1 - p0)` *is* the outward normal, full stop — no averaging, no
heuristic, no dependence on how the points are spread across the face. And it survives the coordinate change
because, as the comment notes, the map→engine transform `{m.x, m.z, -m.y}/scale` is a **rotation with
determinant +1**: a rotation preserves orientation, so a clockwise winding in map space is still clockwise in
engine space, and the outward normal stays outward.

> **Why did three passing tests — including two on real geometry shapes and one real on-disk file — still let
> a broken loader through?** Because every one of those tests fed the loader data with the *same hidden
> property*: points spread evenly across each brush. The synthetic box and wedge were hand-typed with corners
> at the extremes, and `smoke.map` was all boxes, whose averages behave. None of them exercised the thing
> that actually broke it — TrenchBroom's habit of emitting each face as a *tiny* plane triangle bunched at a
> corner. The bug didn't live in "does the geometry algorithm work"; it lived in "does it work on the
> *distribution of inputs the real tool produces*," and no amount of synthetic data probes that. The only
> test that could catch it is one that loads **the real file the real editor wrote**. That is the durable
> lesson: a loader for an external format needs a real-data test, because the failure modes that matter are
> the ones baked into the quirks of the thing on the other end of the format — and those are exactly what your
> tidy hand-made fixtures smooth away.

So the test suite grew a real-data test — Step 7.

---

## Step 7: The Real-Data Test — `scenarioBrushGeometry`

The new scenario proves three things: the algorithm is right on a synthetic **box** (6 faces, 8 corners), it
resolves a genuinely **angled** synthetic wedge (5 faces, 6 corners, with a slanted normal — not a bounding
box), and — the part Step 6 demands — it loads the **actual `showcase.map`** and asserts the authored ramp
comes through as a slanted surface with collision hulls. From `src/harness/map_scenarios.cpp`:

```cpp
    namespace
    {
        // Count face/surface normals that are NOT axis-aligned (≥2 significant
        // components) — i.e. genuinely slanted geometry (a ramp).
        bool isSlanted(const glm::vec3& n)
        {
            int big = (std::fabs(n.x) > 0.3f) + (std::fabs(n.y) > 0.3f) + (std::fabs(n.z) > 0.3f);
            return big >= 2;
        }
    }
```

`isSlanted` is the test's yardstick: an axis-aligned normal has one dominant component, so a normal with
**two or more** significant components is a genuinely slanted face — the fingerprint of a ramp that a
bounding box could never produce. The scenario builds its two synthetic brushes with a small `face` helper
that enforces the same clockwise-from-front winding `buildBrushGeometry` expects:

```cpp
    bool scenarioBrushGeometry()
    {
        // A face from 3 points, wound so cross(p2-p0, p1-p0) points along `outward`
        // (the Quake/TrenchBroom CW-from-front convention buildBrushGeometry expects).
        auto face = [](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 outward)
        {
            if (glm::dot(glm::cross(c - a, b - a), outward) < 0.0f) std::swap(b, c);
            qmap::MapFace f;
            f.points[0] = a; f.points[1] = b; f.points[2] = c;
            f.texture = "wall";
            return f;
        };

        // ── Axis-aligned box, 0..64 map units → 6 faces, 8 corners ──
        qmap::MapBrush box;
        box.faces = {
            face({ 0, 0, 0}, { 0,64, 0}, { 0, 0,64}, {-1, 0, 0}),
            face({64, 0, 0}, {64,64, 0}, {64, 0,64}, { 1, 0, 0}),
            face({ 0, 0, 0}, {64, 0, 0}, { 0, 0,64}, { 0,-1, 0}),
            face({ 0,64, 0}, {64,64, 0}, { 0,64,64}, { 0, 1, 0}),
            face({ 0, 0, 0}, {64, 0, 0}, { 0,64, 0}, { 0, 0,-1}),
            face({ 0, 0,64}, {64, 0,64}, { 0,64,64}, { 0, 0, 1}),
        };
        qmap::BrushGeometry bg = qmap::buildBrushGeometry(box);

        // ── Triangular prism (wedge) extruded along Y → 5 faces, 6 corners, one
        //    slanted normal (the hypotenuse x+z=64). ──
        qmap::MapBrush wedge;
        wedge.faces = {
            face({ 0, 0, 0}, {64, 0, 0}, { 0, 0,64}, { 0,-1, 0}),   // y=0 end
            face({ 0,64, 0}, {64,64, 0}, { 0,64,64}, { 0, 1, 0}),   // y=64 end
            face({ 0, 0, 0}, {64, 0, 0}, { 0,64, 0}, { 0, 0,-1}),   // z=0 bottom
            face({ 0, 0, 0}, { 0,64, 0}, { 0, 0,64}, {-1, 0, 0}),   // x=0 side
            face({64, 0, 0}, { 0, 0,64}, {64,64, 0}, { 1, 0, 1}),   // hypotenuse (slanted)
        };
        qmap::BrushGeometry wg = qmap::buildBrushGeometry(wedge);

        int wedgeSlanted = 0;
        for (const auto& f : wg.faces) if (isSlanted(f.normal)) ++wedgeSlanted;

        bool boxOk   = bg.faces.size() == 6 && bg.vertices.size() == 8;
        bool wedgeOk = wg.faces.size() == 5 && wg.vertices.size() == 6 && wedgeSlanted >= 1;
```

The box asserts the exact counts a cube must have — 6 faces, 8 corners. The wedge is a triangular prism whose
five faces (two triangular ends, a bottom, a vertical side, and the sloping hypotenuse `x+z=64`) resolve to 5
faces and 6 corners, **at least one of them slanted**. Those two assertions guard the algorithm. But — the
whole point of Step 6 — they are synthetic, so the scenario doesn't stop there. It loads the real file:

```cpp
        // ── Real data: the authored showcase.map contains a ramp brush. Load it and
        //    assert the loader produces a slanted surface + convex hulls (the failure
        //    mode the synthetic brushes above missed: real TrenchBroom faces are tiny
        //    plane-defining triangles, so the winding convention — not a face-point
        //    average — must drive the half-space test). ──
        std::string err;
        qmap::MapData realMap = qmap::loadMapFile("assets/maps/showcase.map", &err);
        Level level = mapWorldspawnToLevel(realMap);
        int slantedSurfaces = 0;
        for (const auto& sec : level.sectors)
            for (const auto& s : sec.surfaces) if (isSlanted(s.normal)) ++slantedSurfaces;
        bool realOk = err.empty() && !level.sectors.empty()
                   && !level.collisionHulls.empty() && slantedSurfaces > 0;

        char buf[260];
        std::snprintf(buf, sizeof(buf),
            "box %zu/%zu (want 6/8); wedge %zu/%zu/%d slanted (want 5/6/>=1); showcase.map: "
            "%zu hulls, %d slanted surfaces (want >0 each)",
            bg.faces.size(), bg.vertices.size(), wg.faces.size(), wg.vertices.size(), wedgeSlanted,
            level.collisionHulls.size(), slantedSurfaces);
        return report("brush_geometry", boxOk && wedgeOk && realOk, buf);
    }
```

`realOk` is the assertion Step 6's bug would have failed: the showcase parses cleanly, produces sectors,
fills `collisionHulls`, and — the clincher — yields **at least one slanted surface**. Under the average-of-
points bug the ramp resolved to zero vertices, so `slantedSurfaces` was `0` and `realOk` was `false`. The
scenario only passes on `boxOk && wedgeOk && realOk` — the maths right on synthetic shapes *and* the ramp
present in real, editor-written data.

The scenario is declared in `src/harness/map_scenarios.h`:

```cpp
    // General-geometry check (plane∩plane∩plane): build geometry from a hand-made
    // axis-aligned box (→ 6 faces, 8 corners) and an angled wedge/triangular prism
    // (→ 5 faces, 6 corners, with a genuinely slanted face normal — proving the
    // loader resolves true brush shape, not a bounding box).
    bool scenarioBrushGeometry();
```

and wired into the dispatch in `src/harness/headless_main.cpp` beside the other map scenarios:

```cpp
    else if (scenario == "map_ground")       pass = mapscenarios::scenarioMapGroundPlacement(mapArg);
    else if (scenario == "brush_geometry")   pass = mapscenarios::scenarioBrushGeometry();
    else { std::cerr << "unknown scenario: " << scenario << std::endl; pass = false; }
```

The new `brush_geometry.cpp` also joins `qengine_lib` in `CMakeLists.txt` (the harness's `.cpp` and the two
type headers compile into their includers):

```cmake
	src/engine/level/map_loader.cpp
	src/engine/level/brush_geometry.cpp
	src/engine/level/map_to_level.cpp
```

---

## Step 8: War Story #2 — the Segfault a `grep PASS` Hid

Running the full suite to verify the geometry work turned up a second, unrelated bug — and the story of how
it *nearly didn't* is the more valuable half.

Back in Chapter 31a, enemies became `CharacterVirtual`s, each with a physical **inner body** that Jolt
manages. That inner body is removed in the character's destructor, which calls back into the physics system.
That's fine at runtime — but at **shutdown** the order of teardown suddenly matters, and it was wrong. Both
`main.cpp` and the headless harness destroyed the `entt::registry` *after* tearing down Jolt:

```
    joltWorld.shutdown();   // does physicsSystem.reset()
    // ... registry (and its JoltCharacters) destroyed later
```

`joltWorld.shutdown()` does `physicsSystem.reset()`, destroying the physics system. Then, when the registry
was finally destroyed, every enemy still alive erased its `JoltCharacter`, whose `~CharacterVirtual` tried to
remove its inner body **from a physics system that no longer existed** — a use-after-free, crashing inside
Jolt's `BodyLock`. Any run that ended with a live enemy segfaulted *at exit*.

The fix is a one-liner in each place: **clear the registry before shutting Jolt down**, so the characters
tear down their inner bodies while the physics system is still alive. In `src/harness/headless_main.cpp`:

```cpp
    // Destroy entities (and their components) BEFORE the physics system: enemy
    // JoltCharacters remove their inner Jolt body in ~CharacterVirtual, which must
    // run while jolt is still alive (else a use-after-free segfault at exit).
    registry.clear();
    jolt.shutdown();
```

and the mirror in `src/main.cpp`:

```cpp
	audio.shutdown();
	// Destroy entities before shutdown — enemy ~CharacterVirtual removes its inner Jolt body via joltWorld.
	registry.clear();
	joltWorld.shutdown();
```

`registry.clear()` destroys every entity — and therefore every `JoltCharacter` — while Jolt is still up, so
each inner body is removed cleanly. Then `shutdown()` resets the (now enemy-free) physics system. This is the
same RAII invariant Chapter 31a leaned on at *runtime* (killing an enemy is just `registry.destroy(e)`); it
just also has to hold at *process exit*, and ordering the two teardowns correctly is what makes it.

Now the part worth internalising: **why this crash almost shipped.** The scenario harness prints its
verdict — `[PASS] brush_geometry ...` — from inside `report()`, *before* `main` returns:

```cpp
        bool report(const std::string& name, bool pass, const std::string& detail)
        {
            std::cout << "[" << (pass ? "PASS" : "FAIL") << "] " << name ...
```

The segfault happened *later*, in the teardown after every scenario had already printed `[PASS]`. So a smoke
check like `./QEngineHeadless brush_geometry | grep PASS` was **completely green** — it saw the pass line and
never knew the process then died. The test was "passing" and the binary was crashing, simultaneously.

The tell is the **exit code**. A process that segfaults exits with signal 11, which the shell reports as
`128 + 11 = 139` — not `0`, no matter what it printed first. The way to catch this class of bug is to check
the real exit code, never just stdout:

```bash
./build/QEngineHeadless brush_geometry ; echo $?
# 0   = clean pass
# 139 = segfault during/after the run (a PASS line does NOT mean exit 0)
```

With the teardown fix in place, the full suite — **22 scenarios** — both prints `[PASS]` *and* exits `0`,
checked with the real exit code rather than a `grep`.

> **Why is "check the exit code, not the output" worth a war story — isn't a passing test a passing test?**
> Because a test's printed verdict and the process's actual fate are two different facts, and this bug lived
> precisely in the gap between them. `report()` decides pass/fail from the *assertions* and prints
> immediately; the crash was in *destruction*, which runs after `main`'s logic is done and its verdict is
> already on stdout. A checker that greps for `PASS` is asking "did the assertions hold?" — and they did. It
> is *not* asking "did the program survive?", and the answer to that was no. The exit code is the one signal
> that captures the whole run including teardown: `0` means the process reached a clean return, `139` means it
> was killed by a segfault, and no amount of green stdout changes a `139` into a `0`. Any headless harness you
> trust for CI has to gate on the exit code, because destructors, static teardown, and atexit handlers all run
> *after* your last print — and that's exactly where lifetime bugs like this one hide.

---

## What Changed — Summary

| File | Change |
|------|--------|
| `engine/level/types/brush_geometry.h` | **New.** Output types for the builder: `BrushFacePolygon` (a convex face — CCW-from-outside vertices, outward normal, per-face texture) and `BrushGeometry` (per-face polygons + the full corner-vertex set), both in engine space. |
| `engine/level/brush_geometry.{h,cpp}` | **New.** `buildBrushGeometry(MapBrush)` resolves a brush via plane∩plane∩plane: face planes from the winding (`cross(p2-p0,p1-p0)` outward), corner vertices = triple-plane intersections kept inside every half-space, per-face dedup + CCW sort into convex polygons. Returns empty for a degenerate/unbounded brush. |
| `engine/level/map_to_level.cpp` | Rewritten to call `buildBrushGeometry` per brush. `emitPolygon` turns each face polygon into `Surface`s — a 4-gon stays one quad (box maps unchanged, `smoke.map` still 36 surfaces), an N-gon fans into `N−2` triangle-surfaces. Per-face textures now (was majority-per-brush). Accumulates each brush's corners into `Level.collisionHulls`. Old `addBoxSurfaces`/`majorityTexture` removed. |
| `engine/level/level.h` | Adds `std::vector<std::vector<glm::vec3>> collisionHulls` — per-brush corner sets for convex-hull collision (`.map` fills it; the C++ showcase leaves it empty). |
| `engine/physics/bodies/create_level_bodies.cpp` | `createLevelBodies` branches on `collisionHulls`: non-empty → `addBrushHulls` builds one Jolt `ConvexHullShape` per brush (true angled-brush collision); empty → the existing `addSurfaceBoxes` AABB-per-surface path for the showcase. |
| `harness/map_scenarios.{cpp,h}` | Adds `scenarioBrushGeometry`: a synthetic box (6 faces/8 corners), a synthetic wedge (5/6 + a slanted normal), and — the real-data guard — loads `assets/maps/showcase.map` and asserts the authored ramp yields a slanted surface + convex hulls. |
| `harness/headless_main.cpp` | Registers the `brush_geometry` scenario in the dispatch; adds `registry.clear()` **before** `jolt.shutdown()` (fixes the enemy `~CharacterVirtual` use-after-free at exit). |
| `main.cpp` | Adds `registry.clear()` **before** `joltWorld.shutdown()` — the runtime mirror of the harness teardown fix. |
| `CMakeLists.txt` | Adds `src/engine/level/brush_geometry.cpp` to `qengine_lib`. |

---

## What You Should See

Run `build/QEngine.exe` on the showcase, and the headless suite:

1. **Ramps are walkable.** The authored ramp in `showcase.map` now renders as a slope and you can walk *up*
   it — it collides by its true convex shape, not a bounding box, so there's no invisible wall where the
   wedge cuts its corner away.
2. **Per-face textures.** A brush with different textures on different faces shows each face's own texture,
   instead of one majority texture smeared over the whole box.
3. **Box maps are unchanged.** `smoke.map` looks and collides exactly as it did in Chapter 28 — every box
   brush still emits six quad surfaces, and `QEngineHeadless map_scene` still counts its 36 surfaces.

Headless (check the exit code, not just the `PASS` line):

4. **`QEngineHeadless brush_geometry` passes** — the synthetic box (6/8) and wedge (5/6 + slanted) resolve
   exactly, and `showcase.map` loads with convex hulls and at least one slanted surface (the ramp).
5. **The full 22-scenario suite passes *and* exits 0.** After the teardown fix, no scenario segfaults at
   exit — `; echo $?` reads `0`, where before it could read `139` while still printing `[PASS]`.

---

## What's Next

The loader now resolves *general* brush geometry — angled brushes render and collide by their true shape, box
maps are untouched — which is the last big fidelity gap in turning a `.map` into a level. What remains is a
set of smaller, bounded follow-ons the loader still defers, each already sketched as a plan under
`docs/plans/`: **brush entities** (making `func_door`/`func_lift`/`trigger_*` brushes into movers and sensors,
not just worldspawn), **textures** (loading and binding the named textures a face carries, rather than a
placeholder), **collision precision** (a proper ray-vs-polygon test so hitscan and line-of-sight hit the real
slanted face instead of its quad fan's bounds), and **showcase retirement** (once authored maps reach parity,
folding the hard-coded C++ showcase away so `.map` is the only level path). Any of those is a natural next
chapter; general geometry is the foundation they all build on.
