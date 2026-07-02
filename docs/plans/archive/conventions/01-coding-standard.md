# Plan 01 — C++ Coding Standard (the spec)

**Status: PROPOSED.** This is the source of truth the checks (Plan 02) enforce and
the folder restructures (03–06) target. Writing it down first makes the rules
reviewable before they're automated. **No code changes in this plan** — it produces
one doc: `docs/architecture/CODING_STANDARD.md` (and this plan is its draft).

## 1. One thing per file

- **Free functions** (ECS systems, factories, jolt helpers, core/math, level helpers):
  **one public function per `.h`/`.cpp` pair.** Internal static helpers in the same
  `.cpp` are allowed if small and private to that function.
- **Classes** (`Shader`, `Mesh`, `Texture`, `Camera`, `Window`, `InputManager`,
  `ResourceManager`, `FixedTimestep`, `JoltWorld`): **one class per `.h`/`.cpp` pair**,
  kept whole. The class declaration is never split across headers.
- **Long class `.cpp` → split by concern**, keeping the single header:
  `shader.cpp` → `shader_compile.cpp` + `shader_uniforms.cpp`, both implementing
  `Shader`. This is free (no barrel, consumers' header is unchanged).
- **God-class smell** (a class doing many unrelated things) → decompose
  responsibilities into smaller classes / free functions, don't just scatter the `.cpp`.

## 2. Types belong in `types/`

- **Incidental types** — param/return structs, small value types, standalone enums and
  `using` aliases — live in a `types/` folder near their domain
  (`ecs/types/`, `physics/types/`, `level/types/`).
- **Exception — ECS components stay in `components/`.** They're the domain model, not
  incidental shapes. Keep the existing `components/{core,physics,combat,…}.h`.
- **Enum placement rule:** an enum that is *part of a component's contract*
  (`WeaponType`, `MoverState`, `TriggerAction`, `FireMode`) stays with that component
  group. A *standalone* enum (`SystemPhase`) moves to `types/`.
- Examples to relocate (from audit): `EntityHit` (combat), `PointLightGPU` (render),
  `MeshAssets` (factories), `Ray`/`RayHit` (physics), `Input` (harness). `level.h` is
  already effectively a types header — may stay as `level/types/level.h` or in place.

## 3. Barrels & includes (the compile-cost rule)

- A **barrel** is a header that only `#include`s a folder's leaf headers (no
  definitions). One barrel per folder, named `<folder>.h` (e.g. `components.h`,
  `systems/combat.h`). Max **nesting depth 2** (folder → subfolder → leaf).
- **Prefer the narrowest include** that covers the file's needs.
- **Headers are strict:** a `.h` must include a **specific leaf**, or **forward-declare**
  (`struct Foo;`) when it only needs the name — **never the umbrella barrel.** This is
  what stops rebuild cascades from spreading.
- **`.cpp` files may be relaxed:** including a barrel in a `.cpp` only costs that TU.
- **Pimpl** is available for compile-firewalling a heavy class, but is likely overkill
  at current scale — note it, don't mandate it.

## 4. File-size caps (smell thresholds)

Coarser than WyrdWars (C++ headers are verbose). Initial proposal — tune during 02:

| Area (glob) | Cap (lines) | Note |
|-------------|-------------|------|
| `engine/ecs/systems/**/*.cpp` (free fn) | 120 | split long systems into a `systems/<domain>/` folder |
| `engine/ecs/factories*.cpp`, core/level free fns | 100 | |
| `engine/**/*.h` (non-barrel, non-types) | 70 | classes' public surface |
| class `.cpp` (`renderer/`, `core/`) | 200 | beyond → split `.cpp` by concern |
| `components/**`, `types/**`, barrels | exempt | data/aggregation, length is fine |
| `extern/**`, `build/**`, tests | exempt | out of scope |

Caps are **reported, not blocking, until Plan 07.** They flag candidates, not failures.

## 5. Folder shape

- Group by domain: `ecs/systems/combat/`, `ecs/types/`, `physics/types/`.
- One barrel header per folder; one `types/` per folder that needs it.
- Files that are misplaced today get relocated (Plan 04): `jolt_body_helpers` →
  `physics/`, `scene_setup` → `app/`, `showcase_level` → `level/` (or a new `scene/`).

## 6. Explicitly NOT changed

- Runtime behaviour (guarded by the headless suite).
- `extern/` and third-party patterns.
- Naming style of existing public APIs unless a move requires it.

## Deliverable & done

- `docs/architecture/CODING_STANDARD.md` written from this plan, linked from
  `docs/architecture/README.md`.
- **Done when:** the standard is reviewed/accepted by the user and Plan 02 can cite it.
