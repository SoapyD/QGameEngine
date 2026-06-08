# 02 — Core Foundation

**Status:** Evaluated.
**Scope:** `core/window.*`, `core/input_manager.*`, `core/resource_manager.*`, `core/fixed_timestep.h`.
**Part of:** [README.md](README.md).

## Findings

| # | Issue | Sev | Detail | Action | Location |
|---|-------|-----|--------|--------|----------|
| 2.1 | **Window ctor swallows failures** | P1 | On GLFW init / window create / GLAD load failure it just `return`s, leaving a half-constructed object with `m_window == nullptr`. `main` never checks, then calls `glfwMakeContextCurrent`/GL on null → crash with no clear cause. | Signal failure (throw, or an `ok()` the caller checks) and abort cleanly. | [window.cpp:8-37](../../../../src/engine/core/window.cpp#L8-L37) |
| 2.2 | **Resize breaks aspect ratio** | P1 | `framebufferSizeCallback` updates the GL viewport but **not** `m_width/m_height`. `getWidth()/getHeight()` stay at the original size, so `renderSystem`'s aspect ratio and the HUD use stale dimensions after any resize. | Update `m_width/m_height` in the callback (needs the instance via the GLFW user pointer, like `InputManager`). | [window.cpp:77-80](../../../../src/engine/core/window.cpp#L77-L80) |
| 2.3 | Mouse delta overwrite | P2 | `mouseCallback` assigns (`=`) the offset and advances `m_lastMouse` each event. Two mouse events in one frame → the first event's motion is lost. Should accumulate (`+=`) and clear in `update()`. | Accumulate offsets; zero in `update()`. | [input_manager.cpp:43-62](../../../../src/engine/core/input_manager.cpp#L43-L62) |
| 2.4 | No edge-triggered input | P2 | Only `isKeyPressed` (held) / `isKeyReleased`. Weapon-switch and jump rely on held polling each tick. Works, but no "just pressed" for one-shot actions. | Add a prev-state edge API if/when needed. | [input_manager.cpp:29-37](../../../../src/engine/core/input_manager.cpp#L29-L37) |
| 2.5 | `getAlpha()` unused | P1 | `FixedTimestep` exposes an interpolation alpha but nothing uses it → no render interpolation → discrete stepping at high FPS (reads as jitter). See [05 §9](05-physics.md) and [08](08-integration.md). | Decide: implement render interpolation, or accept and document. | [fixed_timestep.h:50](../../../../src/engine/core/fixed_timestep.h#L50) |
| 2.6 | GL state set in Window ctor | P2 | Cull-face config lives in the Window ctor while depth-test is enabled in `main` — render state is split across two places. | Consolidate render-state init. | [window.cpp:39-41](../../../../src/engine/core/window.cpp#L39-L41) |
| 2.7 | ResourceManager teardown order | P2 | `clear()` relies on `shared_ptr` dtors calling `glDelete*`; it's invoked before the Window dtor in `main`, so the GL context is still alive — currently correct, but fragile if reordered. | Note the ordering dependency; keep `clear()` before context teardown. | [resource_manager.cpp:118](../../../../src/engine/core/resource_manager.cpp#L118) |

## Graduates to a fix plan
2.1 + 2.2 are real robustness bugs → `docs/plans/core-robustness-fixes.md`. 2.3 folds in. 2.5 is shared with the physics/integration interpolation decision — resolve there.
