# Process: Triggers (Trigger Volumes)

**Purpose:** Detect the player entering a volume and run an action — activate a
mover, teleport, damage, heal, change level, or show a message.

**System:** `triggerSystem` (`src/engine/ecs/systems/trigger_system.{h,cpp}`).

## Flow (per tick)

```
for each TriggerVolume:
   AABB overlap test (trigger extents vs player AABBCollider)
   if overlapping && not on cooldown:
       activateMover  → set target Mover.state = Moving / StartDelay
       teleport       → set player Position; zero Velocity
       damage / heal  → modify player Health (per-second via fixedDeltaTime)
       changeLevel    → (action recorded; level swap not yet wired)
       message        → display text
       set cooldown / triggered flag
```

## Detection note

Overlap uses **ECS-level AABB** (`AABB::intersects` from the live `physics/aabb.h`),
not Jolt's contact listener. Jolt sensor bodies exist for the triggers but are not
queried here — ECS overlap is sufficient because `joltSyncSystem` keeps positions
current. (A future move to the Jolt sensor contact listener is a known option —
see status.)

Triggerable entities are selected by the `TagTriggerable` tag (not `TagPlayer`),
so enemies or props can be made to fire triggers by adding the tag — no change to
`triggerSystem`. Today only the player carries it.

## Components

| Component | Access |
|-----------|--------|
| `TriggerVolume` | read/write (action, target, cooldown, triggered) |
| `Position`, `AABBCollider` | read (trigger + triggerable entity) |
| `TagTriggerable` | read (which entities can fire triggers — currently just the player) |
| `Mover` | write (activate target) |
| `Velocity` | write (zero on teleport) |
| `Health` | write (damage/heal) |

**Context:** `PhysicsConfig` (`fixedDeltaTime` for cooldown + DPS).

See also: [`../architecture/SYSTEMS.md`](../architecture/SYSTEMS.md#8-triggersystem),
[movers](movers.md), [status](../status/triggers.md).
