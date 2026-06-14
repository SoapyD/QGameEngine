# Process: Combat (Weapons)

**Purpose:** Fire weapons — hitscan raycasts and spawned projectiles — apply
damage to `Health` entities, and spawn visual effects.

**Systems:** `weaponSwitchSystem` → `combatSystem` → `lifetimeSystem`.
`src/engine/ecs/systems/combat_system.{h,cpp}`, `weapon_switch_system.h`,
`lifetime_system.{h,cpp}`. Weapon stats: `ecs/weapon_definitions.h`
(`createWeapon()` factory).

## Flow (per tick)

```
weaponSwitchSystem: PlayerInput.weaponSwitch → WeaponInventory.currentWeapon
                    (runs first so the right weapon fires this tick)
            │
combatSystem (if PlayerInput.fire && cooldown ready && Ammo > 0):
   hitscan (shotgun):  ray from Position along camera-front
                       → ray-vs-Level + ray-vs-AABBCollider
                       → apply damage to Health; spawn tracer entity
   projectile (rocket): spawn entity with Velocity + Lifetime + MeshRenderer
            │
lifetimeSystem: decrement Lifetime.remaining; destroy at <= 0
                (tracers, projectiles, effects)
```

## Weapons today

| Weapon | Type | Notes |
|--------|------|-------|
| Shotgun | hitscan | ray vs level + colliders, instant damage, tracer |
| Rocket launcher | projectile | spawns moving entity with lifetime |

## Components

| Component | Access |
|-----------|--------|
| `PlayerInput` | read (`fire`) |
| `WeaponInventory` | read/write (current weapon, cooldown) |
| `Ammo` | read/write |
| `Position` | read (ray origin) |
| `AABBCollider` | read (targets) |
| `Health` | write (damage) |
| `Lifetime` | read/write (effects/projectiles) |

**Context:** `CombatResources` (VAO/shaders/textures for spawned entities),
`glm::vec3` camera-front (fire direction), `PhysicsConfig` (cooldowns).
`combatSystem` also takes `const Level&` for hitscan-vs-geometry.

See also: [`../architecture/SYSTEMS.md`](../architecture/SYSTEMS.md#6-combatsystem),
[status](../status/combat.md).
