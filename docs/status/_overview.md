# QEngine — Status Overview

Quick-parse state of every functionality. Legend: ✅ working · 🟡 partial ·
🔴 not implemented · 🗄 archived (dead code, kept for reference).

Read this first for "what works right now". Per-process detail in
`<process>.md`; how each works in [`../processes/`](../processes/).

_Baseline: architecture docs through Chapter 15d. Last verified: 2026-06-14._

---

## Processes

| Process | State | One-line status |
|---------|-------|-----------------|
| [game-loop](game-loop.md) | ✅ | Fixed 60 Hz timestep + headless harness working |
| [player-movement](player-movement.md) | ✅ | Quake-style accel/jump/bunny-hop via `CharacterVirtual` |
| [movers](movers.md) | ✅ | Doors + lifts; kinematic push verified (lift fix shipped) |
| [combat](combat.md) | ✅ | Shotgun hitscan + rocket projectile; projectile fix shipped |
| player death/respawn | ✅ | `player_death_system` in the tick order (Health ≤ 0 → respawn) |
| item pickups | 🟡 | `pickupSystem` grants health/ammo/armour/weapons on touch (5 demo items in showcase); respawn + armour absorption pending (Phase 2) |
| [triggers](triggers.md) | ✅ | activate/teleport/damage/heal; `changeLevel` recorded but not wired |
| [physics](physics.md) | ✅ | Jolt static/dynamic/kinematic/character/sensor + sync bridge |
| [rendering](rendering.md) | ✅ | Phong (dir + point lights), textures, debug text HUD |

## Engine subsystems (foundation)

| Subsystem | State | Note |
|-----------|-------|------|
| Window / GL context / input | ✅ | GLFW + glad |
| Shaders (basic/textured/lit/hud) | ✅ | |
| Texture + OBJ loading | ✅ | stb_image; OBJ via in-repo `renderer/obj_loader.cpp` |
| ECS (EnTT) | ✅ | components + free-function systems |
| FPS camera | ✅ | mouse look |
| Level geometry (sectors/surfaces) | ✅ | hardcoded showcase room |
| Entity spawn (factories) | ✅ | data-driven: `classname`→factory dispatch + two-pass `targetname` linking; showcase built from an in-code descriptor list (`.map`-loader-ready) |
| Resource manager | ✅ | name-cached handles |
| Audio (miniaudio) | 🟡 | miniaudio + stb_vorbis (OGG); named sound manifest (SFX + music); `SoundQueue`/`audioSystem` event wiring (fire/pickup/door/teleport/jump/pain/death) + looping music. Pending: footsteps, 3D positional falloff, adaptive music |

## Not yet implemented

| Feature | State | Note |
|---------|-------|------|
| AI / enemies | 🔴 | no behaviour systems |
| Crosshair / expanded HUD | 🔴 | debug HUD only |
| Networking | 🔴 | `src/engine/network/` empty (scaffold); see roadmap |
| TrenchBroom level loading | 🔴 | levels hardcoded; setup planned (plan 03) |
| BSP traversal | 🔴 | sectors built, no BSP walk |
| Unit tests | 🔴 | `tests/` empty; only the headless harness exists (7 scenarios inc. `spawn_counts`) |

## Archived (not compiled)

`collisionSystem`, `physicsSystem`, `movementSystem`, `playerMovementSystem`
in `src/engine/ecs/systems/archived/` — superseded by Jolt. Legacy physics
helpers (`spatial_hash`, `collision`, `aabb`, `collision_layers`) back them.

---

## Where to look next

Active planning docs in [`../plans/`](../plans/) (improvements, next features,
TrenchBroom, architecture refresh). Forward direction in
[`../roadmap/ROADMAP.md`](../roadmap/ROADMAP.md).
