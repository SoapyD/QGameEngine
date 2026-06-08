# QEngine — Game Loop & Tick Order

## The Game Loop

The game loop in `main.cpp` follows a standard fixed-timestep pattern:

```
while (!window.shouldClose())
{
    accumulate(currentTime)     // Add real elapsed time to accumulator
    pollInput()                 // Read keyboard/mouse

    while (accumulator >= fixedDeltaTime)    // Fixed timestep loop
    {
        runGameSystems(fixedDeltaTime)       // All game logic
        accumulator -= fixedDeltaTime
    }

    updateCamera()              // Follow player position
    render()                    // Draw everything
    swapBuffers()               // Present to screen
}
```

**Fixed timestep:** 1/60th of a second (16.67ms). Physics and game logic always run at exactly 60 ticks/second regardless of frame rate. If the frame rate drops below 60, multiple ticks run per frame. If it's above 60, some frames have zero ticks.

**Spiral of death protection:** `FixedTimestep::accumulate()` clamps `frameTime` to `maxFrameTime` (0.25s), preventing the accumulator from building up unrecoverably during hitches.

---

## Pre-Tick: Input Collection

Before the fixed timestep loop, input is collected once per frame:

```
1. input.update()              — Poll GLFW events
2. Camera mouse look           — Process mouse delta
3. Build wishDir               — WASD → normalized direction vector
4. Populate PlayerInput        — Write wishDir, jump, fire, weaponSwitch to ECS
5. Write camera front to ctx   — For combat system firing direction
```

Input is frame-rate-dependent (collected once per render frame), but the systems that consume it run at fixed rate. This is standard — input doesn't need to be perfectly synchronized with physics.

---

## Fixed Tick Order

Each fixed timestep tick runs these systems in order:

```
 #  System                    Phase         Purpose
─────────────────────────────────────────────────────────────────
 1  weaponSwitchSystem        Input         Handle weapon swap input
 2  moverSystem               Animation     Animate door/lift positions
 3  moverSyncSystem           Physics Prep  Push mover positions to Jolt
 4  joltWorld.step()          Physics       Simulate rigid + kinematic bodies
 5  joltSyncSystem            Physics Sync  Read Jolt transforms → ECS
 6  playerCharacterSystem     Movement      Apply input → CharacterVirtual,
                                            resolving against the now-moved world
 7  combatSystem              Game Logic    Hitscan/projectile weapons
 8  lifetimeSystem            Cleanup       Auto-destroy timed entities
 9  triggerSystem             Game Logic    Detect trigger overlaps
10  playerDeathSystem         Game Logic    Respawn the player on death
11  demoResetSystem           Cleanup       Reset physics demo objects
```

> **Tick-order note:** the player's `ExtendedUpdate` (6) runs *after* movers
> animate (2-3) and the physics step sweeps their kinematic bodies (4). This is
> deliberate — it lets the player's capsule resolve against the lift/door at its
> *current-tick* position, so boarding and riding a lift are smooth. Combined
> with `GetGroundVelocity()` platform inheritance in `playerCharacterSystem`,
> the player rides movers without lag or jitter.

### Why This Order Matters

**1-2: Input before physics.** The player's desired velocity must be set before the physics step resolves collisions. If physics ran first, the player would always be one tick behind their input.

**3-4: Movers before physics.** `moverSystem` calculates where the door/lift should be this tick. `moverSyncSystem` tells Jolt's kinematic body to move there. During the physics step (5), the kinematic body sweeps to that position and pushes anything in its path.

**5: Physics step.** Jolt resolves all collisions, applies gravity to dynamic bodies, moves kinematic bodies to their targets, and updates the `CharacterVirtual`. This is the most expensive call in the loop.

**6: Sync after physics.** `joltSyncSystem` reads the post-physics positions back into the ECS. Systems after this point see the final, collision-resolved positions.

**7-8: Game logic after final positions.** Combat raycasts need accurate positions. Lifetime countdown is independent of physics. Both run after positions are settled.

**9: Triggers last.** Trigger detection uses ECS positions (not Jolt queries), so it needs to run after `joltSyncSystem` has updated everything. Activating a mover here means it will start moving on the *next* tick (which is correct — the state change is picked up by `moverSystem` next iteration).

**10: Demo reset.** Runs last because it teleports entities, which would interfere with physics if it ran earlier.

---

## Post-Tick: Camera and Rendering

After all fixed ticks have run:

```
1. Camera follows player       — Read player Position, set camera eye height
2. Write camera front to ctx   — Update for next frame's combat system
3. Clear framebuffer           — glClear
4. renderSystem                — Draw all MeshRenderer entities + lighting
5. debugHudSystem              — Draw text HUD overlay (FPS, health, ammo)
6. Swap buffers                — Present to screen
```

Rendering runs once per frame at the display's frame rate. It reads ECS positions directly — there's no interpolation between physics states currently. This means at very high frame rates, objects may appear to move in discrete steps. Adding interpolation (using `FixedTimestep::getAlpha()`) is a future improvement.

---

## Jolt Physics Step Detail

Inside `joltWorld.step(deltaTime)`:

```
1. Broad phase               — Spatial acceleration structure finds potential pairs
2. Narrow phase               — Exact shape intersection tests
3. Contact resolution         — Generate contact constraints
4. Solver                     — Resolve constraints (push bodies apart, apply friction)
5. Position integration       — Move bodies to their new positions
6. CharacterVirtual update    — Already handled by ExtendedUpdate in playerCharacterSystem
```

The physics step runs with 1 collision sub-step per call. This is sufficient because our fixed timestep (1/60s) is already small enough for stable simulation.

---

## Data Flow Diagram

```
    PlayerInput ──→ playerCharacterSystem ──→ CharacterVirtual velocity
                                                      │
    Mover state ──→ moverSystem ──→ ECS Position      │
                                       │              │
                          moverSyncSystem ──→ Jolt kinematic body target
                                                      │
                                              joltWorld.step()
                                                      │
                                              joltSyncSystem ──→ ECS Position
                                                      │
                                              combatSystem (raycasts against final positions)
                                                      │
                                              triggerSystem (AABB overlap with final positions)
                                                      │
                                              renderSystem (draws from ECS Position + MeshRenderer)
```
