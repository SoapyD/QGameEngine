# QEngine — File-Convention Adoption (plan bundle)

**Status: ✅ Plans 01–07 COMPLETE — shipped 2026-06-15** (tree convention-compliant,
`run_all.py` reports 0 findings, build clean, 6 scenarios pass, checks enforced in CI).
**Plan 08 (systems domain grouping) is 🟧 IN PROGRESS** — files moved + checks green,
but the build is paused on a `ranlib: file truncated` archive issue (see
[08-systems-domain-grouping.md](08-systems-domain-grouping.md)). The canonical rules
live in [`CODING_STANDARD.md`](../../../architecture/CODING_STANDARD.md).

Outcome summary:
- `types/` folders for incidental structs/enums (ecs + physics).
- `combat_system` (524 lines) → `systems/combat/`; `debug_hud_system` (433) →
  `systems/debug_hud/` (one function per file + internal header).
- Relocations: `jolt_body_helpers` → `physics/bodies/`, `scene_setup` → `app/`,
  `showcase_level` + `buildSectorMeshes` → `level/`.
- Header discipline: headers use leaf includes / forward decls, never the umbrella.
- Size caps recalibrated to realistic C++ values (CODING_STANDARD §4).
- Dead code (`collision`, `spatial_hash`, `.qlvl` parser, `archived/*`) dropped from
  the build and banner-labelled — **kept, not deleted** (pending an explicit call).
- **(Plan 08, pending build verify)** systems grouped into domain folders:
  `systems/player/`, `systems/mover/`, `systems/sync/`, `weapon_switch` → `combat/`.

## Why

Adopt the WyrdWars "small, single-responsibility, traceable, testable files"
discipline (see `wyrdwars/scripts/checks`) in QEngine, **adapted to C++**. The goal
is readability and testability — *not* mechanical mimicry. C++'s header/`.cpp`
split and compile model mean some WyrdWars rules transfer 1:1, some need adapting,
and one (splitting a class into per-method files) is explicitly rejected in favour
of a cheaper C++-native option.

## Principles agreed (the spec lives in [01](01-coding-standard.md))

- **One thing per file.** Free functions: one per `.h`/`.cpp`. Classes: one class
  per `.h`/`.cpp` — kept whole, but a long class **may split its `.cpp` by concern**
  (cheap, no barrel, header unchanged). Decompose god-classes instead of scattering.
- **Types live in `types/` folders.** Incidental structs / enums / aliases (params,
  returns, small value types) move to a `types/` folder near their domain. **ECS
  components stay first-class in `components/`** — a deliberate exception.
- **Barrels are a convenience front door, not mandatory.** Prefer the narrowest
  include. **Headers must be strict** (a specific leaf, or a forward declaration —
  never the umbrella barrel); `.cpp` files may be relaxed. This is the main lever
  against incremental-rebuild fan-out.
- **Size caps are coarser than WyrdWars** (C++ headers are verbose) and are *smell
  thresholds*, not hard law until enforcement (Plan 07).
- **Behaviour must not change.** Every restructure is file-moving + include/namespace
  edits. The headless suite is the safety net — run it after each move.

## Scope

- **In:** `src/` — `engine/`, `game/`, `app/`, `harness/`, `main.cpp`.
- **Out:** `extern/` (third-party: entt, glfw, glad, glm, stb, Jolt) — never touched.
  Generated/build dirs excluded.

## Sequencing

```
01 coding-standard      → write the rules down (reviewable spec)
02 convention-checks    → C++-aware checks that enforce 01 (report-only first)
03 types-folders        → cross-cutting: establish types/ + move incidental types
04 ecs-restructure      → systems split, factories, relocate misplaced files
05 engine-classes       → renderer/core classes: split long .cpp, header discipline
06 physics-level        → physics free fns, level, retire dead code
07 enforcement-rollout  → flip checks to blocking, wire pre-commit/CI, archive
```

03–06 can proceed in any order once 01/02 land, but 03 (types) first reduces churn
in the others.

## Plan index

| # | Plan | Output |
|---|------|--------|
| 01 | [coding-standard.md](01-coding-standard.md) | The house-style spec the checks enforce |
| 02 | [convention-checks.md](02-convention-checks.md) | C++-aware check scripts + runner |
| 03 | [types-folders.md](03-types-folders.md) | `types/` convention + incidental-type moves |
| 04 | [ecs-restructure.md](04-ecs-restructure.md) | Systems split, factories, relocations |
| 05 | [engine-classes.md](05-engine-classes.md) | Renderer/core class `.cpp` splits + headers |
| 06 | [physics-level.md](06-physics-level.md) | Physics free fns, level, dead-code retirement |
| 07 | [enforcement-rollout.md](07-enforcement-rollout.md) | Blocking checks, CI hook, archive |
| 08 | [systems-domain-grouping.md](08-systems-domain-grouping.md) | Group systems into domain folders (🟧 build verify pending) |

## Verify (every plan)

```bash
export PATH="/c/msys64/ucrt64/bin:$PATH"   # required — see project memory
cmake --build build                         # exit 0
for s in rest_no_jitter ride_lift_up walk_onto_lift walk_floor_seams \
         rocket_vs_floor teleporter; do ./build/QEngineHeadless.exe "$s" | grep -E "PASS|FAIL"; done
```

Archived as a unit (move this dir under `docs/plans/archive/`) once Plan 07 lands.
