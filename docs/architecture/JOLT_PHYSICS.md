# QEngine — Jolt Physics Integration

Jolt Physics replaced the custom collision/physics/movement/ground-detection systems in Chapters 14-15. This document covers the integration architecture.

---

## JoltWorld

**File:** `src/engine/physics/jolt_world.h`

Stored in registry context (`registry.ctx().get<JoltWorld>()`). Wraps all Jolt infrastructure:

| Member | Type | Purpose |
|--------|------|---------|
| `tempAllocator` | `TempAllocatorImpl` | 10MB pre-allocated scratch memory for Jolt |
| `jobSystem` | `JobSystemThreadPool` | Multi-threaded job system (cores - 1) |
| `physicsSystem` | `PhysicsSystem` | The physics world — contains all bodies |
| `broadPhaseLayerInterface` | `BPLayerInterfaceImpl` | Maps ObjectLayers to BroadPhaseLayers |
| `objectVsBroadPhaseFilter` | `ObjectVsBroadPhaseLayerFilterImpl` | Broad-phase collision filtering |
| `objectLayerPairFilter` | `ObjectLayerPairFilterImpl` | Narrow-phase collision filtering |

**Methods:**
- `init()` — Registers Jolt allocator, creates factory, initialises physics system with gravity `(0, -20, 0)`
- `step(deltaTime)` — Runs one physics update with 1 collision sub-step
- `getBodyInterface()` — Returns `BodyInterface&` for creating/querying/moving bodies
- `shutdown()` — Cleans up in reverse order

---

## Object Layers

**File:** `src/engine/physics/jolt_setup.h`

```
Layer 0: NON_MOVING  — Static geometry (floors, walls, shelves)
Layer 1: MOVING      — Dynamic bodies (cubes) and kinematic bodies (doors, lifts)
Layer 2: SENSOR      — Trigger volumes (no collision response, overlap detection only)
```

### Collision Matrix

| | NON_MOVING | MOVING | SENSOR |
|---|---|---|---|
| **NON_MOVING** | - | Yes | - |
| **MOVING** | Yes | Yes | - |
| **SENSOR** | - | Yes | - |

- Static bodies don't collide with each other (no point)
- Moving bodies collide with everything except sensors
- Sensors only detect overlap with moving bodies

### Broad Phase Layers

Two broad-phase layers for spatial acceleration:
- `NON_MOVING (0)` — Static geometry + sensors (share the same spatial bucket)
- `MOVING (1)` — Dynamic + kinematic bodies

---

## Body Types

### Static Bodies (`EMotionType::Static`)

Created by `createLevelBodies` (level geometry) and `createStaticBody` (individual entities like the shelf).

- Never move after creation
- Layer: `NON_MOVING`
- Activation: `DontActivate` (static bodies don't need activation)
- Used for: floors, walls, ceilings, shelves, platforms

### Dynamic Bodies (`EMotionType::Dynamic`)

Created by `createDynamicBody` for physics objects (demo cubes).

- Affected by gravity and collisions
- Layer: `MOVING`
- Activation: `Activate`
- Initial velocity set from ECS `Velocity` component
- Positions read back to ECS by `joltSyncSystem`

### Kinematic Bodies (`EMotionType::Kinematic`)

Created by `createKinematicBody` for movers (doors, lifts).

- Controlled by code, not physics — infinite mass
- Layer: `MOVING`
- Activation: `Activate`
- Moved via `MoveKinematic` in `moverSyncSystem`
- Push dynamic bodies and `CharacterVirtual` out of the way

### Sensor Bodies (`EMotionType::Static`, `mIsSensor = true`)

A `createSensorBody` helper exists, but **`buildWorld` deliberately creates none.** Triggers and
pickups run on **ECS AABB overlap** (`triggerSystem` / `pickupSystem`), not Jolt sensor queries —
the Jolt sensors were never queried and would be spuriously tripped by combat's impulse sweep.

The helper is documented here for completeness:
- Detect overlap but don't block movement
- Layer: `SENSOR`
- Activation: `DontActivate`

---

## Body Creation Functions

**File:** `src/engine/ecs/jolt_body_helpers.h/.cpp` (after Chapter 15d split)

All functions follow the same pattern:
1. Get `JoltWorld` and `BodyInterface` from registry context
2. Read `Position` and `AABBCollider` from the entity
3. Create a `BoxShapeSettings` from `halfExtents`
4. Create a `BodyCreationSettings` with appropriate motion type and layer
5. Call `CreateAndAddBody`
6. Emplace a `JoltBody` component on the entity

### `createLevelBodies(registry, level)`

Special case — iterates all surfaces in all sectors. Computes an AABB for each surface, fattens thin dimensions by ±0.1 (so Jolt's convex radius doesn't collapse the shape), and creates static bodies.

**Important:** Half-extents must exceed Jolt's default convex radius (0.05). The fattening of ±0.1 produces a minimum half-extent of 0.1, safely above this threshold.

---

## CharacterVirtual (Player)

**File:** `src/engine/ecs/systems/player_character_system.cpp`

The player uses `CharacterVirtual` instead of a rigid body. This gives direct velocity control while still getting collision response.

### Initialisation (`initPlayerCharacter`)

```
Shape:     CapsuleShape(halfHeight=0.55, radius=0.3)
           Total height = 2 * (0.55 + 0.3) = 1.7 units
Mass:      70 kg
Slope:     50 degrees max walkable slope
Strength:  100 (force applied to push dynamic bodies)
Predictive: 0.1 units contact detection distance
```

### Per-Tick Update (`playerCharacterSystem`)

1. Read ground state from `CharacterVirtual::GetGroundState()`
2. Build desired velocity from input (Quake-style acceleration)
3. Apply gravity if airborne
4. Set velocity on the character
5. Call `ExtendedUpdate` with stair-step and floor-stick settings
6. Read final position back to ECS `Position`

### ExtendedUpdate Settings

| Setting | Value | Effect |
|---------|-------|--------|
| `mWalkStairsStepUp` | `(0, stepHeight, 0)` | Max height the character can step up onto |
| `mStickToFloorStepDown` | `(0, -stepHeight, 0)` | How far down to stick when walking off edges |
| Gravity parameter | `(0, -20, 0)` | Used internally for floor-sticking calculations |

`stepHeight` defaults to 0.7 units (see `CharacterPhysics` in `components.h`). `ExtendedUpdate` only steps up when there's a solid surface to land on.

**Platform carry:** when the player is on a moving kinematic body (lift/door), `playerCharacterSystem` adds `CharacterVirtual::GetGroundVelocity()` to the player's velocity so they ride the platform. On static ground this is zero. The fixed tick order also runs movers + the physics step *before* the player's `ExtendedUpdate`, so the player resolves against the platform's current-tick position (see [TICK_ORDER.md](TICK_ORDER.md)).

---

## MoveKinematic vs SetPosition

| Method | Effect |
|--------|--------|
| `MoveKinematic(id, pos, rot, dt)` | Tells Jolt where the body should be after `dt` seconds. Jolt calculates velocity and sweeps the body, pushing anything in its path. |
| `SetPosition(id, pos, activation)` | Teleports the body instantly. No velocity, no sweep, no pushing. |

`moverSyncSystem` uses `MoveKinematic` — this is why the lift carries the player upward. If it used `SetPosition`, the lift would teleport through the player without pushing.

`demoResetSystem` uses `SetPosition` — teleporting demo cubes back to their start position is intentional (no physics during reset).

---

## Initialisation Order in main.cpp

```
1. Create JoltWorld, call init()
2. setupScene()                    — Creates all ECS entities (no Jolt bodies yet for movers/triggers)
3. createLevelBodies()             — Static bodies from level geometry
4. OptimizeBroadPhase()            — Build spatial acceleration structure
5. Create kinematic bodies         — For all entities with Mover component
   (no sensor bodies — triggers/pickups use ECS AABB overlap, not Jolt sensors)
6. initPlayerCharacter()           — Create CharacterVirtual
7. OptimizeBroadPhase()            — Re-optimize after adding more bodies
```

The order matters — `initPlayerCharacter` must run after level bodies exist so the character spawns on solid ground, not in empty space.
