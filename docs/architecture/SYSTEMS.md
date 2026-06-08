# QEngine — ECS Systems Reference

All systems are free functions that take `entt::registry&` as their first parameter. Systems have no state — all data lives in components or registry context.

---

## Active Systems (Current Build)

### 1. `weaponSwitchSystem`

**File:** `systems/weapon_switch_system.h` (inline, header-only)

**Purpose:** Reads `PlayerInput::weaponSwitch` and updates `WeaponInventory::currentWeapon`.

**Components:**
| Component | Access |
|-----------|--------|
| `PlayerInput` | Read (`weaponSwitch`) |
| `WeaponInventory` | Write (`currentWeapon`) |

**When:** Every fixed tick, before any combat logic. Ensures weapon changes take effect before `combatSystem` fires.

---

### 2. `playerCharacterSystem`

**File:** `systems/player_character_system.h/.cpp`

**Purpose:** The player's movement controller. Reads input, applies Quake-style acceleration (ground and air), handles jumping, and drives Jolt's `CharacterVirtual` via `ExtendedUpdate`.

**Initialisation:** Call `initPlayerCharacter(registry)` once after scene setup. This creates a `CapsuleShape` from the player's `AABBCollider` half-extents and constructs the `CharacterVirtual`.

**Components:**
| Component | Access |
|-----------|--------|
| `Position` | Write (updated from `CharacterVirtual::GetPosition()` after stepping) |
| `JoltCharacter` | Read/Write (holds the `CharacterVirtual` ref, velocity set each tick) |
| `PlayerInput` | Read (`wishDir`, `jump`) |
| `CharacterPhysics` | Read (acceleration, friction, speed caps, jump force, step height) |
| `OnGround` | Write (set from `CharacterVirtual::GetGroundState()`) |

**Context:**
| Context | Access |
|---------|--------|
| `PhysicsConfig` | Read (`fixedDeltaTime`) |
| `JoltWorld` | Read (`physicsSystem` for gravity, broad-phase/layer filters, `tempAllocator`) |

**Key behaviour:**
- **Ground movement:** Quake-style — project current velocity onto wish direction, accelerate up to `maxGroundSpeed`. No input = apply `groundFriction` deceleration.
- **Air movement:** Same acceleration formula but capped at `maxAirSpeed` (1.0), enabling bunny hopping.
- **Jump:** Sets vertical velocity to `jumpForce` when on ground and jump is pressed.
- **Gravity:** Applied manually to `desiredVel` while airborne (`-20.0f * dt`).
- **ExtendedUpdate:** Steps the character with stair-stepping (`mWalkStairsStepUp`) and floor-sticking (`mStickToFloorStepDown`), both controlled by `stepHeight`.

---

### 3. `moverSystem`

**File:** `systems/mover_system.h/.cpp`

**Purpose:** Animates doors and lifts through their state machine: `Idle` → `StartDelay` → `Moving` → `Waiting` → `Returning` → `Idle`.

**Components:**
| Component | Access |
|-----------|--------|
| `Position` | Write (interpolated via `glm::mix` during `Moving` and `Returning`) |
| `Mover` | Read/Write (state machine, progress, timer) |

**Context:**
| Context | Access |
|---------|--------|
| `PhysicsConfig` | Read (`fixedDeltaTime`) |

**State machine:**
1. **Idle** — Waiting for trigger activation. No movement.
2. **StartDelay** — Counts down `timer` (set to `startDelay`). Transitions to `Moving` when timer reaches 0.
3. **Moving** — Interpolates `progress` from 0→1 at `speed` units/second. Position = `mix(startPos, endPos, progress)`. Transitions to `Waiting` at progress=1.
4. **Waiting** — Counts down `timer` (set to `waitTime`). Transitions to `Returning` when timer reaches 0.
5. **Returning** — Interpolates `progress` from 1→0. Transitions to `Idle` at progress=0.

---

### 4. `moverSyncSystem`

**File:** `systems/mover_sync_system.h/.cpp`

**Purpose:** After `moverSystem` updates ECS positions, this pushes those positions into Jolt kinematic bodies using `MoveKinematic`. This is what makes movers physically push the player and other bodies.

**Components:**
| Component | Access |
|-----------|--------|
| `Position` | Read |
| `Mover` | Read (view filter only) |
| `JoltBody` | Read (`id` for the body interface call) |

**Context:**
| Context | Access |
|---------|--------|
| `PhysicsConfig` | Read (`fixedDeltaTime` — Jolt uses this to compute the kinematic body's velocity) |
| `JoltWorld` | Read (`getBodyInterface()`) |

**Key detail:** Uses `MoveKinematic` (not `SetPosition`). `MoveKinematic` tells Jolt the target position for the next physics step — Jolt calculates the velocity needed and sweeps the body, pushing anything in the way. `SetPosition` would teleport without pushing.

---

### 5. `joltSyncSystem`

**File:** `systems/jolt_sync_system.h/.cpp`

**Purpose:** After the Jolt physics step, reads body transforms back into ECS components. This is the bridge from Jolt's simulation results to the ECS world.

**Components:**
| Component | Access |
|-----------|--------|
| `Position` | Write (set from `bodyInterface.GetCenterOfMassPosition()`) |
| `JoltBody` | Read (`id`) |
| `OnGround` | Write (optional — heuristic based on vertical velocity < 0.5) |

**Context:**
| Context | Access |
|---------|--------|
| `JoltWorld` | Read (`getBodyInterface()`) |

**Note:** The `OnGround` heuristic here is a simple fallback for dynamic bodies (cubes). The player uses `CharacterVirtual::GetGroundState()` instead (set in `playerCharacterSystem`).

---

### 6. `combatSystem`

**File:** `systems/combat_system.h/.cpp`

**Purpose:** Handles weapon firing — hitscan raycasting and projectile spawning. Applies damage to entities with `Health` components. Spawns visual tracers for hitscan and projectile entities for rockets/grenades.

**Components:**
| Component | Access |
|-----------|--------|
| `PlayerInput` | Read (`fire`) |
| `WeaponInventory` | Read/Write (current weapon, cooldown) |
| `Ammo` | Read/Write (ammunition counts) |
| `Position` | Read (player position for ray origin) |
| `AABBCollider` | Read (target colliders for hit detection) |
| `Health` | Write (damage application) |

**Context:**
| Context | Access |
|---------|--------|
| `CombatResources` | Read (VAO, shaders, textures for spawned entities) |
| `glm::vec3` (camera front) | Read (firing direction) |
| `PhysicsConfig` | Read (`fixedDeltaTime` for cooldowns) |

**Also takes:** `const Level&` parameter for hitscan ray-vs-level intersection.

---

### 7. `lifetimeSystem`

**File:** `systems/lifetime_system.h/.cpp`

**Purpose:** Counts down `Lifetime::remaining` each tick. Destroys entities when they expire. Used for projectiles, tracers, and visual effects.

**Components:**
| Component | Access |
|-----------|--------|
| `Lifetime` | Read/Write (decrement `remaining`, check <= 0) |

**Context:**
| Context | Access |
|---------|--------|
| `PhysicsConfig` | Read (`fixedDeltaTime`) |

---

### 8. `triggerSystem`

**File:** `systems/trigger_system.h/.cpp`

**Purpose:** Detects AABB overlap between trigger volumes and the player. Executes actions: activate movers, teleport, damage, heal, change level, display message.

**Components:**
| Component | Access |
|-----------|--------|
| `Position` | Read (trigger and player) |
| `AABBCollider` | Read (trigger and player extents) |
| `TriggerVolume` | Read/Write (action, target, cooldown, triggered flag) |
| `TagPlayer` | Read (view filter — only checks player overlap) |
| `Mover` | Write (sets state to `Moving` or `StartDelay` via target entity) |
| `Velocity` | Write (zeroed on teleport) |
| `Health` | Write (damage/heal actions) |

**Context:**
| Context | Access |
|---------|--------|
| `PhysicsConfig` | Read (`fixedDeltaTime` for cooldown and damage-per-second) |

**Note:** Uses ECS-level AABB overlap, not Jolt's contact listener. Jolt sensor bodies exist for the triggers but aren't queried here — the ECS overlap works fine since positions are synced.

---

### 9. `demoResetSystem`

**File:** `systems/demo_reset_system.h/.cpp`

**Purpose:** Periodically resets demo entities (physics cubes) back to their starting positions. Teleports both the ECS position and the Jolt body.

**Components:**
| Component | Access |
|-----------|--------|
| `Position` | Write (reset to `startPosition`) |
| `Velocity` | Write (reset to `startVelocity`) |
| `DemoReset` | Read/Write (timer, interval, start values) |
| `OnGround` | Write (set to false so gravity applies) |
| `JoltBody` | Read (`id` for `SetPosition` and `SetLinearVelocity`) |

**Context:**
| Context | Access |
|---------|--------|
| `PhysicsConfig` | Read (`fixedDeltaTime`) |
| `JoltWorld` | Read (`getBodyInterface()`) |

---

### 10. `renderSystem`

**File:** `systems/render_system.h/.cpp`

**Purpose:** Iterates all entities with `MeshRenderer` and draws them. Sets up view/projection matrices from the camera. Handles lighting uniforms (directional + point lights). Draws `TagDebugWireframe` entities in wireframe mode.

**Components:**
| Component | Access |
|-----------|--------|
| `Position` | Read |
| `Scale` | Read (optional) |
| `MeshRenderer` | Read (VAO, shader, texture, index count) |
| `Colour` | Read (optional — tint colour) |
| `DirectionalLight` | Read |
| `PointLight` | Read |
| `TagDebugWireframe` | Read (draws in `GL_LINE` mode) |

**Parameters:** Takes `Camera&` and `aspectRatio` directly (not from registry).

---

### 11. `debugHudSystem`

**File:** `systems/debug_hud_system.h/.cpp`

**Purpose:** Renders a text-based HUD overlay showing FPS, player position, health, current weapon, and ammo. Uses `stb_easy_font` for text rendering.

**Components:**
| Component | Access |
|-----------|--------|
| `Position` | Read (player position display) |
| `Health` | Read (health display) |
| `WeaponInventory` | Read (current weapon display) |
| `Ammo` | Read (ammo counts display) |
| `TagPlayer` | Read (view filter) |

**Context:**
| Context | Access |
|---------|--------|
| `HudConfig` | Read (`shaderId` for the HUD shader) |

**Parameters:** Takes `windowWidth`, `windowHeight`, `fps` directly.

---

## Archived Systems (Not Compiled)

These systems were replaced by Jolt Physics in Chapters 14-15. They live in `systems/archived/` for reference.

| System | Replaced By |
|--------|-------------|
| `collisionSystem` | Jolt rigid body solver + `CharacterVirtual::ExtendedUpdate` |
| `physicsSystem` | Jolt gravity + `CharacterVirtual` (player) / Jolt bodies (objects) |
| `groundDetectionSystem` | `CharacterVirtual::GetGroundState()` (player) / `joltSyncSystem` heuristic (objects) |
| `movementSystem` | Jolt body simulation + `joltSyncSystem` |
| `playerMovementSystem` | `playerCharacterSystem` |
