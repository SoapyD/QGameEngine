# 01 — Build & Dead-Code Census

**Status:** Evaluated.
**Scope:** [CMakeLists.txt](../../../CMakeLists.txt) `add_executable` list vs every `.cpp`/`.h` under `src/`.
**Part of:** [README.md](README.md).

## Method
Cross-referenced the explicit source list ([CMakeLists.txt:62-89](../../../CMakeLists.txt#L62-L89)) against the file tree and grepped every orphan for live `#include`rs.

## Findings

| # | Issue | Sev | Detail | Action | Location |
|---|-------|-----|--------|--------|----------|
| 1.1 | Orphaned `collision.*` | P2 | Not compiled; only includers are `collision.cpp` itself + `archived/`. Pre-Jolt collision code. | Delete (or move to `archived/`). | `src/engine/physics/collision.{h,cpp}` |
| 1.2 | Orphaned `spatial_hash.*` | P2 | Not compiled; only includer is `archived/collision_system.h`. | Delete. | `src/engine/physics/spatial_hash.{h,cpp}` |
| 1.3 | **Duplicate `jolt_sync_system.*`** | P2 | A second copy exists in `physics/`, **not** in the build; the live one is `ecs/systems/`. Risk: editing the dead copy. | Delete the `physics/` copy. | `src/engine/physics/jolt_sync_system.{h,cpp}` |
| 1.4 | `rayIntersectsTriangle` unused | P2 | Defined in `raycast.cpp` but no caller (combat uses `rayIntersectionsAABB` only). | Confirm + remove, or wire up. | [raycast.cpp:54](../../../src/engine/physics/raycast.cpp#L54) |
| 1.5 | `aabb.h` is **live** — keep | — | Used by `trigger_system.cpp`, `combat_system.cpp`, `raycast.h`. | Keep. | `src/engine/physics/aabb.h` |
| 1.6 | `raycast.*` is **live** but legacy | P2 | Combat's AABB raycast coexists with Jolt — a second collision truth. See [07](07-gameplay-systems.md). | Keep; track divergence in 07. | `src/engine/physics/raycast.*` |
| 1.7 | `archived/` tree | P2 | 4 pre-Jolt systems (collision/movement/physics/player_movement), reference-only, not compiled. | Keep as reference but relocate out of `src/` (e.g. `docs/` or a `legacy/` root) so it doesn't read as live. | `src/engine/ecs/systems/archived/` |
| 1.8 | Near-empty TU | P2 | `jolt_world.cpp` is just an `#include` (header-only impl). Harmless. | Leave; note. | [jolt_world.cpp](../../../src/engine/physics/jolt_world.cpp) |
| 1.9 | Manual source list | P2 | `add_executable` lists files explicitly (no glob), so orphans accumulate silently. | Optional: periodic census, or document the choice. | [CMakeLists.txt:62](../../../CMakeLists.txt#L62) |

## Graduates to a fix plan
Items 1.1–1.4, 1.7 are a single low-risk cleanup PR (`docs/plans/dead-code-cleanup.md`). **Do this first** — it guarantees areas 02–08 only read live code. Verify a clean build after each deletion.
