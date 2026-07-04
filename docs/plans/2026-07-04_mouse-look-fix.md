# Plan — Fix: mouse look drops motion while moving

**Status:** Proposed — ready to implement. **Small** (a one-line-ish fix + optional polish).

**Symptom (reported):** you can't move/strafe and look at the same time — the camera barely
turns while you're holding a movement key.

---

## Diagnosis (confirmed in source)
The per-frame mouse delta only ever holds the **last motion event**, not the sum of the frame's
motion:

- [`input_manager.cpp:59-60`](../../src/engine/core/input_manager.cpp#L59-L60) — `mouseCallback`
  does `m_mouseXOffset = x - m_lastMouseX;` (**`=`**, overwrite) and updates `m_lastMouseX` each
  event.
- [`input_manager.cpp:25-26`](../../src/engine/core/input_manager.cpp#L25-L26) — `update()` resets
  the offsets to 0 at the start of each frame.
- [`camera.cpp:38-49`](../../src/engine/renderer/camera.cpp#L38-L49) — `processMouse` applies that
  single frame offset to yaw/pitch.

`glfwPollEvents()` dispatches **every** pending motion event per frame, each re-invoking the
callback. Because the callback overwrites (rather than accumulates), only the final event's tiny
delta survives — a 1000 Hz mouse at 60 fps loses ~15/16 of the movement. It gets **worse on longer
frames**: when the game dips below vsync (moving = physics + AI + more render), more events pile up
per frame and a larger fraction is discarded — so look feels dead *specifically while moving*. That
matches the report exactly.

## Fix
- **Accumulate** the delta in the callback: `m_mouseXOffset += x - m_lastMouseX;` and
  `m_mouseYOffset += m_lastMouseY - y;` (still update `m_lastMouseX/Y` every event). `update()`
  already zeroes them at frame start, so the sum is the whole frame's motion.
- **Optional polish:** enable raw mouse motion when available
  (`glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE)` guarded by
  `glfwRawMouseMotionSupported()`) for unaccelerated, consistent input.

## Verification
- Manual (this is windowed input — the headless harness can't exercise it): hold `W` and sweep the
  mouse; the camera should turn the full amount smoothly, the same whether standing or moving.
- Sanity: log the accumulated `xOffset` for a frame with several motion events and confirm it's the
  sum, not the last delta.

## Notes
This is a ~2-line change with no ripple — no docs/anti-drift updates needed beyond a mention. Could
just be applied directly rather than scheduled.
