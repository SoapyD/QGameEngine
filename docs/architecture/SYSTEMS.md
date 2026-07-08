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
- **Platform carry:** ground accel/friction runs in the ground's reference frame, so a moving kinematic platform's velocity is inherited exactly once (no run-away on horizontal movers). Horizontal speed is clamped to `CharacterPhysics.maxHorizontalSpeed` as a backstop.
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

**Purpose:** Handles weapon firing — hitscan raycasting and projectile spawning. Applies damage to entities with `Health` components. Spawns visual tracers for hitscan and projectile entities for rockets/grenades. Projectiles carry a `Faction` (derived from the shooter); `updateProjectiles` skips same-faction targets and other projectiles, so enemy bolts can't hurt enemies, player shots can't hurt the player, and bolts pass through each other. `raycastEntities` likewise ignores projectiles (they neither block sightlines nor are shootable). Enemy ranged fire reuses `fireProjectile` via `aiSystem`, not this system's player-input path.

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

**Note:** Uses ECS-level AABB overlap, not Jolt's contact listener. `buildWorld` deliberately does **not** create Jolt sensor bodies for triggers — they were never queried and would be spuriously hit by combat's impulse sweep. The ECS overlap works because positions are synced by `joltSyncSystem` first.

---

### 9. `pickupSystem`

**File:** `systems/pickup/pickup_system.h/.cpp`

**Purpose:** ECS AABB overlap between `Pickup` sensor entities and `TagTriggerable` touchers. On overlap, grants the effect (health / ammo pool / armour / weapon), sets a `PickupMessage` toast, and destroys the pickup.

**Components:**
| Component | Access |
|-----------|--------|
| `Position` | Read (pickup + toucher) |
| `AABBCollider` | Read (overlap extents) |
| `Pickup` | Read (type/amount/weaponType) |
| `TagTriggerable` | Read (view filter — who can collect) |
| `Health` / `Ammo` / `Armor` / `WeaponInventory` | Write (grant) |
| `PickupMessage` | Write (toast text/timer) |

**When:** Fixed tick, after `triggerSystem` (positions already synced).

---

### 10. `playerDeathSystem`

**File:** `systems/player/player_death_system.h/.cpp`

**Purpose:** When the player's `Health` reaches 0, respawns them at their `SpawnPoint` and grants a short `Health::invulnerableTimer`.

**Components:**
| Component | Access |
|-----------|--------|
| `Health` | Read/Write (detect ≤ 0, restore, set invuln) |
| `SpawnPoint` | Read (respawn position + yaw) |
| `Position` | Write (teleport to spawn) |
| `JoltCharacter` | Write (move the CharacterVirtual) |
| `TagPlayer` | Read (view filter) |

**When:** Fixed tick, after `pickupSystem`, before `enemyDeathSystem`.

---

### 11. `aiSystem`

**File:** `systems/enemy/ai_system.h/.cpp`

**Purpose:** Enemy behaviour. For each `AIState` entity: **aggro** the player on sight (detect range + clear line of sight; `target` latches until they escape pursue range), run the `Idle → Chase → Attack` state machine, and attack on a cooldown. In **Chase** it **pathfinds** (A\* over the `NavGrid`, stored in `AIPath`, recomputed on a timer) and drives its `CharacterVirtual` toward each waypoint, so it routes *around* walls and props **and** — because locomotion is now collided (`ExtendedUpdate`), not a kinematic sweep — no longer clips a wall on a corner-cut. **Attack** is melee (`applyDamage`) by default; an enemy carrying a `RangedAttack` instead **holds at standoff range**, telegraphs (`windup`), then fires a dodgeable Enemy-faction bolt (`aiFireEnemyBolt` → `fireProjectile`), backing off if the player closes inside `standoffMin`. Line-of-sight tests level surfaces + solid entities via `aiClearLineOfSight` (projectiles are excluded, so an enemy's own bolt never blocks its sight).

**Helpers (split files, CODING_STANDARD §4):** `ai_step_character.cpp` (`aiStepCharacter` — collided locomotion), `ai_line_of_sight.cpp` (`aiClearLineOfSight`), `ai_fire_bolt.cpp` (`aiFireEnemyBolt`), all declared in `ai_support.h`.

**Initialisation:** Call `initEnemyCharacters(registry)` once after level bodies exist (in `buildWorld`). Like `initPlayerCharacter`, it builds a `CapsuleShape` `CharacterVirtual` from each enemy's `AABBCollider` — plus a kinematic **inner body** on `Layers::MOVING` so the enemy still blocks the player and separates from other enemies. `aiSystem` owns the enemy's `Position` (it writes back `CharacterVirtual::GetPosition()`); `joltSyncSystem` skips enemies since they have no `JoltBody`.

**Components:**
| Component | Access |
|-----------|--------|
| `AIState` | Read/Write (aggro target, state, attack cooldown) |
| `AIPath` | Read/Write (A\* waypoints + follow cursor) |
| `RangedAttack` | Read/Write (optional — standoff/windup/fire; melee-only if absent) |
| `Position` | Read/Write (self, from `CharacterVirtual`; player read) |
| `JoltCharacter` | Read (drive `ExtendedUpdate`, read position/ground state) |
| `Rotation` | Write (face movement/target) |
| `TagPlayer` | Read (locate the target) |
| `Health` | Write (attack damages the player, via `applyDamage`) |

**Context:**
| Context | Access |
|---------|--------|
| `CombatResources` | Read (spawn ranged bolts via `fireProjectile`) |
| `PhysicsConfig` | Read (`fixedDeltaTime`) |
| `JoltWorld` | Read (`physicsSystem` filters + `tempAllocator` for `ExtendedUpdate`) |
| `NavGrid` | Read (walkability grid for A\*) |

**Also takes:** `const Level&` (line-of-sight vs. walls). Pathfinding lives in `engine/ai/` (`build_nav_grid`, `find_path`). Repaths are capped per tick.

**When:** Fixed tick, after `moverSyncSystem` and **before** `joltWorld.step()`. The character's `ExtendedUpdate` moves it (and its inner body) immediately, so the player's own `ExtendedUpdate` later in the tick collides against the enemy's current-tick position. Reads last tick's player position.

---

### 12. `enemyDeathSystem`

**File:** `systems/enemy/enemy_death_system.h/.cpp`

**Purpose:** Per-tick enemy upkeep that isn't behaviour: fades each enemy's `DamageFlash` timer (the white hit-blink), and removes enemies whose `Health` reached 0 — plays `combat.explosion_small`, drops the entity. Destroying the entity erases its `JoltCharacter`, whose destructor removes and destroys the inner body — no manual body teardown. Chase/attack behaviour is a separate system (`aiSystem`).

**Components:**
| Component | Access |
|-----------|--------|
| `AIState` | Read (view filter — marks enemies) |
| `DamageFlash` | Read/Write (fade the hit-flash timer) |
| `Health` | Read (≤ 0 → dead) |
| `JoltCharacter` | (erased on destroy → inner body auto-removed) |

**Context:**
| Context | Access |
|---------|--------|
| `PhysicsConfig` | Read (`fixedDeltaTime` for the flash fade) |
| `JoltWorld` | Read (`getBodyInterface()` to remove the body) |

**When:** Fixed tick, after `playerDeathSystem`, before `demoResetSystem`.

---

### 12. `demoResetSystem`

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

### 13. `hudSignalSystem`

**File:** `systems/hud/hud_signal_system.h/.cpp`

**Purpose:** Recompute the player-facing HUD *state* each fixed tick into the `HudSignals` context: the crosshair gap (base + weapon spread + horizontal movement + a recoil kick *derived* from the current weapon's cooldown), the low-ammo flag, and the decay of the transient hit/kill/damage-direction timers. The **markers themselves are set at their event sites** — `fireHitscan`/`updateProjectiles` set the hit/kill marker when a player shot damages/kills an enemy; `updateProjectiles` (enemy bolt) and `aiSystem` (melee) set the damage-direction vector when the player is hit. Runs last in the sim tick, **before render**, so the state is fully computed for `debugHudSystem` to draw — and, crucially, is exercised **headless** (the `hud_signals` scenario asserts this ctx state; the GL HUD only draws it).

**Context:** `HudSignals` (write), `PhysicsConfig` (read `fixedDeltaTime`). **Components:** player `WeaponInventory`/`Ammo`/`JoltCharacter` (read, for spread/low-ammo/speed).

---

### 14. `renderSystem`

**File:** `systems/render_system.h/.cpp`

**Purpose:** Iterates all entities with `MeshRenderer` and draws them. Sets up view/projection matrices from the camera. Handles lighting uniforms (directional + point lights). Draws `TagDebugWireframe` entities in wireframe mode.

**Components:**
| Component | Access |
|-----------|--------|
| `Position` | Read |
| `Scale` | Read (optional) |
| `MeshRenderer` | Read (VAO, shader, texture, index count) |
| `Colour` | Read (optional — flat lit albedo colour, untextured; e.g. weapon-pickup gun meshes) |
| `DirectionalLight` | Read |
| `PointLight` | Read |
| `TagDebugWireframe` | Read (draws in `GL_LINE` mode) |

**Parameters:** Takes `Camera&` and `aspectRatio` directly (not from registry).

---

### 15. `debugHudSystem`

**File:** `systems/debug_hud_system.h/.cpp`

**Purpose:** Renders the 2D overlay: FPS text, health + armour bars, ammo readout, **crosshair**, **damage-flash overlay**, and the **pickup toast**. Text uses `stb_easy_font`; bars/crosshair/panels/flash are drawn by the sibling `draw_*` files (`draw_bar`, `draw_ammo`, `draw_crosshair`, `draw_flash_overlay`, `draw_panel`, `draw_text`). Also ticks the `DamageFlash` and `PickupMessage` timers.

**Components:**
| Component | Access |
|-----------|--------|
| `Position` | Read (player position display) |
| `Health` | Read (health bar) |
| `Armor` | Read (armour bar) |
| `WeaponInventory` | Read (current weapon display) |
| `Ammo` | Read (ammo readout) |
| `DamageFlash` | Read/Write (tick timer + draw overlay) |
| `PickupMessage` | Read/Write (tick timer + draw toast) |
| `TagPlayer` | Read (view filter) |

**Context:**
| Context | Access |
|---------|--------|
| `HudConfig` | Read (`shaderId` for the HUD shader) |
| `PhysicsConfig` | Read (`fixedDeltaTime` for flash/toast timers) |

**Parameters:** Takes `windowWidth`, `windowHeight`, `fps` directly.

---

### 16. `audioSystem`

**File:** `systems/audio/audio_system.h/.cpp`

**Purpose:** Drains the `SoundQueue` and plays each `SoundEvent` through the audio engine (miniaudio + stb_vorbis). A **frame system** — runs once per frame in the windowed build only; the headless build leaves the queue unread. Simulation systems enqueue with `queueSound()` / `queueSoundAt()` (weapon fire, pickups, doors, teleport, jump/pain/death) and looping music is started once at setup.

**Context:**
| Context | Access |
|---------|--------|
| `SoundQueue` | Read/Write (drain the frame's events) |

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
