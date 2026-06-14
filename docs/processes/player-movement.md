# Process: Player Movement

**Purpose:** Quake-style first-person locomotion — ground/air acceleration,
jumping, bunny-hopping, stair-stepping — driven by Jolt's `CharacterVirtual`.

**System:** `playerCharacterSystem` (`src/engine/ecs/systems/player_character_system.{h,cpp}`).

## Flow (per tick)

```
PlayerInput.wishDir/jump  ──┐
CharacterPhysics (caps)   ──┤
                            ▼
  ground? Quake ground accel → maxGroundSpeed (+ groundFriction when idle)
  air?    same accel formula  → maxAirSpeed (1.0)  → bunny hop
  jump?   vertical vel = jumpForce  (only when OnGround)
  always (airborne): gravity -20·dt applied to desiredVel
                            ▼
  CharacterVirtual.ExtendedUpdate(stepHeight)   // stair-up + floor-stick
                            ▼
  Position  ← CharacterVirtual.GetPosition()
  OnGround  ← CharacterVirtual.GetGroundState()
```

**Initialisation:** call `initPlayerCharacter(registry)` once after scene setup —
builds a `CapsuleShape` from the player's `AABBCollider` half-extents and
constructs the `CharacterVirtual`.

## Components

| Component | Access |
|-----------|--------|
| `PlayerInput` | read (`wishDir`, `jump`) |
| `CharacterPhysics` | read (accel, friction, speed caps, jumpForce, stepHeight) |
| `JoltCharacter` | read/write (the `CharacterVirtual` ref + velocity) |
| `Position` | write (from `CharacterVirtual`) |
| `OnGround` | write (from ground state) |

**Context:** `PhysicsConfig` (read `fixedDeltaTime`), `JoltWorld` (read gravity,
broad-phase/layer filters, temp allocator).

## Why CharacterVirtual

The player does not use a dynamic rigid body — `CharacterVirtual` gives direct
velocity control with collision response, which is required for responsive
Quake-style movement (instant accel, air control). The dynamic-body
`OnGround` heuristic in `joltSyncSystem` is only a fallback for cubes.

See also: [`../architecture/SYSTEMS.md`](../architecture/SYSTEMS.md#2-playercharactersystem),
[status](../status/player-movement.md).
