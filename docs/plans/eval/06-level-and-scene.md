# 06 — Level & Scene Setup

**Status:** Evaluated.
**Scope:** `level/{level.h,level_loader.*}`, `ecs/{showcase_level.*,scene_setup.*}`, `ecs/jolt_body_helpers.*`.
**Part of:** [README.md](README.md).

## Findings

| # | Issue | Sev | Detail | Action | Location |
|---|-------|-----|--------|--------|----------|
| 6.1 | **Level geometry has no collision entity** | P1 | Sector meshes are spawned as entities with only `Position` + `MeshRenderer` — **no `AABBCollider`**. So anything that collides via the ECS AABB path (projectiles) ignores walls/floors entirely. Root cause of [07 §7.2](07-gameplay-systems.md) (projectiles through walls). Jolt *does* have static level bodies, but those aren't visible to the ECS-AABB systems. | Decide the single collision source for projectiles (Jolt raycast/shapecast vs. give level a collider representation). | [scene_setup.cpp:27-43](../../../src/engine/ecs/scene_setup.cpp#L27-L43) |
| 6.2 | **`LevelLoader::load` ignores open failure** | P1 | On a missing file it logs but does **not** return — it proceeds to parse a closed stream, producing a silent empty level. | Return early / signal failure. | [level_loader.cpp:14-18](../../../src/engine/level/level_loader.cpp#L14-L18) |
| 6.3 | **Body helpers skip `IsValid()`** | P1 | `createDynamic/Kinematic/Static/Sensor` call `shapeResult.Get()` without checking `IsValid()` (only `createLevelBodies` checks). Any collider with a half-extent below Jolt's convex radius (0.05) → invalid shape → crash/garbage body. Current scene's thin colliders (door x=0.1, lift y=0.1) are just above the line — fragile. | Add the `IsValid()` guard to all four helpers; clamp half-extents. | [jolt_body_helpers.cpp:71-153](../../../src/engine/ecs/jolt_body_helpers.cpp#L71-L153) |
| 6.4 | Per-surface static bodies → internal edges | P1 | `createLevelBodies` makes one fattened box per surface; adjacent boxes create seams the player capsule snags on (the floor↔lift seam included). Cross [05 §4](05-physics.md). | Evaluate `mEnhancedInternalEdgeRemoval` and/or merged collision shapes. | [jolt_body_helpers.cpp:5-57](../../../src/engine/ecs/jolt_body_helpers.cpp#L5-L57) |
| 6.5 | `parseSector` unvalidated id | P2 | Negative/huge `sector.id` → `resize(id+1)` blowup on malformed input. | Validate id range. | [level_loader.cpp:69-71](../../../src/engine/level/level_loader.cpp#L69-L71) |
| 6.6 | `LevelLoader` path currently unexercised | P2 | Runtime uses `createShowcaseLevel()`; the text `.map`/level loader isn't called yet. It's latent until Phase 5 (Ch 17). Bugs here (6.2, 6.5) won't surface until then. | Add a smoke test before Ch 17 leans on it. | [scene_setup.cpp:25](../../../src/engine/ecs/scene_setup.cpp#L25) |
| 6.7 | `buildSectorMeshes` assumes quads | P2 | Hardcoded 4 vertices / 2 triangles per surface; non-quad surfaces unsupported. | Note as a Phase-5 constraint. | [level_loader.cpp:189-224](../../../src/engine/level/level_loader.cpp#L189-L224) |
| 6.8 | Body-helper boilerplate duplication | P2 | Four near-identical create functions. | Extract a shared builder (shape + settings). | [jolt_body_helpers.cpp:59-184](../../../src/engine/ecs/jolt_body_helpers.cpp#L59-L184) |
| 6.9 | `scene_setup` is a 380-line hardcoded scene | P2 | Lights, demos, movers, triggers all inline. Will be superseded by TrenchBroom (Phase 5); fine for now but heavy. | Keep until Ch 18 entity mapping replaces it. | [scene_setup.cpp](../../../src/engine/ecs/scene_setup.cpp) |

## Graduates to a fix plan
- 6.3 (IsValid guards) → quick safety fix, can ride with the physics PR.
- 6.4 → part of `docs/plans/physics-fixes.md` (the lift/movement work).
- 6.1 → tied to the projectile fix; decide collision source in `docs/plans/projectile-collision-fix.md` together with [07 §7.2].
- 6.2 + 6.5 → `docs/plans/level-loader-hardening.md`, scheduled **before** Phase 5.
