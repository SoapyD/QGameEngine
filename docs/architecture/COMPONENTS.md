# QEngine — ECS Components Reference

All components are plain data structs. No methods, no inheritance, no behaviour. Components live under `src/engine/ecs/components/` (grouped `core` / `physics` / `combat` / `gameplay` / `rendering` / `tags`), re-exported by the `components.h` barrel.

---

## Spatial Components

### `Position`
```cpp
struct Position {
    glm::vec3 value = glm::vec3(0.0f);
};
```
World-space position. Nearly every entity has one. Written by `playerCharacterSystem`, `joltSyncSystem`, `moverSystem`, `demoResetSystem`, `triggerSystem` (teleport).

### `PrevPosition`
```cpp
struct PrevPosition {
    glm::vec3 value = glm::vec3(0.0f);
};
```
Position at the start of the *previous* fixed tick. `main.cpp` snapshots `Position` into this before each `stepSimulation`, and the renderer lerps between the two by the fixed-timestep alpha so motion is smooth above the 60 Hz tick rate.

### `Rotation`
```cpp
struct Rotation {
    glm::vec3 euler = glm::vec3(0.0f); // pitch, yaw, roll in degrees
};
```
Euler angles. Currently unused by systems — reserved for future entity rotation.

### `Scale`
```cpp
struct Scale {
    glm::vec3 value = glm::vec3(1.0f);
};
```
Multiplied into the model matrix by `renderSystem`. Used to size entities visually (e.g. shelf is `4x2x4`, lights are `0.2x0.2x0.2`).

### `Velocity`
```cpp
struct Velocity {
    glm::vec3 value = glm::vec3(0.0f);
};
```
Used for initial velocity of dynamic bodies (passed to Jolt at body creation). Also zeroed by `triggerSystem` on teleport. `demoResetSystem` resets it to `startVelocity`.

### `Vertex`
```cpp
struct Vertex {
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 normal = glm::vec3(0.0f);
    glm::vec2 texCoords = glm::vec2(0.0f);
};
```
Mesh vertex data. Not an ECS component — used by the mesh/renderer layer.

---

## Input Components

### `PlayerInput`
```cpp
struct PlayerInput {
    bool fire = false;
    int weaponSwitch = -1;              // -1 = no switch, 0+ = weapon slot
    glm::vec3 wishDir = glm::vec3(0.0f); // desired move direction (normalised)
    bool jump = false;
};
```
Written each frame from `InputManager` (pre-tick). **Read by:** `playerCharacterSystem` (`wishDir`, `jump`), `combatSystem` (`fire`), `weaponSwitchSystem` (`weaponSwitch`).

### `CameraDirection`
```cpp
struct CameraDirection {
    glm::vec3 value = glm::vec3(0.0f, 0.0f, -1.0f);
};
```
The player's view/aim direction, published to the registry **context** each frame. `combatSystem` reads it as the firing direction. A named type replaces the old fragile bare-`glm::vec3`-in-context coupling.

---

## Physics Components

### `JoltBody`
```cpp
struct JoltBody {
    JPH::BodyID id;
};
```
Links an ECS entity to a Jolt rigid body. The `id` is used to query position, velocity, and apply forces via `BodyInterface`. Created by `createDynamicBody`, `createKinematicBody`, `createStaticBody`, `createSensorBody`.

**Used by:** `joltSyncSystem` (read position), `moverSyncSystem` (MoveKinematic), `demoResetSystem` (teleport body).

### `JoltCharacter`
```cpp
struct JoltCharacter {
    JPH::Ref<JPH::CharacterVirtual> character;
};
```
Links an entity to a Jolt `CharacterVirtual` controller. The `character` ref is used to set velocity, call `ExtendedUpdate`, read position, and check ground state. Carried by **the player** (created by `initPlayerCharacter`, no inner body) and by **enemies** (created by `initEnemyCharacters`, *with* a kinematic inner body on `Layers::MOVING` so the enemy blocks the player and other enemies). Driven by `playerCharacterSystem` / `aiSystem` respectively.

**Used by:** `playerCharacterSystem` only.

### `AABBCollider`
```cpp
struct AABBCollider {
    glm::vec3 halfExtents = glm::vec3(0.5f);
    bool isTrigger = false;
    uint32_t layer = CollisionLayers::World;
    uint32_t mask = CollisionLayers::All;
};
```
Axis-aligned bounding box. `halfExtents` defines the box size from centre. `isTrigger` marks volumes that detect overlap without blocking. `layer` and `mask` are legacy collision layer bits from the old system — Jolt uses its own `ObjectLayer` system instead.

**Used by:** `triggerSystem` (AABB overlap), `playerCharacterSystem` (capsule shape dimensions), Jolt body creation functions (box shape dimensions), `renderSystem` (camera eye height).

> **Removed:** the old `Gravity` component was deleted in the 2026-06-08 eval
> cleanup. Player gravity is now applied inside `playerCharacterSystem`
> (`-20.0f * dt`); dynamic bodies use Jolt's world gravity (`-20 m/s^2`).

### `OnGround`
```cpp
struct OnGround {
    bool value = false;
};
```
Whether the entity is touching ground. For the player, set by `playerCharacterSystem` from `CharacterVirtual::GetGroundState()`. For dynamic bodies, set by `joltSyncSystem` using a velocity heuristic. Reset to `false` by `demoResetSystem`.

### `CharacterPhysics`
```cpp
struct CharacterPhysics {
    float groundFriction = 6.0f;
    float airFriction = 0.1f;
    float maxGroundSpeed = 7.0f;
    float maxAirSpeed = 1.0f;
    float groundAcceleration = 10.0f;
    float airAcceleration = 10.0f;
    float jumpForce = 8.0f;
    float stepHeight = 0.7f;
    float maxHorizontalSpeed = 20.0f;
};
```
Tuning values for Quake-style movement. All values are read by `playerCharacterSystem`. Ground accel/friction is computed in the **ground's reference frame** so a moving platform's velocity is inherited exactly once (fixes speed run-away on horizontal movers).

| Field | Effect |
|-------|--------|
| `groundFriction` | How quickly the player decelerates on ground (higher = snappier stops) |
| `airFriction` | Not currently used by `playerCharacterSystem` (air has no friction in Quake movement) |
| `maxGroundSpeed` | Maximum horizontal speed on ground (units/second) |
| `maxAirSpeed` | Air speed cap — kept at 1.0 to enable bunny hopping |
| `groundAcceleration` | How quickly the player reaches max speed |
| `airAcceleration` | Air control responsiveness |
| `jumpForce` | Vertical velocity applied on jump |
| `stepHeight` | Maximum height for automatic stair stepping (Jolt `ExtendedUpdate`) |
| `maxHorizontalSpeed` | Anti-runaway ceiling on the player's *own* horizontal speed (platform carry excluded). Generous so bunny-hopping still feels fast |

### `SpawnPoint`
```cpp
struct SpawnPoint {
    glm::vec3 position = glm::vec3(0.0f);
    float yaw = 0.0f; // facing direction on respawn (degrees)
};
```
The player's respawn location + facing. **Used by:** `playerDeathSystem` (teleport the player here and grant invulnerability when `Health` hits 0).

---

## Weapon Components

### `WeaponType` (enum)
```cpp
enum class WeaponType {
    Shotgun, SuperShotgun, Nailgun, RocketLauncher,
    GrenadeLauncher, LighteningGun, Railgun
};
```

### `FireMode` (enum)
```cpp
enum class FireMode { Hitscan, Projectile };
```

### `Weapon`
```cpp
struct Weapon {
    WeaponType type;
    FireMode fireMode;
    float damage = 10.0f;
    float fireRate = 0.5f;
    float cooldownRemaining = 0.0f;
    float range = 1000.0f;
    float spread = 0.0f;
    int pelletCount = 1;
    float projectileSpeed = 0.0f;
    float splashRadius = 0.0f;
    float splashDamage = 0.0f;
    int ammoPerShot = 1;
};
```
Weapon stats. Created by `createWeapon()` in `weapon_definitions.h`. Used by `combatSystem`.

### `WeaponInventory`
```cpp
struct WeaponInventory {
    std::vector<Weapon> weapons;
    int currentWeapon = 0;
};
```
**Used by:** `weaponSwitchSystem` (write `currentWeapon`), `combatSystem` (read current weapon stats), `debugHudSystem` (display).

### `Ammo`
```cpp
struct Ammo {
    int shells = 0;
    int nails = 0;
    int rockets = 0;
    int cells = 0;
};
```
**Used by:** `combatSystem` (decrement on fire), `debugHudSystem` (display).

### `Armor`
```cpp
struct Armor {
    float current = 0.0f;
    float max = 100.0f;
};
```
Secondary defence stat. In v1 it is filled by the `item_armor` pickup and shown on the HUD armour bar; damage *absorption* is a Phase-2 follow-on (see the item-pickups plan). **Used by:** `pickupSystem` (fill), `debugHudSystem` (armour bar).

### `Projectile`
```cpp
struct Projectile {
    float damage;
    float splashRadius;
    float splashDamage;
    entt::entity owner = entt::null;   // who fired it (kill credit; owner never self-hits)
    Faction faction = Faction::Player; // Player/Enemy — friendly-fire guard
};
```
Attached to projectile entities spawned by `fireProjectile`. Used for hit detection and splash damage. `faction` is derived from the shooter (enemies fire `Enemy`, everything else `Player`); `updateProjectiles` skips same-faction targets (and other projectiles), so enemy bolts never hurt enemies and player shots never hurt the player.

---

## State Components

### `Health`
```cpp
struct Health {
    float current;
    float max;
    float invulnerableTimer = 0.0f; // seconds of remaining invulnerability
};
```
`invulnerableTimer` gives brief post-respawn immunity. **Used by:** `triggerSystem` (damage/heal), `combatSystem` (damage), `playerDeathSystem` (respawn + set invuln), `debugHudSystem` (display).

### `DamageFlash`
```cpp
struct DamageFlash {
    float timer = 0.0f;    // remaining flash time
    float duration = 0.3f; // total flash length (seconds)
};
```
"Recently hit" timer. Set by `applyDamage` on any target that has it. **For the player** it drives the red full-screen overlay (ticked + drawn by `debugHudSystem`). **For enemies** (the grunt) it drives a brief flat-white model flash: `renderSystem` overrides the colour while `timer > 0`, and `enemyDeathSystem` fades the timer.

### `PendingKnockback`
```cpp
struct PendingKnockback {
    glm::vec3 impulse = glm::vec3(0.0f);
};
```
An impulse buffered for the player to apply on the next tick (e.g. rocket splash). Written by `combatSystem`, consumed by `playerCharacterSystem`.

### `MoverState` (enum)
```cpp
enum class MoverState {
    Idle, StartDelay, Moving, Waiting, Returning
};
```

### `Mover`
```cpp
struct Mover {
    glm::vec3 startPos;
    glm::vec3 endPos;
    float speed = 2.0f;
    float waitTime = 3.0f;
    float startDelay = 0.0f;
    float timer = 0.0f;
    float progress = 0.0f;
    MoverState state = MoverState::Idle;
    bool requiresTrigger = true;
};
```
Drives doors and lifts through a position interpolation state machine.

| Field | Purpose |
|-------|---------|
| `startPos` / `endPos` | Start and end positions for the movement |
| `speed` | Units per second |
| `waitTime` | Seconds to stay at `endPos` before returning |
| `startDelay` | Seconds to wait after trigger before starting to move |
| `timer` | Shared countdown used by `StartDelay` and `Waiting` states |
| `progress` | 0.0 (at start) to 1.0 (at end) — drives `glm::mix` |
| `state` | Current state machine state |
| `requiresTrigger` | If true, stays `Idle` until a `TriggerVolume` activates it |

**Used by:** `moverSystem` (state machine), `moverSyncSystem` (view filter), `triggerSystem` (activate).

### `TriggerAction` (enum)
```cpp
enum class TriggerAction {
    ActivateMover, Teleport, Damage, Heal, ChangeLevel, Message
};
```

### `TriggerVolume`
```cpp
struct TriggerVolume {
    TriggerAction action = TriggerAction::ActivateMover;
    entt::entity target = entt::null;
    glm::vec3 destination;
    float value = 0.0f;
    std::string message;
    bool onlyOnce = false;
    bool triggered = false;
    float cooldown = 0.0f;
    float cooldownTimer = 0.0f;
};
```
**Used by:** `triggerSystem` only.

### `Lifetime`
```cpp
struct Lifetime {
    float remaining = 5.0f;
};
```
Auto-destroys the entity when `remaining` reaches 0. **Used by:** `lifetimeSystem`.

### `DemoReset`
```cpp
struct DemoReset {
    glm::vec3 startPosition;
    glm::vec3 startVelocity = glm::vec3(0.0f);
    float interval = 5.0f;
    float timer = 0.0f;
};
```
Periodically resets demo entities to their starting state. **Used by:** `demoResetSystem`.

---

## Item Pickup Components

### `PickupType` (enum)
```cpp
enum class PickupType {
    Health, Shells, Nails, Rockets, Cells, Armor, Weapon
};
```

### `Pickup`
```cpp
struct Pickup {
    PickupType type = PickupType::Health;
    int amount = 0;                              // health/armour/ammo granted
    WeaponType weaponType = static_cast<WeaponType>(0); // only when type == Weapon
};
```
A sensor entity that grants an effect to a `TagTriggerable` toucher, then is consumed. **Used by:** `pickupSystem` (ECS overlap → grant → destroy). Weapon pickups grant the weapon if not held, else top up its ammo.

### `PickupMessage`
```cpp
struct PickupMessage {
    std::string text;
    float timer = 0.0f;    // remaining display time (seconds)
    float duration = 2.5f; // total display length
};
```
Transient on-screen toast ("Picked up …"). `pickupSystem` sets `text`+`timer` on the receiving player; `debugHudSystem` draws it centred and fades it out.

---

## Enemy Components

### `AIStateKind` (enum)
```cpp
enum class AIStateKind { Idle, Chase, Attack, Dead };
```
Only `Idle` is produced by the AI *setup* — the state machine that uses the rest lands in the AI behaviour plan.

### `AIState`
```cpp
struct AIState {
    AIStateKind  state = AIStateKind::Idle;
    float        attackCooldown = 0.0f;   // seconds until the next attack
    entt::entity target = entt::null;     // who to chase/attack (behaviour)
};
```
Marks an entity as an enemy (the `monster_grunt` archetype) and holds its behaviour state. `target` doubles as the aggro flag — `null` until the grunt sees the player, then the player entity until they escape pursue range. **Written by:** `aiSystem` (aggro, state machine, attack cooldown). **Read by:** `enemyDeathSystem`. `buildWorld` (`initEnemyCharacters`) gives every `AIState` entity a `JoltCharacter` (`CharacterVirtual` + kinematic inner body) so it stands on the floor, blocks the player, and is driven with collided locomotion by `aiSystem`.

### `AIPath`
```cpp
struct AIPath {
    std::vector<glm::vec3> waypoints;
    size_t index = 0;          // current waypoint being walked toward
    float  repathTimer = 0.0f; // recompute the path at ≤ 0
};
```
The A\* route the grunt is following toward its target, over the `NavGrid`. **Used by:** `aiSystem` — recomputed on a timer / when the path runs out, and followed by driving the enemy's `CharacterVirtual` from waypoint to waypoint (so the enemy routes *around* walls and props, and collides rather than clips on corner-cuts).

### `RangedAttack`
```cpp
struct RangedAttack {
    float range = 16.0f;           // fire when the player is within this (and visible)
    float standoffMin = 7.0f;      // back off if the player closes inside this
    float damage = 10.0f;
    float projectileSpeed = 12.0f; // slow enough to dodge
    float windup = 0.5f;           // telegraph before firing (seconds)
    float cooldown = 1.6f;         // seconds between shots
    float windupTimer = 0.0f;      // >0 while telegraphing the current shot
};
```
Present on ranged enemies (the `monster_ranged` archetype); absent → melee-only. **Used by:** `aiSystem`'s ranged branch — holds the enemy at standoff range, telegraphs (`windup`), then fires a dodgeable Enemy-faction bolt (`aiFireEnemyBolt` → `fireProjectile`). See also [SYSTEMS.md](SYSTEMS.md) `aiSystem`.

---

## Rendering Components

### `MeshRenderer`
```cpp
struct MeshRenderer {
    unsigned int vao = 0;
    unsigned int vertexCount = 0;
    unsigned int shaderId = 0;
    unsigned int textureId = 0;
    bool useIndices = false;
    unsigned int indexCount = 0;
};
```
Everything `renderSystem` needs to draw an entity. Stores OpenGL handles (VAO, shader program ID, texture ID). Most entities use indexed drawing (`useIndices = true`).

### `Colour`
```cpp
struct Colour {
    glm::vec4 value = glm::vec4(1.0f);
};
```
Flat albedo colour. When present, `renderSystem` draws the entity **lit but untextured** in this
colour (via the lit shader's `useAlbedo` path) instead of sampling a texture. Used by weapon
pickups, which render as coloured gun meshes.

---

## Lighting Components

### `DirectionalLight`
```cpp
struct DirectionalLight {
    glm::vec3 direction = glm::vec3(-0.2f, -1.0f, -0.3f);
    glm::vec3 color = glm::vec3(1.0f);
    float ambientStrength = 0.1f;
};
```
Global sun light. Read by `renderSystem`.

### `PointLight`
```cpp
struct PointLight {
    glm::vec3 color = glm::vec3(1.0f);
    float ambientStrength = 0.05f;
    float linear = 0.09f;
    float quadratic = 0.032f;
};
```
Positional light with attenuation. Paired with a `Position` component. Read by `renderSystem`. `linear` and `quadratic` control falloff.

---

## Tags

Tags are empty structs that mark entities without adding data. Used as view filters.

### `TagPlayer`
```cpp
struct TagPlayer {};
```
Marks the player entity. Used by `triggerSystem` (only check player overlap), `debugHudSystem` (display player stats), `renderSystem` (camera follow).

### `TagDebugWireframe`
```cpp
struct TagDebugWireframe {};
```
Marks entities that should render in wireframe mode (`GL_LINE`). Used for trigger volume debug visualisation.

### `TagTriggerable`
```cpp
struct TagTriggerable {};
```
Marks an entity that can activate `TriggerVolume`s and collect `Pickup`s. Currently only the player carries it, but `triggerSystem`/`pickupSystem` key off this tag rather than `TagPlayer` so enemies/props can be made triggerable without touching the trigger logic.

---

## Registry Context Objects

These are singletons stored in `registry.ctx()`, not attached to entities.

| Type | Purpose | Set by | Read by |
|------|---------|--------|---------|
| `PhysicsConfig` | `fixedDeltaTime`, `terminalVelocity` | `main.cpp` (init) | Most systems |
| `JoltWorld` | Jolt `PhysicsSystem`, allocator, job system | `main.cpp` (init) | Physics systems |
| `HudConfig` | HUD shader ID | `main.cpp` (init) | `debugHudSystem` |
| `HudSignals` | Crosshair gap, recoil, hit/kill/damage-dir timers, low-ammo flag, `showDebug` | `buildWorld`; recomputed each tick by `hudSignalSystem`, event markers set by `combatSystem`/`aiSystem` | `debugHudSystem` (draws it) |
| `CombatResources` | VAO, shader, texture IDs for projectile/tracer spawning | `setupScene` | `combatSystem` |
| `CameraDirection` | Camera front direction (for weapon firing) | `main.cpp` (each frame) | `combatSystem` |
| `SoundQueue` | One-frame queue of `SoundEvent`s (`{id, pos, positional}`) | any simulation system via `queueSound()` | `audioSystem` (windowed build; drained + played) |
| `NavGrid` | Enemy walkability grid (blocked cells over the level's XZ), built from walls + solid props | `buildWorld` (`buildNavGrid`) | `aiSystem` (A\* pathfinding) |
