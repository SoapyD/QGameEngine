# Process: Game Loop

**Purpose:** Orchestrate every tick — collect input once per frame, run game
systems at a fixed 60 Hz, then render at display rate.

**Entry point:** `src/main.cpp` (game loop); `src/engine/app/simulation.{h,cpp}`
(system orchestration). The headless equivalent is `src/harness/headless_main.cpp`.

## Flow

```
while (!window.shouldClose())
    accumulate(realElapsed)        // clamped to 0.25s (spiral-of-death guard)
    pollInput()                    // GLFW events, mouse look → PlayerInput + camera-front ctx
    while (accumulator >= 1/60)
        runGameSystems(1/60)       // fixed-order system run (see below)
        accumulator -= 1/60
    updateCamera()                 // follow player position
    render(); swapBuffers()
```

**Fixed timestep:** `1/60` s. Logic and physics always advance at 60 ticks/sec
regardless of frame rate — multiple ticks per frame when slow, zero when fast.

## System order per tick

The order in [`_overview.md`](_overview.md) is authoritative. Key invariants:
- `weaponSwitchSystem` runs before `combatSystem` so the active weapon is correct.
- `moverSyncSystem` runs **before** the Jolt step (pushes kinematic targets);
  `joltSyncSystem` runs **after** (reads results back).
- Render and HUD run once per frame, outside the fixed loop.

## Key files

| File | Role |
|------|------|
| `src/main.cpp` | Window, input collection, frame/tick loop |
| `src/engine/app/simulation.{h,cpp}` | `runGameSystems` — the per-tick call order |
| `src/engine/core/fixed_timestep.h` | Accumulator + clamp |
| `src/harness/headless_main.cpp` | Headless run (no window) for deterministic testing |

## Inputs / Outputs

- **In:** GLFW keyboard/mouse, real elapsed time.
- **Out:** mutated `entt::registry`, presented frame.

See also: [`../architecture/TICK_ORDER.md`](../architecture/TICK_ORDER.md),
[status](../status/game-loop.md).
