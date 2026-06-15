# Plan 08 — Systems Domain-Folder Grouping (addendum)

**Status: 🟧 IN PROGRESS — files moved, checks green, build verification pending.**
Added 2026-06-15 after the bundle's first completion, on request: group
prefix-sharing systems into domain folders (the wyrdwars `core/<domain>/` pattern),
extending what plans 04b already did for `combat/` and `debug_hud/`.

## Decision

Most "prefix groups" are just a system's `.h`+`.cpp` pair (one system), not
multiple related systems. The genuine multi-system domains are `player` and
`mover`/`sync`. The chosen layout (the "aggressive" option):

| Folder | Systems moved in |
|--------|------------------|
| `systems/player/` | `player_character_system`, `init_player_character`, `player_input_system`, `player_death_system`, `camera_follow_system` |
| `systems/mover/` | `mover_system` |
| `systems/sync/` | `mover_sync_system`, `jolt_sync_system` (both ECS↔Jolt sync) |
| `systems/combat/` | + `weapon_switch_system` (joins the existing combat split) |
| _flat (unchanged)_ | `render_system`, `trigger_system`, `lifetime_system`, `demo_reset_system` |

Note: `combat/` and `debug_hud/` hold **one system split into parts** (private
`*_internal.h`); `player/`/`sync/` hold **several independent public systems** —
same "group by domain" idea, slightly different meaning. Both match wyrdwars.

## Done

- All files moved into the folders above (plain `mv`; git detects the renames).
- All `#include` paths updated across `src/` (self-includes + `simulation.cpp` +
  `main.cpp`) — no old-path includes remain.
- `CMakeLists.txt` source paths updated to the new locations.
- **Convention checks: `run_all.py --strict` → 0 findings** (grouping doesn't change
  file sizes / types / header discipline).

## NOT yet verified — resume here

- **Build is currently failing** with `ranlib.exe: libqengine_lib.a: file truncated`
  during the archive step. This is **not** a code error — it is the build
  environment truncating/locking the freshly-written static library (consistent with
  Windows Defender real-time scanning the `.a` while `ranlib` reads it). The objects
  compile cleanly; only the archive step trips.
  - **To finish:** retry `cmake --build build --parallel 1` (often succeeds once the
    AV scan releases the file), or exclude the `build/` dir from real-time scanning,
    or `rm -rf build && cmake -B build && cmake --build build --parallel 1` for a
    guaranteed-clean archive.
- **Headless scenarios** last passed on the *pre-grouping* binary; re-run all 6 once
  the build links to confirm (grouping is pure relocation, so behaviour is expected
  identical — but verify).

## Build gotcha (recurring this session)

Parallel `cmake --build` intermittently corrupts `libqengine_lib.a`
("file truncated") after large source-list changes; serial (`--parallel 1`) avoids
the race, and a clean `build/` clears any already-corrupt archive.
