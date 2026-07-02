# Chapter 15c: Jolt Integration — Wiring, CMake & Architecture Review

## What You'll Learn
- Wiring all Jolt systems into `main.cpp`
- The complete tick order and why it matters
- Updating `CMakeLists.txt` to swap source files
- Comparing the old and new physics architecture

---

## Step 5: Wire It All Up in main.cpp

### Add new includes

```cpp
#include "engine/ecs/systems/player_character_system.h"
#include "engine/ecs/systems/mover_sync_system.h"
```

### Remove old player movement include

```cpp
// REMOVE:
#include "engine/ecs/systems/player_movement_system.h"
```

### Initialise the player character after scene setup

After `OptimizeBroadPhase()`:

```cpp
// Create Jolt bodies for movers (lifts, doors)
auto moverView = registry.view<Position, AABBCollider, Mover>();
for (auto [entity, pos, col, mover] : moverView.each())
{
    createKinematicBody(registry, entity);
}

// Create sensor bodies for triggers
auto triggerView = registry.view<Position, AABBCollider, TriggerVolume>();
for (auto [entity, pos, col, trigger] : triggerView.each())
{
    if (col.isTrigger)
    {
        createSensorBody(registry, entity);
    }
}

// Initialise the player's CharacterVirtual
initPlayerCharacter(registry);

// Re-optimise broad phase after adding more bodies
joltWorld.physicsSystem->OptimizeBroadPhase();
```

### Update the tick loop

```cpp
while (fixedTimestep.step())
{
    weaponSwitchSystem(registry);
    playerCharacterSystem(registry);         // NEW: replaces playerMovementSystem
    moverSystem(registry);                   // animate doors/lifts
    moverSyncSystem(registry);               // NEW: push mover positions to Jolt
    joltWorld.step(physicsConfig.fixedDeltaTime);  // step physics
    joltSyncSystem(registry);                // sync dynamic body transforms to ECS
    combatSystem(registry, level);
    lifetimeSystem(registry);
    triggerSystem(registry);
    demoResetSystem(registry);
}
```

### Tick Order Explained

```
1. weaponSwitchSystem      — handle weapon swap input
2. playerCharacterSystem   — apply input → Jolt CharacterVirtual
3. moverSystem             — animate door/lift positions
4. moverSyncSystem         — push mover positions to Jolt kinematic bodies
5. joltWorld.step()        — simulate physics, resolve collisions
6. joltSyncSystem          — read Jolt transforms → ECS Position/Velocity
7. combatSystem            — hitscan/projectile weapons
8. lifetimeSystem          — auto-destroy timed entities
9. triggerSystem            — detect trigger overlaps
10. demoResetSystem         — reset physics demos
```

The critical ordering: `playerCharacterSystem` runs first to set the character's desired velocity. `moverSyncSystem` runs before the physics step so kinematic bodies have their target positions. The physics step resolves everything. Then `joltSyncSystem` reads the results back to the ECS.

---

## Step 6: Update CMakeLists.txt

### Remove old sources

```cmake
# REMOVE (if not already removed in Ch14b):
src/engine/ecs/systems/player_movement_system.cpp
```

### Add new sources

```cmake
src/engine/ecs/systems/player_character_system.cpp
src/engine/ecs/systems/mover_sync_system.cpp
```

---

## What Changed — Summary

| File | Change |
|------|--------|
| `main.cpp` | Updated tick loop, removed old player movement system, added Jolt body creation for movers/triggers/player |
| `CMakeLists.txt` | Swapped source files |

### Files removed from build

| File | Was |
|------|-----|
| `player_movement_system.cpp/h` | Custom Quake-style acceleration (now in `playerCharacterSystem`) |

---

## What You Should See

After building and running:

1. **WASD moves the player** — same Quake-style acceleration and friction as before
2. **Spacebar jumps** — gravity pulls you back down, no jitter on landing
3. **The lift carries you upward** — stand on it, trigger it, ride it up. No custom rider code needed — the kinematic body pushes the CharacterVirtual
4. **Doors push you out of the way** — walk into a closing door and it moves you
5. **Stair stepping works** — walk onto the lift's thin platform without being blocked. `ExtendedUpdate` handles this automatically
6. **Cubes land cleanly** — no jitter, no micro-bouncing
7. **Lava still damages you** — trigger system is unchanged
8. **Weapons still work** — projectiles fly and push Jolt bodies on impact (patched in Ch 14b)

### Troubleshooting

**Player falls through the floor:**
- Check that `initPlayerCharacter` is called after level bodies are created
- Verify the capsule shape dimensions match the player's collider half-extents
- Ensure gravity direction is `(0, -20, 0)` not `(0, 20, 0)`

**Player slides on slopes they should stand on:**
- Increase `mMaxSlopeAngle` in the character settings (default 50 degrees)

**Lift doesn't push the player:**
- Verify the lift entity has a `JoltBody` component (kinematic)
- Check that `moverSyncSystem` runs before `joltWorld.step()`
- Use `MoveKinematic` not `SetPosition` — only `MoveKinematic` generates the velocity needed to push

**Player movement feels different:**
- The Quake-style acceleration is reimplemented in `playerCharacterSystem`. Tune `CharacterPhysics` values (groundAcceleration, maxGroundSpeed, etc.) to match your desired feel.
- `ExtendedUpdate` has its own floor-sticking behaviour that interacts with your velocity — if the player "sticks" going down slopes, reduce `mStickToFloorStepDown`

**Triggers don't fire:**
- The `triggerSystem` uses ECS-level AABB overlap, which still works since positions are synced. If triggers seem unreliable, check that the player's ECS `Position` is being updated each frame.

---

## Architecture Review

Let's compare the old and new physics architecture:

### Before (Custom Physics)

```
playerMovementSystem  → sets Velocity from input
physicsSystem         → applies gravity, friction to Velocity
moverSystem           → animates mover positions
collisionSystem       → sweeps AABBs, adjusts Velocity
movementSystem        → applies Velocity to Position
groundDetectionSystem → raycasts downward, sets OnGround
```

Six systems, all interdependent, full of edge cases.

### After (Jolt)

```
playerCharacterSystem → sets CharacterVirtual velocity, calls ExtendedUpdate
moverSystem           → animates mover positions (unchanged)
moverSyncSystem       → pushes mover positions to kinematic bodies
joltWorld.step()      → simulates everything
joltSyncSystem        → reads transforms back to ECS
```

Five calls, but the heavy lifting is inside Jolt. Ground detection, stair stepping, collision response, resting contact — all handled by Jolt's solver.

---

## What's Next

The physics are now solid and reliable. In **Chapter 16**, we'll start building gameplay content — crosshair, expanded HUD, death/respawn, and damage feedback — to exercise the new physics system and create a playable experience.
