# Chapter 17d: Enforcement & Verification

## What You'll Learn
- Header discipline: why a `.h` must never pull the umbrella barrel `ecs/components.h`
- The split decision: free-function collections split, engine classes stay whole
- Flipping the convention checks from report-only to blocking with `--strict`
- Installing the opt-in local pre-commit hook (`core.hooksPath`)
- The CI `conventions` job that runs on every PR
- Proving the restructure changed nothing — the 6-scenario headless regression suite
- The before/after source tree, and the build gotcha that bites a large source-list change

---

Previously (17a–c) we wrote the standard, built the report-only checker, and restructured every file. This final part makes the rules binding and proves behaviour is unchanged.

By the end of 17c the tree was compliant in shape: long systems were split into domain folders, incidental types lived in `types/`, and `run_all.py --report` printed a short list of findings shrinking toward zero. But a *report* changes nothing on its own — anyone could commit a 500-line system or a header that drags the whole component model into every translation unit, and nothing would stop them. This part closes that gap: it pins down the two rules that are easy to get wrong (header includes and the split decision), turns the checker into a gate, wires that gate into both the commit flow and CI, and then runs the regression suite to confirm the entire 17a–c restructure was pure relocation.

---

## Step 1: Header discipline — the compile-cost rule

### Why headers must not include the umbrella barrel

`src/engine/ecs/components.h` is the **umbrella barrel** for the ECS component model — it `#include`s every component group (`core.h`, `physics.h`, `combat.h`, `gameplay.h`, `rendering.h`, `tags.h`). It is enormously convenient in a `.cpp`: one include and every component type is in scope.

It is a trap in a `.h`.

A header is pulled into *every* translation unit that includes it, transitively. If a header includes the umbrella barrel, then every `.cpp` that uses that header — and every header that includes *that* header — now transitively depends on the entire component model. Touch one component and the compiler rebuilds everything downstream. That is the incremental-rebuild cascade the standard exists to stop (CODING_STANDARD §3):

> **Headers are strict:** a `.h` includes a **specific leaf**, or **forward-declares** (`struct Foo;`) when only the name is needed — **never the umbrella barrel.** This is what stops incremental-rebuild cascades from spreading.
>
> **`.cpp` files may be relaxed:** including a barrel in a `.cpp` only costs that TU.

This is the rule the `UMBRELLA_BARRELS` check enforces. From `scripts/checks/rules.py`:

```python
# Umbrella barrels that headers must not pull in (CODING_STANDARD §3). A .h that
# includes one of these should use a specific leaf or a forward declaration.
UMBRELLA_BARRELS = [
    "engine/ecs/components.h",
]
```

And the scanner that acts on it, from `scripts/checks/run_all.py`:

```python
def check_header_discipline():
    findings = []
    for rel in iter_source_files():
        if not rel.endswith((".h", ".hpp")) or is_barrel(rel):
            continue
        if matches_any(rel, rules.SKIP_GLOBS):
            continue
        for ln, line in code_lines(rel):
            m = INCLUDE_RE.match(line)
            if m and any(m.group(1).endswith(b) for b in rules.UMBRELLA_BARRELS):
                findings.append(Finding("header-discipline", rel, ln,
                                        f'header includes umbrella barrel "{m.group(1)}" '
                                        f"-> use a specific leaf or forward-declare"))
    return findings
```

Note the check only looks at `.h`/`.hpp` files (`.cpp` is allowed the barrel), and it skips pure barrels themselves (a barrel including leaves is the whole point of a barrel).

### Before / after: a concrete include

Suppose a header for a system needs only the `Health` component name in a function signature. The lazy version drags in the whole model:

```cpp
─── BEFORE: header pulls the umbrella barrel ──────────────────
#pragma once

#include <entt/entt.hpp>
#include "engine/ecs/components.h"   // ← every component, into every TU downstream

void healSystem(entt::registry& registry, entt::entity target);
```

The fix depends on what the header actually needs:

- If it needs the **layout** of `Health` (it stores one by value, or accesses a field), include the **specific leaf** that defines it.
- If it needs only the **name** (a pointer/reference parameter, an `entt::entity`), **forward-declare** and include nothing.

The system above takes only a registry and an entity — it names no component at all in the header, so it needs neither:

```cpp
─── AFTER: header includes only what the signature needs ──────
#pragma once

#include <entt/entt.hpp>

void healSystem(entt::registry& registry, entt::entity target);
```

The barrel — and the components themselves — move down into `heal_system.cpp`, where the cost is confined to that one translation unit:

```cpp
─── heal_system.cpp: the barrel is fine here ──────────────────
#include "engine/ecs/systems/heal_system.h"
#include "engine/ecs/components.h"   // relaxed in .cpp — costs only this TU

void healSystem(entt::registry& registry, entt::entity target)
{
    auto& health = registry.get<Health>(target);
    health.current = health.max;
}
```

Every real system header in the restructured tree follows this shape. Look at `src/engine/ecs/systems/player/player_death_system.h`:

```cpp
#pragma once

#include <entt/entt.hpp>

void playerDeathSystem(entt::registry& registry);
```

No component include at all. The component types appear only in `player_death_system.cpp`. That is the rule, applied: the header tells you *what the system is*, the `.cpp` carries the dependency weight.

---

## Step 2: Split free functions, keep classes whole

### Why the split target is free functions, not classes

17a–c split two large files — `combat_system` (524 lines) and `debug_hud_system` (433 lines) — into domain folders of small one-function files. It is tempting to read that as "split anything large." That is the wrong lesson, and the standard is explicit about it (CODING_STANDARD §1):

> - **Free functions** (ECS systems, factories, jolt/body helpers, core/level free functions): **one public function per `.h`/`.cpp` pair.** Small `static` helpers private to that function may share the `.cpp`.
> - **Classes** (`Shader`, `Mesh`, `Texture`, `Camera`, `Window`, `InputManager`, `ResourceManager`, `FixedTimestep`, `JoltWorld`): **one class per `.h`/`.cpp` pair**, kept whole — the declaration is never split across headers.

The things we split were **collections of free functions** that had accreted into one file. `combat_system.cpp` was hitscan firing, projectile firing, projectile update, splash damage, spread, tracer spawning, entity raycasting, AABB-vs-level — eight independent free functions sharing a file. Splitting them is just giving each its own `.h`/`.cpp` and a barrel, which is the standard's default state. Plan 08 confirms it directly:

> ### Classes are NOT split (decision)
>
> Engine classes (`Shader`, `Mesh`, `Texture`, `Camera`, `Window`, `InputManager`, `ResourceManager`, `FixedTimestep`) are each **one `.h`/`.cpp`, kept whole** — verified that no class was ever split across files. The big things split (`combat_system` 524, `debug_hud_system` 433) were collections of **free functions**, not classes.

You can see both halves of this decision in the final tree. The split free-function collection:

```
─── src/engine/ecs/systems/combat/  (free functions, one per file) ──
combat/apply_spread.cpp
combat/box_hits_level.cpp
combat/combat_internal.h
combat/combat_system.cpp        ← public entry point
combat/combat_system.h          ← the one public header
combat/fire_hitscan.cpp
combat/fire_projectile.cpp
combat/raycast_entities.cpp
combat/spawn_tracer.cpp
combat/splash_damage.cpp
combat/update_projectiles.cpp
combat/weapon_switch_system.h
```

The kept-whole classes — each one `.h` + one `.cpp`, untouched by the split:

```
─── classes stay one-per-pair ────────────────────────────────
core/window.{h,cpp}
core/input_manager.{h,cpp}
core/resource_manager.{h,cpp}
core/fixed_timestep.h
physics/jolt_world.{h,cpp}
```

### The C++-native escape hatch for a genuinely long class

What if a *class* legitimately grows past the 200-line `.cpp` cap? You do **not** split the class — its declaration must never be split across headers. Instead you split its **`.cpp` by concern, keeping the single header** (CODING_STANDARD §1):

> **A long class `.cpp` splits by concern, keeping the single header.** Example: `shader.cpp` → `shader_compile.cpp` + `shader_uniforms.cpp`, both implementing `Shader`. This is free — consumers' header is unchanged, nothing cascades.

Both `.cpp` files `#include "shader.h"` and define `Shader::` methods; consumers still include the one `shader.h` and never know it happened. This is why the single-function checker exempts class-impl files — it sees a `Type::method(` definition and steps aside, because a class may have many methods spread across many `.cpp` files. From `run_all.py`:

```python
def check_single_function():
    """Heuristic: a free-function .cpp should define one top-level function.

    Class-impl files (any `Type::method(` definition) are exempt — a class may
    have many methods. Handles both `ret name(` and `ret name` + `(` on next line.
    """
```

So the decision tree is: **free-function collection → one function per file; god-class → decompose into smaller classes; merely long class → split the `.cpp` by concern, one header.** The restructure only ever did the first.

---

## Step 3: Flip the checks to blocking

### Why a report is not enforcement

`run_all.py` has always had two modes. In report mode it prints the findings and exits 0 — useful while driving the restructure, useless as a gate, because nothing fails. The closing step (Plan 07 §1) is to make `--strict` exit non-zero on any finding:

> 1. **Flip to strict.** Make `run_all.py --strict` exit non-zero on any non-allowlisted finding. Keep the allowlist file as the single escape hatch (each entry commented with *why*).

The mechanism is the last few lines of `main()` in `run_all.py`:

```python
    total = len(all_findings)
    print(f"  TOTAL: {total} finding(s)\n")
    if args.strict and total:
        sys.exit(1)
```

Without `--strict`, the function returns normally (exit 0) regardless of `total`. With `--strict`, a non-zero total is a non-zero exit — which is all a hook or a CI job needs to fail. The two invocations the script documents in its own header:

```python
    python scripts/checks/run_all.py            # report mode (always exit 0)
    python scripts/checks/run_all.py --strict   # exit 1 on any finding (CI/hook)
```

The "non-allowlisted" qualifier is carried entirely by `rules.py`: `SKIP_GLOBS` (files never checked — third-party, generated, banner-dead code) and the per-check allowlists (`TYPE_LOCATION_ALLOWLIST`, `DEAD_HEADERS`). Each allowlist entry is commented with *why* it is exempt, so the escape hatch stays honest. The end state, confirmed by Plan 08's verification line, is **0 findings** — strict mode passes clean on a fresh checkout, so flipping it on blocks nothing legitimate and everything new that drifts.

---

## Step 4: The local pre-commit hook

### Why opt-in, and what it runs

The fastest feedback loop is at commit time, before anything reaches CI. The hook runs the exact same `run_all.py --strict` the CI job runs, so a clean local commit is a clean CI run (Plan 07 §2). It is **opt-in**: the repo ships the hook script under `scripts/githooks/`, but Git does not use it until you point `core.hooksPath` at that directory. This keeps the repo from imposing a hook on contributors who have their own setup, while making it a one-line install for those who want it.

Reproduce `scripts/githooks/pre-commit` in full:

```bash
#!/bin/sh
# QEngine pre-commit hook — file-convention checks
# (docs/architecture/CODING_STANDARD.md). Opt in with:
#
#     git config core.hooksPath scripts/githooks
#
# Runs the same checks as CI. Requires python on PATH.

python scripts/checks/run_all.py --strict
status=$?
if [ "$status" -ne 0 ]; then
    echo "" >&2
    echo "Pre-commit blocked: file-convention violations (above)." >&2
    echo "Fix them, or run 'python scripts/checks/run_all.py' to inspect in report mode." >&2
    exit 1
fi
```

It captures the strict exit code, and on failure prints a pointer to report mode (where you can read the findings without the commit being blocked) before exiting 1 — which aborts the commit.

Install it once per clone:

```bash
git config core.hooksPath scripts/githooks
```

`core.hooksPath` redirects Git from its default `.git/hooks/` to the tracked `scripts/githooks/` directory, so the hook is version-controlled with the rest of the project rather than living in each developer's untracked `.git/`. From then on every `git commit` runs the checks first; a violation blocks the commit until you fix it (or, in report mode, inspect it). The hook requires `python` on `PATH`.

---

## Step 5: The CI gate

### Why CI is the backstop

The local hook is opt-in and can be skipped (`--no-verify`); CI cannot be. The CI gate is the non-negotiable backstop — it runs `run_all.py --strict` on every PR, so no drift reaches a protected branch regardless of anyone's local setup (Plan 07 §3). It joins the existing branch-flow guard in the same workflow.

Reproduce the `conventions` job from `.github/workflows/code-quality.yml` in full (with the surrounding header and the branch-flow guard it sits beside, for context):

```yaml
name: Code Quality

on:
  pull_request:
    branches: [main, master, dev]
  workflow_dispatch:

jobs:
  # Branch-flow guard: only `dev` is allowed to merge into a production branch
  # (master/main). PRs into master/main from any other branch fail. PRs into
  # `dev` skip the enforcing step (the `if` is false) and pass.
  guard-branch-flow:
    runs-on: ubuntu-latest
    steps:
      - name: Only `dev` may merge into master
        if: github.base_ref == 'master' || github.base_ref == 'main'
        run: |
          if [ "${{ github.head_ref }}" != "dev" ]; then
            echo "::error::PRs into '${{ github.base_ref }}' must come from 'dev', but this one is from '${{ github.head_ref }}'. Merge your branch into 'dev' first, then open a 'dev → ${{ github.base_ref }}' PR."
            exit 1
          fi
          echo "OK: '${{ github.head_ref }}' → '${{ github.base_ref }}' is an allowed merge."

  # File-convention checks (docs/architecture/CODING_STANDARD.md): one-thing-per-file,
  # types/ folders, barrel/header discipline, size caps. Heuristic Python scanners
  # in scripts/checks/; --strict fails the job on any non-allowlisted finding.
  conventions:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-python@v5
        with:
          python-version: '3.x'
      - name: Run convention checks
        run: python scripts/checks/run_all.py --strict
```

The job is deliberately minimal: check out, get Python, run the strict checker. The scanners are stdlib-only, so there are no dependencies to install. Because the checks are heuristic line scanners (not a C++ compiler), the job is fast and needs no toolchain — it runs the same command as the local hook, on Linux, on every PR into `main`/`master`/`dev`. When branch protection is configured, mark it a **required status check** so a red `conventions` job blocks the merge (Plan 07 §3).

---

## Step 6: Verify behaviour is unchanged

### Why a regression suite, not a code review

The entire 17a–c restructure was **pure relocation** — files moved, includes re-pointed, large `.cpp` files broken into one-function `.cpp` files, the same code compiled from new paths. No logic changed. But "no logic changed" is a claim, and the only honest way to back it is to run the game and check the observable behaviour is byte-identical. That is what the headless regression suite is for (CODING_STANDARD §6: "Runtime behaviour (protected by the headless regression suite)"). Plan 08's verification states the expectation plainly: "All 6 headless scenarios pass (identical to pre-grouping — it's pure relocation)."

Run the verify block from CODING_STANDARD.md, with the convention report appended:

```bash
export PATH="/c/msys64/ucrt64/bin:$PATH"   # required — the compiler needs it on PATH
cmake --build build
for s in rest_no_jitter ride_lift_up walk_onto_lift walk_floor_seams \
         rocket_vs_floor teleporter; do ./build/QEngineHeadless.exe "$s" | grep -E "PASS|FAIL"; done
python scripts/checks/run_all.py --report
```

The first line is not optional: the MSYS2 UCRT64 toolchain must be on `PATH` or `cmake --build` fails to find the compiler. The loop runs all six headless scenarios and greps each one's output for its `PASS`/`FAIL` line:

```
─── expected ─────────────────────────────────────────────────
rest_no_jitter      PASS
ride_lift_up        PASS
walk_onto_lift      PASS
walk_floor_seams    PASS
rocket_vs_floor     PASS
teleporter          PASS
```

All 6 scenarios pass identically to their pre-restructure results — resting bodies still don't jitter, the player still rides the lift, walks onto it, crosses floor seams without snagging, rockets still resolve against the floor, and the teleporter still teleports. The trailing `run_all.py --report` confirms the tree is convention-clean (0 findings) at the same time.

### The build gotcha

A large source-list change is exactly the situation that trips the archive race. From Plan 08:

> Parallel `cmake --build` intermittently corrupts `libqengine_lib.a` ("file truncated") after large source-list changes; serial (`--parallel 1`) avoids the race, and a clean `build/` clears any already-corrupt archive.

If the build dies with `ranlib: libqengine_lib.a: file truncated`, the static archive was corrupted by a parallel write race while many newly-split `.cpp` files were being archived at once. The fix is two-fold:

```bash
─── recover from a corrupt archive ───────────────────────────
# 1. clear the corrupt archive
rm -rf build/CMakeFiles/qengine_lib.dir
# 2. rebuild serially to avoid the race
cmake --build build --parallel 1
```

Use `--parallel 1` whenever you have just changed the source list significantly; once the archive is healthy again, parallel builds are fine.

---

## Step 7: Before & after tree

### Why show the whole shape

The point of 17a–c is best seen at a glance: a flat `systems/` directory with two oversized monster files becomes a set of small domain folders, each a barrel plus one-function leaves, with incidental types pulled into `types/` and domain files relocated to the folder that owns them.

```
─── BEFORE: flat systems/, two monsters ──────────────────────
src/engine/
├── app/
│   ├── scene_setup.{h,cpp}
│   ├── simulation.{h,cpp}
│   └── factories.{h,cpp}            ← level construction, in app/
├── core/
│   ├── fixed_timestep.h
│   ├── input_manager.{h,cpp}
│   ├── resource_manager.{h,cpp}
│   └── window.{h,cpp}
├── ecs/
│   ├── components.h                 ← umbrella barrel
│   └── systems/                     ← FLAT: every system a loose file
│       ├── combat_system.{h,cpp}    ← 524 lines (8 free functions)
│       ├── debug_hud_system.{h,cpp} ← 433 lines (text/bar/crosshair/flash)
│       ├── weapon_switch_system.{h,cpp}
│       ├── player_character_system.{h,cpp}
│       ├── player_input_system.{h,cpp}
│       ├── camera_follow_system.{h,cpp}
│       ├── player_death_system.{h,cpp}
│       ├── mover_system.{h,cpp}
│       ├── mover_sync_system.{h,cpp}
│       ├── jolt_sync_system.{h,cpp}
│       ├── render_system.{h,cpp}
│       ├── trigger_system.{h,cpp}
│       ├── lifetime_system.{h,cpp}
│       └── demo_reset_system.{h,cpp}
├── level/
│   └── level.h
└── physics/
    ├── jolt_world.{h,cpp}
    ├── raycast.{h,cpp}              ← two free functions in one file
    └── physics_config.h
```

```
─── AFTER: domain folders, types/, relocations ───────────────
src/engine/
├── app/
│   ├── scene_setup.{h,cpp}          ← orchestrator stays in app/
│   └── simulation.{h,cpp}
├── core/                            ← classes, each one .h/.cpp (unchanged)
│   ├── fixed_timestep.h
│   ├── input_manager.{h,cpp}
│   ├── resource_manager.{h,cpp}
│   └── window.{h,cpp}
├── ecs/
│   ├── components.h                 ← umbrella barrel (skipped by checks)
│   ├── components/                  ← {core,physics,combat,gameplay,rendering,tags}.h
│   ├── types/                       ← entity_hit.h, mesh_assets.h, system_phase.h
│   └── systems/                     ← every system in a domain folder
│       ├── combat/                  ← 524-line monster, split one-fn-per-file
│       │   ├── combat_system.{h,cpp}, combat_internal.h
│       │   ├── apply_spread.cpp, box_hits_level.cpp
│       │   ├── fire_hitscan.cpp, fire_projectile.cpp
│       │   ├── raycast_entities.cpp, spawn_tracer.cpp
│       │   ├── splash_damage.cpp, update_projectiles.cpp
│       │   └── weapon_switch_system.h
│       ├── debug_hud/               ← 433-line monster, split one-fn-per-file
│       │   ├── debug_hud_system.{h,cpp}, debug_hud_internal.h
│       │   ├── draw_text.cpp, draw_bar.cpp, draw_crosshair.cpp
│       │   ├── draw_ammo.cpp, draw_flash_overlay.cpp
│       ├── player/                  ← multi-system domain
│       │   ├── player_character_system.{h,cpp}, init_player_character.cpp
│       │   ├── player_input_system.{h,cpp}
│       │   ├── player_death_system.{h,cpp}
│       │   └── camera_follow_system.{h,cpp}
│       ├── mover/   → mover_system.{h,cpp}
│       ├── sync/    → mover_sync_system.{h,cpp}, jolt_sync_system.{h,cpp}
│       ├── render/  → render_system.{h,cpp}
│       ├── trigger/ → trigger_system.{h,cpp}
│       ├── lifetime/→ lifetime_system.{h,cpp}
│       └── demo/    → demo_reset_system.{h,cpp}
├── level/
│   ├── level.h
│   ├── factories.{h,cpp}            ← relocated from app/ (owns its domain)
│   ├── build_sector_meshes.{h,cpp}
│   └── showcase_level.{h,cpp}
└── physics/
    ├── jolt_world.{h,cpp}           ← class, kept whole
    ├── physics_config.h
    ├── bodies/                      ← create_{static,dynamic,kinematic,sensor,level}_body.cpp
    └── raycast/                     ← ray_intersect_aabb.cpp + ray_intersect_triangle.cpp
```

Single-file folders (`mover/`, `render/`, `trigger/`, `lifetime/`, `demo/`) hold one system each — chosen for full consistency with the domain-folder pattern even where there is only one sibling. The shape is now self-describing: the folder names *are* the domain map.

---

## Summary

| Change | What it does |
|--------|--------------|
| Header discipline rule | A `.h` includes a specific leaf or forward-declares; never the umbrella `ecs/components.h`. `.cpp` may relax. Enforced by `UMBRELLA_BARRELS` / `check_header_discipline`. |
| Split decision | Free-function collections (combat 524, debug_hud 433) split one-fn-per-file; engine classes stay one whole `.h`/`.cpp`. A long class splits its `.cpp` by concern, keeping the single header. |
| `--strict` flip | `run_all.py --strict` exits 1 on any non-allowlisted finding (`sys.exit(1)`); end state is 0 findings. |
| Pre-commit hook | `scripts/githooks/pre-commit` runs the strict checker; opt in with `git config core.hooksPath scripts/githooks`. |
| CI gate | The `conventions` job in `.github/workflows/code-quality.yml` runs `run_all.py --strict` on every PR. |
| Regression verify | All 6 headless scenarios pass identically — the restructure is pure relocation. |
| Build gotcha | Parallel `cmake --build` can truncate `libqengine_lib.a` after a large source-list change; use `--parallel 1` and clean `build/`. |

With the checks blocking locally and in CI, and the regression suite green, the convention is self-sustaining: new code cannot drift without a red check, and the restructure is proven to have changed nothing about how the game runs.

---

## What's Next

Chapter 17 set out to make the codebase's *shape* a first-class, enforced property — and it now is. 17a wrote the standard, 17b built the report-only checker, 17c restructured every file to satisfy it, and this part (17d) made the rules binding and proved behaviour unchanged. The convention is finished and self-enforcing; there is no more structural work to do here.

The next body of work is a real feature, and a parked draft for it already exists. Under `docs/Game Learning/Unplaced Tutorials_2/` there are four draft chapters numbered 17–20 there — **Map Parser, Entity Mapping, Brush Collision, and TrenchBroom Config** — which together describe **TrenchBroom `.map` loading**: replacing the hardcoded showcase level with maps authored in a real level editor, parsing the `.map` format, mapping its entities onto our ECS components, deriving brush collision, and configuring TrenchBroom to know about our entity set.

That is the candidate next arc — but it is a **scope decision**, not a foregone conclusion. Those drafts predate the restructure and will need re-numbering and a pass against the now-enforced conventions before they are followed. Treat them as a parked proposal: read them, decide whether `.map` loading is the right next investment, and only then renumber and fold them into this tutorial series.
