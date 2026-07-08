# Plan — TrenchBroom: Ray-vs-Polygon Precision (hitscan / LoS / nav)

**Group:** `trenchbroom`. **Status:** 📝 Proposed 2026-07-09. **Priority: LOW** — the AABB approximation
is acceptable at showcase scale; do this once angled geometry is common enough that the imprecision
is noticeable.

**Goal:** make hitscan, enemy line-of-sight, projectile-vs-level, and the nav grid test level geometry
by its **true polygons / convex hulls** instead of per-surface **AABB**, so angled brushes behave
correctly for shooting, sight, and pathing.

---

## Why (current limitation)
Jolt gives the *player/enemy CharacterVirtual* accurate collision (convex hulls now), but several
gameplay ray/box tests still approximate each level surface by its **axis-aligned bounding box**:
- `fireHitscan` — ray vs surface AABB (a shot can "hit" empty space beside a ramp).
- `aiClearLineOfSight` — ray vs surface AABB (sight blocked by a bounding box, not the real slope).
- `boxHitsLevel` (projectiles) — box vs surface AABB.
- `buildNavGrid` — blocks a surface's AABB footprint (over-blocks around angled walls).

For axis-aligned geometry the AABB *is* the surface, so this was exact — general geometry exposes the
gap.

## Scope
| # | Task | Notes |
|---|------|-------|
| 1 | **Ray-vs-polygon** | A `rayHitsSurface(ray, Surface)` (ray vs the surface's convex polygon in its plane) replacing the AABB shortcut in `fireHitscan` + `aiClearLineOfSight`. Surface carries its polygon already (quad / fanned tris). |
| 2 | **Box/projectile-vs-polygon** | Tighten `boxHitsLevel` — or test against `Level.collisionHulls` directly (they're the real convex shapes). |
| 3 | **Nav footprint (optional)** | Rasterize the true face footprint into the nav grid instead of the AABB, so ramps/angled walls don't over-block. |

## Notes
- Alternative to per-surface polygon tests: query **Jolt** for these rays (the hulls are already
  physical bodies) via `NarrowPhaseQuery::CastRay`. That reuses one collision representation and may
  be simpler than hand-rolled polygon math — decide during implementation.

## Verification
Headless `ray_precision`: place a ramp; fire a ray that passes *through* the ramp's bounding box but
*outside* its actual polygon — assert a **miss** (where the AABB path would false-hit). Assert a ray
along the true slope face **hits**. Keep combat/AI scenarios green.

## Docs to update on ship
`SYSTEMS.md` (combat + ai ray tests), the [engine-loader plan](2026-07-03_trenchbroom_engine-loader.md).
