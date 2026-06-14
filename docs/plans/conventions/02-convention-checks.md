# Plan 02 — C++-Aware Convention Checks

**Status: PROPOSED.** Build the automated checks that enforce [Plan 01](01-coding-standard.md),
adapted from `wyrdwars/scripts/checks` to C++ realities (`.h`/`.cpp` pairs, classes
as a unit, header-include discipline). Ships **report-only**; the blocking flip is
Plan 07.

## Open decision — implementation language

The WyrdWars checks are TypeScript (Node). QEngine has **no Node toolchain**.

| Option | Pros | Cons |
|--------|------|------|
| **Python (stdlib) — recommended** | already on the box; zero deps; regex/line-scan is enough; cross-platform | not the same code as WyrdWars |
| Node/TS | parity with WyrdWars checks; reuse logic | adds a Node devDependency to a C++ repo |
| Compiled C++ tool | no external runtime | heaviest to write/maintain |

**Recommendation: Python**, in `scripts/checks/` with a `run_all.py`. Resolve before
starting. (Heuristic line-scanners, like WyrdWars', not a real C++ parser — good
enough and matches their approach.)

## Checks to build

Each mirrors a WyrdWars check, adapted. All skip `extern/`, `build/`, generated dirs.

1. **check_file_sizes** — per-area caps from a config (see below). Skip barrels,
   `components/**`, `types/**`. Mirrors `file-size-rules.ts`.
2. **check_single_function** — for free-function `.cpp` files, count top-level
   function *definitions*; >1 → flag. **Class member definitions are exempt**
   (a class may have many methods; that's "one class"). Detect "is this a class
   impl file" by matching `Type::method` definitions.
3. **check_type_locations** — flag `struct` / `enum` / `enum class` / `using X =`
   declared **outside** a `types/` folder, with an **allowlist** for the component
   group headers (`components/**`) and any sanctioned exceptions (`level.h` until
   moved). Mirrors `check-type-locations.ts` (which targets `interface`/`type`).
4. **check_header_discipline** (NEW, C++-specific) — a `.h` that `#include`s an
   umbrella **barrel** (e.g. `components.h`) is flagged; suggest a leaf include or a
   forward declaration. This is the rule that protects incremental builds.
5. **check_barrel_shape** — a barrel header (`<folder>.h` matching the folder name,
   or files that are pure `#include` lists) must contain **only** `#include`s /
   comments (no definitions), and barrel nesting must not exceed **depth 2**. Mirrors
   `check-barrel-nesting.ts`.
6. **check_no_default_includes_of_dead** (small) — flag any live file including a
   banner-marked legacy header (`collision.h`, `spatial_hash.h`) so dead code can't
   silently come back. (Optional, cheap.)

Deferred / stretch (harder in C++, not needed for v1):
- unused-symbol detection → lean on compiler `-Wunused` + a future include-what-you-use
  pass, not a hand-rolled check.

## Config

`scripts/checks/rules.{py,json}` holding: per-area size caps (from Plan 01 §4),
`types/` allowlist, barrel-name pattern, skip globs. One edit point, mirrors
`file-size-rules.ts`.

## Runner & output

- `run_all.py` runs every check, prints findings grouped by file (WyrdWars' format:
  `path` then `⚠ detail` lines), exits non-zero **only in `--strict` mode** (off until
  Plan 07).
- A `--report` default mode prints a summary table (violations per check) and exits 0.

## Wiring (prepared here, activated in Plan 07)

- Local: a pre-commit hook (or a `cmake` custom target `make checks`) that runs
  `run_all.py --report`.
- CI: a job in the existing [`.github/workflows/code-quality.yml`](../../../.github/workflows/code-quality.yml)
  running the checks (report-only first).

## Risks

- **False positives** from heuristic scanning (e.g. a `struct` inside a function, or
  templates). Mitigate with the same line-level guards WyrdWars uses (skip `//`,
  brace-depth tracking) + an allowlist. Tune against the current tree before enforcing.
- Checks must run on **Windows + CI Linux** — keep them stdlib-only and path-normalised.

## Verification

- Run against the current `src/` and hand-review the findings: every flagged item
  should be a *real* convention gap or a known exception (then allowlist it). The
  check is "correct" when its report matches a manual audit of a sample folder.

## Done when

- All six checks run via `run_all.py --report` on `src/`, output reviewed, exceptions
  allowlisted, and the findings list becomes the concrete backlog for Plans 03–06.
