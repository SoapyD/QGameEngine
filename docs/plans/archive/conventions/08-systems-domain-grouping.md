# Plan 08 — Systems Domain-Folder Grouping (addendum)

**Status: ✅ COMPLETE — verified 2026-06-15.** Build clean, all 6 headless scenarios
pass, `run_all.py --strict` reports 0 findings. Added after the bundle's first
completion, on request: group prefix-sharing systems into domain folders (the
wyrdwars `core/<domain>/` pattern), extending what plans 04b did for `combat/` and
`debug_hud/`.

## Decision

Most "prefix groups" are just a system's `.h`+`.cpp` pair (one system), not
multiple related systems. The genuine multi-system domains are `player` and
`mover`/`sync`. The chosen layout (the "aggressive" option):

Every system now lives in a domain folder (no loose files under `systems/`):

| Folder | Systems moved in |
|--------|------------------|
| `systems/player/` | `player_character_system`, `init_player_character`, `player_input_system`, `player_death_system`, `camera_follow_system` |
| `systems/mover/` | `mover_system` |
| `systems/sync/` | `mover_sync_system`, `jolt_sync_system` (both ECS↔Jolt sync) |
| `systems/combat/` | + `weapon_switch_system` (joins the existing combat split) |
| `systems/render/` | `render_system` |
| `systems/trigger/` | `trigger_system` |
| `systems/lifetime/` | `lifetime_system` |
| `systems/demo/` | `demo_reset_system` |

`render`/`trigger`/`lifetime`/`demo` are single-file folders (no siblings) — chosen
for full consistency with the wyrdwars `core/<domain>/` pattern.

Also done in this round:
- **`factories.{h,cpp}` → `level/`** (level-entity construction; `scene_setup`, the
  orchestrator that calls them, stays in `app/`). The factory size cap (150) follows
  the file via a `**/factories*.cpp` rule.
- **`physics/raycast.cpp` split** → `physics/raycast/ray_intersect_aabb.cpp` +
  `ray_intersect_triangle.cpp` (two free functions, one per file); `raycast.h` keeps
  the declarations.

### Classes are NOT split (decision)

Engine classes (`Shader`, `Mesh`, `Texture`, `Camera`, `Window`, `InputManager`,
`ResourceManager`, `FixedTimestep`) are each **one `.h`/`.cpp`, kept whole** — verified
that no class was ever split across files. The big things split (`combat_system` 524,
`debug_hud_system` 433) were collections of **free functions**, not classes. Per
CODING_STANDARD §1 the "one per file" rule targets free functions; a class's methods
stay together. (Open to revisiting if class-method-splitting is ever wanted.)

Note: `combat/` and `debug_hud/` hold **one system split into parts** (private
`*_internal.h`); the others hold **independent public systems** — same "group by
domain" idea. Both match wyrdwars.

## Done

- All files moved into the folders above (plain `mv`; git detects the renames).
- All `#include` paths updated across `src/` (self-includes + `simulation.cpp` +
  `main.cpp`) — no old-path includes remain.
- `CMakeLists.txt` source paths updated to the new locations.
- **Convention checks: `run_all.py --strict` → 0 findings** (grouping doesn't change
  file sizes / types / header discipline).

## Verification

- Build clean (`cmake --build build --parallel 1`, exit 0).
- All 6 headless scenarios pass (identical to pre-grouping — it's pure relocation).
- `run_all.py --strict` → 0 findings.

The build initially tripped on `ranlib: libqengine_lib.a: file truncated` (the
archive gotcha below); it cleared on retry after a clean `build/CMakeFiles/qengine_lib.dir`.

## Build gotcha (recurring this session)

Parallel `cmake --build` intermittently corrupts `libqengine_lib.a`
("file truncated") after large source-list changes; serial (`--parallel 1`) avoids
the race, and a clean `build/` clears any already-corrupt archive.
