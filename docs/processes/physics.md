# Process: Physics (Jolt Integration & Sync)

**Purpose:** Run Jolt Physics for dynamics and collision, and keep the ECS and
Jolt worlds in sync each tick.

**Systems:** `moverSyncSystem` (ECS→Jolt, pre-step), the Jolt step itself,
`joltSyncSystem` (Jolt→ECS, post-step), `demoResetSystem` (periodic respawn).
Setup: `physics/jolt_world.{h,cpp}`, `jolt_setup.h`, `physics_config.h`,
`ecs/jolt_body_helpers.{h,cpp}`. See [`../architecture/JOLT_PHYSICS.md`](../architecture/JOLT_PHYSICS.md).

## Body types

| Type | Used for | Driven by |
|------|----------|-----------|
| Static | level geometry, fixed platforms | never moves |
| Dynamic | physics cubes | Jolt solver (gravity, collisions) |
| Kinematic | doors, lifts | `MoveKinematic` from [movers](movers.md) |
| `CharacterVirtual` | player | [player-movement](player-movement.md) |
| Sensor | trigger volumes | overlap only (currently read by ECS, not Jolt) |

## Sync flow (per tick)

```
moverSyncSystem        Position ──▶ kinematic JoltBody (MoveKinematic)   [pre-step]
   ▼
JoltWorld.step(dt)     simulate gravity, collisions, constraints
   ▼
joltSyncSystem         JoltBody.GetCenterOfMassPosition() ──▶ Position    [post-step]
                       OnGround heuristic (vertical vel < 0.5) for cubes
```

`demoResetSystem` periodically teleports demo cubes back to start — resets both
ECS `Position`/`Velocity` and the Jolt body (`SetPosition` + `SetLinearVelocity`).

## Config

`PhysicsConfig`: `fixedDeltaTime` (1/60), `terminalVelocity`. `JoltWorld` owns the
`PhysicsSystem`, body interface, broad-phase/layer filters, and temp allocator
(all in `registry.ctx()`).

## Legacy & retained code

Some pre-Jolt files survive for tutorial reference. The split below matters:
mislabelling the **live** ones as dead has bitten us before (they *look* like old
collision code but the build depends on them) — do not "clean them up" without
checking this table.

**DEAD — not compiled, safe to ignore/delete (banner-marked in source):**
| File | Note |
|------|------|
| `physics/collision.{h,cpp}` | swept-AABB collision; not in CMake, no live includer |
| `physics/spatial_hash.{h,cpp}` | broad-phase grid; not in CMake, only archived used it |
| `level/level_loader.cpp` → `LevelLoader::*` | the `.qlvl` text parser; never called |
| `assets/levels/test.qlvl` | sample `.qlvl`; never loaded |
| `ecs/systems/archived/*` | pre-Jolt systems; not in CMake (see folder README) |

**LIVE — old-looking but actively used, do NOT remove:**
| File / symbol | Used by |
|---------------|---------|
| `physics/aabb.h` (`AABB`) | `combatSystem` (hit tests) and `triggerSystem` (overlap) |
| `physics/collision_layers.h` (`CollisionLayers`) | `components.h` — `AABBCollider::layer/mask` defaults reference it; the fields are inert under Jolt but the header is a real include |
| `level_loader.cpp` → `buildSectorMeshes` | `createShowcaseLevel()` — builds the level's render meshes (non-headless) |
| `physics/raycast.{h,cpp}` | `combatSystem` hitscan (ray-vs-AABB / ray-vs-level) |

See also: [`../architecture/JOLT_PHYSICS.md`](../architecture/JOLT_PHYSICS.md),
[status](../status/physics.md).
