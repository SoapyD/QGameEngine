# Plan 03 — `types/` Folders & Incidental-Type Extraction

**Status: PROPOSED.** Cross-cutting; do first among the restructures so 04–06 land
their splits against a stable type layout. Establishes the `types/` convention
(Plan 01 §2) and relocates incidental structs/enums out of implementation files.

## Inventory (from `grep` audit, 2026-06-14)

| Type | Currently in | Verdict |
|------|--------------|---------|
| `EntityHit` | `ecs/systems/combat_system.cpp` | → `ecs/types/combat.h` (param/return type) |
| `PointLightGPU` | `ecs/systems/render_system.cpp` | → `ecs/types/render.h` (GPU-layout struct) |
| `MeshAssets` | `ecs/factories.h` | → `ecs/types/factories.h` (or keep beside factories) |
| `Input` | `harness/headless_main.cpp` | → `harness/types/input.h` (test-driver struct) |
| `Ray`, `RayHit` | `physics/raycast.h` | → `physics/types/ray.h` |
| `AABB` | `physics/aabb.h` | already a type-only header — move under `physics/types/` |
| `SystemPhase` | `ecs/systems/system_phase.h` | standalone enum → `ecs/types/` |
| `Level`/`Sector`/`Surface`/`Portal`/`LevelEntity` | `level/level.h` | already a cohesive types header — move to `level/types/level.h` or leave + allowlist |
| `SweepResult` (`collision.h`) | dead | leave — file is legacy/dead (Plan 06 decides delete) |

**Stays put (component-contract enums, per Plan 01 §2):** `WeaponType`, `FireMode`
(in `components/combat.h`); `MoverState`, `TriggerAction` (in `components/gameplay.h`).

## Steps

1. Create `types/` folders: `ecs/types/`, `physics/types/`, `level/types/`,
   `harness/types/`, each with a barrel header.
2. Move one type at a time: cut the declaration into its `types/` leaf, leave the
   original file `#include`-ing the new leaf (so consumers are unaffected initially),
   then update direct consumers to include the leaf.
3. Update `CMakeLists.txt` only if a moved type had an associated `.cpp` (most are
   header-only — no build-list change).
4. Add the `types/` allowlist entries to the Plan 02 `check_type_locations` config.

## Risks

- Low behavioural risk (types are passive). The real risk is **include breakage** —
  a moved struct changes paths for its consumers. Mitigated by the "leave a
  re-include behind first" step and a build after each move.
- `level.h` is borderline (it's already a clean types header). Decision: move to
  `level/types/level.h` for consistency, **or** allowlist it in place. Recommend
  moving for uniformity; flag as a 1-line decision.

## Verification

Build + 6 scenarios after each batch of moves (the harness `Input` move is exercised
directly by every scenario; the others are compile-only checks).

## Done when

`check_type_locations` reports zero findings outside the allowlist, build is clean,
all 6 scenarios pass.
