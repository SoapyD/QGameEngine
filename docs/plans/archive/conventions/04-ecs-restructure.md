# Plan 04 — ECS Restructure: Systems, Factories, Relocations

**Status: PROPOSED.** Apply the standard to `ecs/`: split oversized systems into
domain folders, finalise factories, and **relocate the three files that don't belong
in `ecs/`**. Behaviour-preserving; gated by the headless suite.

## A. Split oversized systems into `systems/<domain>/`

Audit current system sizes first (Plan 02 `check_file_sizes`). Known candidates:

- **`combat_system.cpp` (~300 lines)** → `ecs/systems/combat/`:
  - `combat_system.cpp` — the `combatSystem` entry (orchestration only)
  - `fire_hitscan.cpp`, `fire_projectile.cpp` — per fire mode
  - `apply_damage.cpp`, `spawn_tracer.cpp` / `spawn_projectile.cpp` — effects
  - `combat.h` barrel; shared helper types already moved to `ecs/types/combat.h` (Plan 03)
- **`trigger_system.cpp` (~150)** → optionally `systems/trigger/` with the action
  handlers (`activate_mover`, `teleport`, `damage`, `heal`) as one-fn files + entry.
  Only if it exceeds the cap after Plan 03; otherwise leave as one file.
- **`render_system.cpp`, `debug_hud_system.cpp`** — review against the cap; split the
  light-uniform upload out of render if it's long.
- **Small systems stay single-file** (`mover_system`, `mover_sync_system`,
  `jolt_sync_system`, `lifetime_system`, `demo_reset_system`, `weapon_switch_system`,
  `player_input_system`, `camera_follow_system`, `player_death_system`,
  `player_character_system`). Don't split for the sake of it.

Each split: extract into the new folder, add the barrel, update `simulation.cpp` (and
any caller) to include `systems/<domain>.h`, update `CMakeLists.txt`, build, run scenarios.

## B. Factories

`factories.{h,cpp}` is currently one file with ~8 spawn functions. Options:
- **Keep as one file** if under the cap (simplest), or
- Split into `ecs/factories/<spawn_x>.cpp` + `factories.h` barrel if it grows or to
  satisfy one-function-per-file strictly. Recommend: **defer the split** unless the
  size check flags it — factories read well as a cohesive group.

## C. Relocate out of `ecs/`

| File | New home | Rationale |
|------|----------|-----------|
| `ecs/jolt_body_helpers.{h,cpp}` | `physics/` (or `physics/bodies/`) | pure Jolt body creation; physics-domain glue (ECS-aware ≠ ECS-owned) |
| `ecs/scene_setup.{h,cpp}` | `app/` (beside `simulation`) | world bootstrap/orchestration, not a component or system |
| `ecs/showcase_level.{h,cpp}` | `level/` (or a new `scene/`) | hand-authored level content |

Each move: update `#include` paths in all consumers (`simulation.cpp` chiefly),
update `CMakeLists.txt` source paths, build, run scenarios. Note: `jolt_body_helpers`
splitting into one-fn files (`create_static_body.cpp`, …) can fold into this move or
Plan 06 — recommend doing it as part of the relocation.

## Risks

- Highest **mechanical** churn of the bundle (many include-path edits, CMake source
  list changes). Each is low *behavioural* risk — the suite catches any wiring error.
- Splitting `combat_system` touches the most-tested path (rocket/hitscan scenarios) —
  do it in one focused step and verify immediately.

## Verification

Build + all 6 scenarios after **each** of A's splits, B, and **each** relocation in C.
`teleporter` + `rocket_vs_floor` specifically cover the combat/trigger changes.

## Done when

`check_file_sizes` + `check_single_function` report zero findings in `ecs/systems/`,
the three relocations are complete with `ecs/` holding only components/systems/factories/types,
build clean, all 6 scenarios pass.
