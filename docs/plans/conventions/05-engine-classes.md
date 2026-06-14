# Plan 05 — Engine Classes: `.cpp` Splits & Header Discipline

**Status: PROPOSED.** Apply the standard to the stateful classes in `renderer/` and
`core/` (and `physics/jolt_world.h`). The rule (Plan 01 §1): **one class per `.h`/`.cpp`,
kept whole; split a long `.cpp` by concern; never per-method barrels.** Lowest-risk
plan in the bundle — no behaviour change, and `.cpp` splits don't touch consumers.

## Classes in scope

| Class | File | Action |
|-------|------|--------|
| `Shader` | `renderer/shader.{h,cpp}` | likely split `.cpp`: `shader_compile.cpp` + `shader_uniforms.cpp` (compile/link/error vs uniform setters + cache) |
| `Mesh` | `renderer/mesh.{h,cpp}` | review size; split setup vs cleanup if over cap |
| `Texture` | `renderer/texture.{h,cpp}` | likely fine as-is |
| `Camera` | `renderer/camera.{h,cpp}` | split movement vs matrices if over cap |
| `Window` | `core/window.{h,cpp}` | review |
| `InputManager` | `core/input_manager.{h,cpp}` | review |
| `ResourceManager` | `core/resource_manager.{h,cpp}` | split by resource kind (`shaders.cpp`/`textures.cpp`/`meshes.cpp`) if over cap |
| `FixedTimestep` | `core/fixed_timestep.h` | header-only struct — leave; do **not** over-engineer |
| `JoltWorld` | `physics/jolt_world.h` | header-only (init/step/shutdown inline) — review whether `init` belongs in a `.cpp` |

**Process:** run `check_file_sizes` first; only split classes whose `.cpp` exceeds the
class cap (Plan 01 §4, ~200 lines). Most are likely already under it — **don't split
for its own sake.** A class declaration stays in one header regardless.

## Header discipline pass

Across these headers (and all `engine/**/*.h`):

- Replace any barrel include in a header with the **specific leaf** it needs, or a
  **forward declaration** when only the name is used (`struct JoltWorld;` is already
  done in `app/simulation.h` — apply the same elsewhere).
- Target: `check_header_discipline` (Plan 02) reports zero `.h` files pulling an
  umbrella barrel.

## Risks

- Splitting a `.cpp` is mechanically safe (consumers see an unchanged header) — the
  only failure mode is forgetting to add the new `.cpp` to `CMakeLists.txt` (build
  catches it immediately).
- Header-discipline edits can surface *hidden* transitive dependencies (a `.cpp` that
  relied on the barrel pulling something in). Build catches these; fix by adding the
  now-needed direct include to the `.cpp`.

## Verification

Build + 6 scenarios after each class split and after the header pass. (These classes
are exercised by the windowed app, not the headless suite — so also do a **windowed
smoke run** of `QEngine.exe` if any renderer/`Window`/`InputManager` header changes,
since the harness can't cover rendering/input.)

## Done when

`check_file_sizes` reports no class `.cpp` over cap (or a documented exception),
`check_header_discipline` is clean for `renderer/` + `core/`, build clean, scenarios
pass, windowed smoke run OK.
