# QEngine — Coding Standard

How code is organised in `src/`. The goal is **small, single-responsibility,
traceable, testable files**, adapted from the WyrdWars file conventions to C++'s
header/`.cpp` model. Enforced by `scripts/checks/` (see
[plan bundle](../plans/conventions/README.md) / once shipped, `archive/`).

Applies to `src/` (`engine/`, `game/`, `app/`, `harness/`, `main.cpp`).
**Excludes `extern/`** (third-party) and `build/`.

---

## 1. One thing per file

- **Free functions** (ECS systems, factories, jolt/body helpers, core/level free
  functions): **one public function per `.h`/`.cpp` pair.** Small `static` helpers
  private to that function may share the `.cpp`.
- **Classes** (`Shader`, `Mesh`, `Texture`, `Camera`, `Window`, `InputManager`,
  `ResourceManager`, `FixedTimestep`, `JoltWorld`): **one class per `.h`/`.cpp` pair**,
  kept whole — the declaration is never split across headers.
- **A long class `.cpp` splits by concern, keeping the single header.** Example:
  `shader.cpp` → `shader_compile.cpp` + `shader_uniforms.cpp`, both implementing
  `Shader`. This is free — consumers' header is unchanged, nothing cascades.
- **A god-class** (one class doing many unrelated things) is decomposed into smaller
  classes / free functions, not merely scattered across `.cpp` files.

## 2. Types live in `types/`

- **Incidental types** — param/return structs, small value types, standalone enums,
  `using` aliases — live in a `types/` folder near their domain (`ecs/types/`,
  `physics/types/`, `level/types/`, `harness/types/`).
- **Exception: ECS components stay in `components/`.** They are the domain model, not
  incidental shapes. Keep `components/{core,physics,combat,gameplay,rendering,tags}.h`.
- **Enum placement:** an enum that is part of a component's contract (`WeaponType`,
  `FireMode`, `MoverState`, `TriggerAction`) stays with that component group. A
  standalone enum (`SystemPhase`) goes in `types/`.

## 3. Barrels & includes (the compile-cost rule)

- A **barrel** is a header that only `#include`s a folder's leaves (no definitions).
  One per folder, named `<folder>.h`. Barrel **nesting depth ≤ 2** (folder → subfolder
  → leaf).
- **Prefer the narrowest include** that covers a file's needs.
- **Headers are strict:** a `.h` includes a **specific leaf**, or **forward-declares**
  (`struct Foo;`) when only the name is needed — **never the umbrella barrel.** This is
  what stops incremental-rebuild cascades from spreading.
- **`.cpp` files may be relaxed:** including a barrel in a `.cpp` only costs that TU.
- **Pimpl** is available to compile-firewall a heavy class, but is not mandated at the
  current scale.

## 4. File-size caps (smell thresholds)

Coarser than WyrdWars — C++ headers are verbose. These flag *candidates to split*,
not hard failures (blocking only after the rollout plan).

Recalibrated after the first full report — a cohesive C++ system runs ~150–190
lines (longer than the TS equivalent), so the caps catch genuine monsters
(combat was 524, debug_hud 433) without fragmenting cohesive units.

| Area | Cap (lines) |
|------|-------------|
| `ecs/systems/**/*.cpp` (free fn) | 190 |
| factory / app bootstrap `.cpp` | 150 |
| `level/**/*.cpp` free fn | 100 |
| class `.cpp` (`renderer/`, `core/`, `physics/`) | 200 (beyond → split `.cpp`) |
| non-barrel, non-types `.h` | 100 |
| `components/**`, `types/**`, barrels | exempt |
| `extern/**`, `build/**`, tests, Jolt glue | exempt |

## 5. Folder shape

- Group by domain; one barrel header per folder; one `types/` per folder that needs it.
- Long systems live in `ecs/systems/<domain>/` (entry function + one-function helpers
  + barrel).
- Files live in the folder that owns their domain — e.g. Jolt body creation in
  `physics/`, world bootstrap in `app/`, level content in `level/`.

## 6. Not governed by this standard

- Runtime behaviour (protected by the headless regression suite).
- `extern/` and third-party patterns.
- Public API naming, unless a move requires an update.

---

## Verifying a change

```bash
export PATH="/c/msys64/ucrt64/bin:$PATH"   # required — the compiler needs it on PATH
cmake --build build
for s in rest_no_jitter ride_lift_up walk_onto_lift walk_floor_seams \
         rocket_vs_floor teleporter; do ./build/QEngineHeadless.exe "$s" | grep -E "PASS|FAIL"; done
```

Plus `python scripts/checks/run_all.py --report` for convention compliance.

## Enforcement

- **CI:** the `conventions` job in `.github/workflows/code-quality.yml` runs
  `run_all.py --strict` on every PR — a finding fails the job.
- **Local (opt-in):** `git config core.hooksPath scripts/githooks` installs a
  pre-commit hook running the same checks.
- **Config:** size caps, allowlists, and skip globs live in `scripts/checks/rules.py`
  (one edit point). Allowlist entries should each say *why*.
