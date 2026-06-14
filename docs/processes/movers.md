# Process: Movers (Doors & Lifts)

**Purpose:** Animate kinematic platforms — doors and lifts — through a state
machine, and make them physically push the player and dynamic bodies.

**Systems:** `moverSystem` (animation) + `moverSyncSystem` (push into Jolt).
`src/engine/ecs/systems/mover_system.{h,cpp}`, `mover_sync_system.{h,cpp}`.

## Flow (per tick)

```
moverSystem:   advance state machine, write ECS Position
   Idle ──trigger──▶ StartDelay ──timer──▶ Moving ──progress=1──▶ Waiting
                                                                      │timer
                          Idle ◀──progress=0── Returning ◀───────────┘

   Moving/Returning: Position = mix(startPos, endPos, progress)
                     progress advances at `speed` units/sec

moverSyncSystem: Position ──▶ JoltBody via MoveKinematic
```

**Activation:** a mover sits in `Idle` until [`triggerSystem`](triggers.md) sets
its state to `Moving` (or `StartDelay`).

## Why `MoveKinematic` (not `SetPosition`)

`MoveKinematic` gives Jolt the *target* position for the next step; Jolt computes
the velocity and sweeps the body, pushing anything in its path. `SetPosition`
would teleport and pass through bodies. This is why `moverSyncSystem` must run
**before** the Jolt step and `joltSyncSystem` after.

## Components

| Component | System | Access |
|-----------|--------|--------|
| `Mover` | both | read/write (state, progress, timer) |
| `Position` | moverSystem write, moverSyncSystem read | interpolated |
| `JoltBody` | moverSyncSystem | read (`id`) |

**Context:** `PhysicsConfig` (`fixedDeltaTime`), `JoltWorld` (`getBodyInterface()`).

See also: [`../architecture/SYSTEMS.md`](../architecture/SYSTEMS.md#3-moversystem),
[triggers](triggers.md), [physics](physics.md), [status](../status/movers.md).
