# QEngine — Processes Overview

A **process** is a player-facing functionality and the chain of ECS systems that
implement it. This folder describes *how things flow*; [`../architecture/`](../architecture/)
is the detailed per-file/per-component reference, and [`../status/`](../status/)
tracks the current state of each process.

Read this file to learn what functionalities exist. Read a specific
`<process>.md` for its flow. Read `../status/<process>.md` for its current state.

---

## The engine in one paragraph

QEngine is an EnTT-based ECS engine running a fixed 60 Hz timestep
([`../architecture/TICK_ORDER.md`](../architecture/TICK_ORDER.md)). Each tick,
free-function systems run in a fixed order over the registry; Jolt Physics owns
collision/dynamics and the ECS mirrors its transforms. Rendering runs at display
frame rate. All singletons (`PhysicsConfig`, `JoltWorld`, `HudConfig`,
`CombatResources`) live in `registry.ctx()`.

---

## Processes

| Process | What it does | Systems involved |
|---------|--------------|------------------|
| [game-loop](game-loop.md) | Fixed-timestep orchestration and per-tick system order | `main.cpp` / `app/simulation` |
| [player-movement](player-movement.md) | Quake-style locomotion via Jolt `CharacterVirtual` | `playerCharacterSystem` |
| [movers](movers.md) | Doors and lifts (kinematic state machine) | `moverSystem`, `moverSyncSystem` |
| [combat](combat.md) | Weapons: hitscan + projectiles, damage, effects | `weaponSwitchSystem`, `combatSystem`, `lifetimeSystem` |
| [triggers](triggers.md) | Trigger volumes → activate / teleport / damage / heal | `triggerSystem` |
| [physics](physics.md) | Jolt integration and the ECS↔Jolt sync bridge | `moverSyncSystem`, `joltSyncSystem`, `demoResetSystem` |
| [rendering](rendering.md) | Mesh drawing, Phong lighting, debug HUD | `renderSystem`, `debugHudSystem` |

---

## Per-tick order (summary)

```
weaponSwitchSystem        → resolve weapon change before firing
playerCharacterSystem     → player locomotion (CharacterVirtual)
moverSystem               → door/lift state machine (ECS positions)
moverSyncSystem           → push mover positions into Jolt kinematic bodies
[ Jolt physicsSystem.Update ]   → simulate dynamics + collisions
joltSyncSystem            → read Jolt transforms back into ECS
combatSystem              → fire weapons, hitscan, spawn projectiles
triggerSystem             → AABB overlap → actions
lifetimeSystem            → expire projectiles/tracers
demoResetSystem           → periodic demo-object respawn
[ render + debugHudSystem ]     → once per frame, not per tick
```

See [game-loop.md](game-loop.md) for the authoritative order and rationale.

---

_Last verified: 2026-06-14 (against architecture docs through Chapter 15d)._
