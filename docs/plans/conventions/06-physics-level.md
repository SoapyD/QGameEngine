# Plan 06 — Physics Free Functions, Level, Dead-Code Retirement

**Status: PROPOSED.** Apply the standard to the free-function corners of `physics/`
and `level/`, and decide the fate of the already-labelled dead code.

## A. Physics free functions

- **`jolt_body_helpers`** (relocated here in Plan 04) — split the body-creation
  functions one-per-file: `create_static_body.cpp`, `create_dynamic_body.cpp`,
  `create_kinematic_body.cpp`, `create_sensor_body.cpp`, `create_level_bodies.cpp`,
  with a `bodies.h` barrel. Each is small and self-contained — a clean fit for the
  one-function rule.
- **`raycast.{h,cpp}`** — two free functions (`rayIntersectionsAABB`,
  `rayIntersectsTriangle`). Split into `ray_intersect_aabb.cpp` /
  `ray_intersect_triangle.cpp` + a `raycast.h` barrel, **or** leave as one cohesive
  file if under cap. Recommend split only if the size check flags it. Note: the
  triangle path may be unused by live hitscan — confirm and, if dead, label/drop it.
- **`jolt_world.h`** — covered in Plan 05 (header-only class).

## B. Level

- `level/level.h` types → `level/types/` (Plan 03).
- `level_loader.cpp` — `buildSectorMeshes` is **live** (keep); the `LevelLoader`
  `.qlvl` parser is **dead** (already banner-labelled). Decision point: extract
  `buildSectorMeshes` into its own `build_sector_meshes.{h,cpp}` (live, one function)
  and **delete** the dead `LevelLoader` + `level_loader.{h,cpp}` parser, **or** keep
  the parser as labelled reference. Recommend: split out the live function, then the
  remaining parser file is unambiguously dead and can be deleted in Plan 07.

## C. Dead-code retirement (decision required)

Already banner-labelled and confirmed not compiled / not included by live code:

| File | Recommendation |
|------|----------------|
| `physics/collision.{h,cpp}` | **delete** (superseded by Jolt; not in build) |
| `physics/spatial_hash.{h,cpp}` | **delete** (only archived used it) |
| `ecs/systems/archived/*` | **delete** the folder (Jolt replaced all; README documents the mapping) |
| `assets/levels/test.qlvl` + `LevelLoader` | **delete** once `buildSectorMeshes` is split out (B) |

This is the one plan that **removes** files rather than moving them — needs explicit
user sign-off (per the "never delete without asking" rule). Default if unsure: keep
labelled, don't delete. Deleting shrinks the surface the checks must allowlist.

## Risks

- Deletion is irreversible in the working tree — **only with user approval**, and only
  files confirmed dead by Plan 02's `check_no_default_includes_of_dead` + a final grep.
- Splitting `jolt_body_helpers` touches body creation used by every scenario (lift,
  cubes, level) — verify immediately.

## Verification

Build + 6 scenarios after each split and after any deletion. `ride_lift_up` /
`walk_floor_seams` cover kinematic + static body creation.

## Done when

Physics/level free functions meet the one-function rule, the live/dead boundary is
either split-and-deleted or split-and-labelled per the user's call, build clean, all
6 scenarios pass.
