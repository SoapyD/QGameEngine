# Status: Game Loop

**State:** ✅ working · _verified 2026-06-14_

## Works
- Fixed 60 Hz timestep with accumulator + 0.25 s spiral-of-death clamp.
- Deterministic system order (`app/simulation`), shared by windowed and headless.
- Headless harness (`src/harness/headless_main.cpp`) runs the sim with **no window
  and no GL context** (GL-free stub resources, render-mesh build skipped) — runs on
  a box with no GPU/driver (CI-ready, 2026-06-14). Used by the eval/fix bundle.
- Windowed input mapping and camera-follow are systems (`playerInputSystem`,
  `cameraFollowSystem`), not inline game-loop code.

## Known gaps / risks
- No per-system timing/profiling instrumentation.
- Tick order is documented in two places ([`../processes/game-loop.md`](../processes/game-loop.md)
  and [`../architecture/TICK_ORDER.md`](../architecture/TICK_ORDER.md)) — keep in sync.

## Next
- None blocking. Add lightweight frame/tick timing if profiling becomes needed.

Process: [`../processes/game-loop.md`](../processes/game-loop.md)
