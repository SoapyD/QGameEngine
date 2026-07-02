# Chapter 12: Weapons & Projectiles

## What You'll Learn
- Hitscan weapons — instant-hit guns like shotguns and railguns
- Projectile weapons — rockets, grenades, and other moving objects
- Weapon switching and cooldowns
- Damage dealing through the ECS
- Debug tracers for hitscan and projectile cubes for visual feedback
- How Quake handled its weapon arsenal

---

## Two Types of Weapon

FPS games use two fundamentally different weapon types:

### Hitscan
The shot is instant. When you click, a ray is cast from the camera and whatever it hits takes damage immediately. No travel time.

- Quake: Shotgun, Super Shotgun, Nailgun (debatable), Lightning Gun
- Used for: fast, reliable weapons

### Projectile
A physical entity is spawned and travels through the world. It has a position, velocity, and collider. When it hits something, it deals damage and is destroyed.

- Quake: Rocket Launcher, Grenade Launcher
- Used for: area-denial, splash damage, skill shots

---

## Weapon Components

Add to `components.h`. First, the weapon data types and the inventory:

```cpp
// ─── Weapon Components ────────────────────────────────────────────

enum class WeaponType {
    Shotgun,
    SuperShotgun,
    Nailgun,
    RocketLauncher,
    GrenadeLauncher,
    LightningGun,
    Railgun
};

enum class FireMode {
    Hitscan,
    Projectile
};

struct Weapon {
    WeaponType type;
    FireMode fireMode;
    float damage = 10.0f;
    float fireRate = 0.5f;          // Seconds between shots
    float cooldownRemaining = 0.0f;
    float range = 1000.0f;          // Hitscan max range
    float spread = 0.0f;            // Cone of inaccuracy (radians)
    int pelletCount = 1;            // Shotguns fire multiple pellets
    float projectileSpeed = 0.0f;   // For projectile weapons
    float splashRadius = 0.0f;      // Area of effect damage radius
    float splashDamage = 0.0f;      // Damage at center of splash
    int ammoPerShot = 1;
};

struct WeaponInventory {
    std::vector<Weapon> weapons;
    int currentWeapon = 0;
};

struct Ammo {
    int shells = 0;
    int nails = 0;
    int rockets = 0;
    int cells = 0;
};

// Attached to projectile entities
struct Projectile {
    float damage;
    float splashRadius;
    float splashDamage;
    entt::entity owner = entt::null;  // Who fired it (for kill credit)
};
```

Now, the components that the combat system needs to function. `Lifetime` auto-destroys entities after a duration — used for projectiles and debug tracers. `PlayerInput` is the bridge between GLFW input polling and the ECS:

```cpp
// Auto-destroy after a duration (projectiles, tracers, effects)
struct Lifetime {
    float remaining = 5.0f;
};

// Input state for the player — set each frame from InputManager
struct PlayerInput {
    bool fire = false;
    int weaponSwitch = -1;    // -1 = no switch, 0+ = weapon slot
};
```

`PlayerInput` is deliberately minimal. We're not adding `moveDir` or `lookDelta` yet because our camera still handles movement directly. When we convert to a proper first-person controller in a later chapter, we'll expand this component. For now, it just carries the fire button and weapon switch.

---

## Weapon Definitions

A factory function that creates weapon data. No classes — just filling in a struct. Place this in a new file `src/engine/ecs/weapon_definitions.h` — it's a header-only helper since it's a simple function returning a struct:

### src/engine/ecs/weapon_definitions.h

```cpp
#pragma once

#include "engine/ecs/components.h"

inline Weapon createWeapon(WeaponType type) {
    Weapon w{};
    w.type = type;

    switch (type) {
        case WeaponType::Shotgun:
            w.fireMode = FireMode::Hitscan;
            w.damage = 4.0f;         // Per pellet
            w.fireRate = 0.5f;
            w.range = 500.0f;
            w.spread = 0.05f;        // Small spread cone
            w.pelletCount = 6;
            w.ammoPerShot = 1;
            break;

        case WeaponType::SuperShotgun:
            w.fireMode = FireMode::Hitscan;
            w.damage = 4.0f;
            w.fireRate = 0.8f;
            w.range = 400.0f;
            w.spread = 0.08f;
            w.pelletCount = 14;
            w.ammoPerShot = 2;
            break;

        case WeaponType::Nailgun:
            w.fireMode = FireMode::Projectile;
            w.damage = 9.0f;
            w.fireRate = 0.1f;
            w.projectileSpeed = 30.0f;
            w.ammoPerShot = 1;
            break;

        case WeaponType::RocketLauncher:
            w.fireMode = FireMode::Projectile;
            w.damage = 100.0f;
            w.fireRate = 0.8f;
            w.projectileSpeed = 20.0f;
            w.splashRadius = 5.0f;
            w.splashDamage = 80.0f;
            w.ammoPerShot = 1;
            break;

        case WeaponType::GrenadeLauncher:
            w.fireMode = FireMode::Projectile;
            w.damage = 100.0f;
            w.fireRate = 0.6f;
            w.projectileSpeed = 15.0f;
            w.splashRadius = 5.0f;
            w.splashDamage = 80.0f;
            w.ammoPerShot = 1;
            break;

        case WeaponType::LightningGun:
            w.fireMode = FireMode::Hitscan;
            w.damage = 30.0f;        // Per second (continuous fire)
            w.fireRate = 0.05f;       // Very fast "ticks"
            w.range = 15.0f;          // Short range
            w.spread = 0.0f;
            w.pelletCount = 1;
            w.ammoPerShot = 1;
            break;

        case WeaponType::Railgun:
            w.fireMode = FireMode::Hitscan;
            w.damage = 80.0f;
            w.fireRate = 1.5f;
            w.range = 1000.0f;
            w.spread = 0.0f;
            w.pelletCount = 1;
            w.ammoPerShot = 1;
            break;
    }

    return w;
}
```

---

## Debug Tracers — Seeing Your Shots

Before we write the combat system, we need a way to *see* what's happening. Without visual feedback, we won't know if our weapons are working. We'll use two approaches:

**Hitscan tracer**: When a hitscan weapon fires, we spawn a thin, stretched wireframe cube along the ray. It hangs in the air for a couple of seconds so you can see the shot pattern. Think of it like a laser beam that lingers.

**Projectile cube**: Projectile weapons spawn a small coloured cube entity that flies through the world with velocity. You can watch it travel and collide.

Both approaches reuse our existing cube mesh and lit shader — no new rendering code needed. The `Lifetime` component handles cleanup automatically.

### Spawning a Hitscan Tracer

This helper creates a thin wireframe cube stretched between two points, rotated to align with the fire direction:

```cpp
void spawnTracer(entt::registry& registry,
                 const glm::vec3& start, const glm::vec3& end,
                 const CombatResources& resources)
{
    glm::vec3 diff = end - start;
    float length = glm::length(diff);
    if (length < 0.01f) return;

    glm::vec3 dir = diff / length;
    glm::vec3 midpoint = (start + end) * 0.5f;

    // Calculate Euler angles to align the cube's Z axis with the ray direction
    // Yaw: rotation around Y axis (horizontal angle)
    float yaw = glm::degrees(std::atan2(dir.x, dir.z));
    // Pitch: rotation around X axis (vertical angle)
    float pitch = glm::degrees(-std::asin(dir.y));

    auto tracer = registry.create();
    registry.emplace<Position>(tracer, midpoint);
    registry.emplace<Rotation>(tracer, glm::vec3(pitch, yaw, 0.0f));
    registry.emplace<Scale>(tracer, glm::vec3(0.03f, 0.03f, length));
    registry.emplace<MeshRenderer>(tracer,
        resources.cubeVAO, 0u,
        resources.shaderId, resources.tracerTextureId,
        true, resources.cubeIndexCount);
    registry.emplace<TagDebugWireframe>(tracer);
    registry.emplace<Lifetime>(tracer, 2.0f);  // Hang in the air for 2 seconds
}
```

The tracer is a very thin cube stretched along its Z axis, then rotated so Z points from start to end. We calculate yaw (horizontal angle) and pitch (vertical angle) from the ray direction using `atan2` and `asin` — the same maths you'd use for a look-at function.

`TagDebugWireframe` makes the render system draw it as green wireframe lines, which is exactly what we want for a debug tracer. It lives for 2 seconds so you can step to the side and inspect the shot pattern.

> **Why not `GL_LINES`?** OpenGL can draw lines, but they're always 1 pixel wide and look terrible. A stretched wireframe cube gives us visible thickness and reuses our existing render pipeline. Every commercial engine uses mesh-based tracers, not GL lines.

---

## The Combat System

The combat system needs access to mesh and shader IDs to spawn projectiles and tracers. We pass these through a small struct stored in the registry context — the same pattern we used for `PhysicsConfig`:

### Combat Resources

Add this to `components.h` alongside the other weapon components:

```cpp
// Resources the combat system needs to spawn projectiles and tracers
struct CombatResources {
    unsigned int cubeVAO = 0;
    unsigned int cubeIndexCount = 0;
    unsigned int shaderId = 0;
    unsigned int projectileTextureId = 0;  // Colour for projectile cubes
    unsigned int tracerTextureId = 0;      // Colour for hitscan tracers
};
```

### src/engine/ecs/systems/combat_system.h

```cpp
#pragma once

#include <entt/entt.hpp>
#include "engine/level/level.h"

void combatSystem(entt::registry& registry, const Level& level);
```

### src/engine/ecs/systems/combat_system.cpp

```cpp
#include "engine/ecs/systems/combat_system.h"
#include "engine/ecs/components.h"
#include "engine/physics/raycast.h"
#include "engine/physics/aabb.h"
#include "engine/physics/physics_config.h"
#include <random>

// Random number generator for spread
static std::mt19937 rng(std::random_device{}());

// Apply spread to a direction vector
glm::vec3 applySpread(const glm::vec3& direction, float spread) {
    if (spread <= 0.0f) return direction;

    std::uniform_real_distribution<float> dist(-spread, spread);
    glm::vec3 spreadDir = direction;
    spreadDir.x += dist(rng);
    spreadDir.y += dist(rng);
    spreadDir.z += dist(rng);
    return glm::normalize(spreadDir);
}

// ─── Raycasting against entities ─────────────────────────────────

struct EntityHit {
    entt::entity entity;
    float distance;
    glm::vec3 point;
};

std::optional<EntityHit> raycastEntities(
    entt::registry& registry, const Ray& ray,
    entt::entity ignore, float maxRange)
{
    std::optional<EntityHit> closest;
    float closestDist = maxRange;

    auto view = registry.view<Position, AABBCollider>();
    for (auto [entity, pos, col] : view.each()) {
        if (entity == ignore) continue;
        if (col.isTrigger) continue;

        AABB box = AABB::fromCentreSize(pos.value, col.halfExtents);
        auto hit = rayIntersectionsAABB(ray, box);

        if (hit.has_value() && hit.value() < closestDist) {
            closestDist = hit.value();
            closest = EntityHit{
                entity,
                hit.value(),
                ray.pointAt(hit.value())
            };
        }
    }

    return closest;
}

// ─── Hitscan tracer (debug visualisation) ────────────────────────
// Spawns a thin wireframe cube stretched between start and end,
// rotated to align with the fire direction.

void spawnTracer(entt::registry& registry,
                 const glm::vec3& start, const glm::vec3& end,
                 const CombatResources& resources)
{
    glm::vec3 diff = end - start;
    float length = glm::length(diff);
    if (length < 0.01f) return;

    glm::vec3 dir = diff / length;
    glm::vec3 midpoint = (start + end) * 0.5f;

    // Calculate Euler angles to align the cube's Z axis with the ray direction
    float yaw = glm::degrees(std::atan2(dir.x, dir.z));
    float pitch = glm::degrees(-std::asin(dir.y));

    auto tracer = registry.create();
    registry.emplace<Position>(tracer, midpoint);
    registry.emplace<Rotation>(tracer, glm::vec3(pitch, yaw, 0.0f));
    registry.emplace<Scale>(tracer, glm::vec3(0.03f, 0.03f, length));
    registry.emplace<MeshRenderer>(tracer,
        resources.cubeVAO, 0u,
        resources.shaderId, resources.tracerTextureId,
        true, resources.cubeIndexCount);
    registry.emplace<TagDebugWireframe>(tracer);
    registry.emplace<Lifetime>(tracer, 2.0f);  // Hang in the air for 2 seconds
}

// ─── Splash damage ──────────────────────────────────────────────

void applySplashDamage(entt::registry& registry, const glm::vec3& center,
                        float radius, float maxDamage, entt::entity ignore) {

    auto view = registry.view<Position, Health>();

    for (auto [entity, pos, health] : view.each()) {
        if (entity == ignore) continue;

        float distance = glm::length(pos.value - center);
        if (distance > radius) continue;

        // Linear falloff: full damage at center, zero at edge
        float scale = 1.0f - (distance / radius);
        float damage = maxDamage * scale;
        health.current -= damage;

        // Knockback — push entity away from explosion
        if (registry.all_of<Velocity>(entity)) {
            glm::vec3 pushDir = glm::normalize(pos.value - center);
            float knockback = damage * 0.5f;
            registry.get<Velocity>(entity).value += pushDir * knockback;
        }
    }
}

// ─── Fire hitscan ───────────────────────────────────────────────

void fireHitscan(entt::registry& registry, const Level& level,
                  entt::entity shooter, const Weapon& weapon,
                  const glm::vec3& origin, const glm::vec3& direction,
                  const CombatResources& resources) {

    for (int i = 0; i < weapon.pelletCount; i++) {
        glm::vec3 dir = applySpread(direction, weapon.spread);
        Ray ray{ origin, dir };

        // Check against entities
        auto entityHit = raycastEntities(registry, ray, shooter, weapon.range);

        // Check against level geometry (floors, walls, ceilings)
        float levelDist = weapon.range;
        for (const auto& sector : level.sectors) {
            for (const auto& surface : sector.surfaces) {
                // Build a thin AABB for the surface
                AABB surfBox;
                surfBox.min = glm::min(
                    glm::min(surface.vertices[0], surface.vertices[1]),
                    glm::min(surface.vertices[2], surface.vertices[3]));
                surfBox.max = glm::max(
                    glm::max(surface.vertices[0], surface.vertices[1]),
                    glm::max(surface.vertices[2], surface.vertices[3]));
                // Inflate thin axes so the AABB has volume
                surfBox.min -= glm::vec3(0.05f);
                surfBox.max += glm::vec3(0.05f);

                auto surfHit = rayIntersectionsAABB(ray, surfBox);
                if (surfHit.has_value() && surfHit.value() < levelDist) {
                    levelDist = surfHit.value();
                }
            }
        }

        // Determine hit point for the tracer
        glm::vec3 hitPoint;
        if (entityHit.has_value() && entityHit->distance < levelDist) {
            hitPoint = entityHit->point;

            // Apply damage
            if (registry.all_of<Health>(entityHit->entity)) {
                registry.get<Health>(entityHit->entity).current -= weapon.damage;
            }
        } else {
            // Hit level geometry (or max range)
            hitPoint = origin + dir * levelDist;
        }

        // Offset the tracer start to a "gun barrel" position — slightly
        // down and right of the camera so it's not edge-on invisible.
        // The ray itself fires from camera centre for accurate aiming.
        glm::vec3 right = glm::normalize(glm::cross(dir, glm::vec3(0, 1, 0)));
        glm::vec3 tracerStart = origin + right * 0.3f - glm::vec3(0, 0.2f, 0);
        spawnTracer(registry, tracerStart, hitPoint, resources);
    }
}

// ─── Fire projectile ────────────────────────────────────────────

void fireProjectile(entt::registry& registry, entt::entity shooter,
                     const Weapon& weapon,
                     const glm::vec3& origin, const glm::vec3& direction,
                     const CombatResources& resources) {

    auto projectile = registry.create();

    // Spawn slightly in front of the shooter so it doesn't collide immediately
    registry.emplace<Position>(projectile, origin + direction * 0.5f);
    registry.emplace<Velocity>(projectile, direction * weapon.projectileSpeed);
    registry.emplace<AABBCollider>(projectile,
        glm::vec3(0.15f, 0.15f, 0.15f), false);
    registry.emplace<Projectile>(projectile,
        weapon.damage, weapon.splashRadius, weapon.splashDamage, shooter);
    registry.emplace<Lifetime>(projectile, 10.0f);  // Despawn after 10 seconds

    // Visual: a small coloured cube
    registry.emplace<Scale>(projectile, glm::vec3(0.3f));
    registry.emplace<MeshRenderer>(projectile,
        resources.cubeVAO, 0u,
        resources.shaderId, resources.projectileTextureId,
        true, resources.cubeIndexCount);
}

// ─── Main combat system ─────────────────────────────────────────

void combatSystem(entt::registry& registry, const Level& level) {
    const auto& config = registry.ctx().get<PhysicsConfig>();
    const auto& resources = registry.ctx().get<CombatResources>();
    float dt = config.fixedDeltaTime;

    // ─── Weapon cooldowns ────────────────────────────────────────
    auto weaponView = registry.view<WeaponInventory>();
    for (auto [entity, inv] : weaponView.each()) {
        for (auto& weapon : inv.weapons) {
            if (weapon.cooldownRemaining > 0.0f) {
                weapon.cooldownRemaining -= dt;
            }
        }
    }

    // ─── Handle fire input ───────────────────────────────────────
    auto shooterView = registry.view<Position, PlayerInput, WeaponInventory>();

    for (auto [entity, pos, input, inv] : shooterView.each()) {
        if (!input.fire) continue;
        if (inv.weapons.empty()) continue;

        Weapon& weapon = inv.weapons[inv.currentWeapon];
        if (weapon.cooldownRemaining > 0.0f) continue;

        // Get firing direction from camera front vector
        const auto& cameraDir = registry.ctx().get<glm::vec3>();

        // Player position is already at eye height (synced from camera)
        glm::vec3 fireOrigin = pos.value;

        if (weapon.fireMode == FireMode::Hitscan) {
            fireHitscan(registry, level, entity, weapon,
                        fireOrigin, cameraDir, resources);
        } else {
            fireProjectile(registry, entity, weapon,
                           fireOrigin, cameraDir, resources);
        }

        weapon.cooldownRemaining = weapon.fireRate;
    }

    // ─── Projectile collision ────────────────────────────────────
    auto projView = registry.view<Position, Velocity, AABBCollider, Projectile>();
    std::vector<entt::entity> toDestroy;

    for (auto [projEntity, pos, vel, col, proj] : projView.each()) {
        AABB projBox = AABB::fromCentreSize(pos.value, col.halfExtents);

        // Check against ALL colliders (not just entities with Health)
        auto entityView = registry.view<Position, AABBCollider>();
        for (auto [target, tPos, tCol] : entityView.each()) {
            if (target == projEntity) continue;  // Don't collide with self
            if (target == proj.owner) continue;
            if (tCol.isTrigger) continue;

            AABB targetBox = AABB::fromCentreSize(tPos.value, tCol.halfExtents);
            if (projBox.intersects(targetBox)) {
                // Apply damage if the target has Health
                if (registry.all_of<Health>(target)) {
                    registry.get<Health>(target).current -= proj.damage;
                }

                // Splash damage — hurt nearby entities too
                if (proj.splashRadius > 0.0f) {
                    applySplashDamage(registry, pos.value,
                                      proj.splashRadius, proj.splashDamage,
                                      proj.owner);
                }

                toDestroy.push_back(projEntity);
                break;
            }
        }
    }

    for (auto e : toDestroy) {
        if (registry.valid(e)) {
            registry.destroy(e);
        }
    }
}
```

Let's walk through the key decisions:

**Camera direction via context**: The combat system needs to know which way the player is looking, but systems can't access the `Camera` class directly (that would break the ECS boundary). Instead, `main.cpp` writes `camera.getFront()` into the registry context each frame, and the combat system reads it. Clean separation.

**CombatResources via context**: Same pattern. The system needs VAO/shader IDs to spawn visual entities, so we store those IDs in the registry context at startup. No resource manager access from within a system.

**`fireOrigin = pos.value` (no offset)**: The player's `Position` is synced to the camera, which is already at eye height. Adding `+0.7` would put the fire origin above the player's head.

**Tracer gun barrel offset**: The raycast fires from the camera centre for accurate aiming, but the visual tracer starts 0.3 units right and 0.2 units down — simulating a held weapon. Without this offset, the tracer would be directly along the view direction and invisible (you'd be looking straight down a 0.03-unit-wide tube).

**Level geometry raycasting**: Hitscan now checks against level surfaces (floors, walls, ceilings), not just entities. We build a thin AABB for each surface and raycast against it — the same approach as ground detection.

**Projectile collision against all colliders**: The projectile collision check uses `registry.view<Position, AABBCollider>()` — not just entities with `Health`. This means projectiles are destroyed when they hit walls, shelves, or any solid entity. Damage is only applied if the target happens to have `Health`.

**`fromCentreSize` not `fromCenterSize`**: Our AABB class uses British spelling — make sure to match it.

**`rayIntersectionsAABB` not `rayIntersectsAABB`**: Same — match our existing function name in `raycast.h`.

---

## Weapon Switching

It's small enough to be header-only — put it alongside the other system headers:

### src/engine/ecs/systems/weapon_switch_system.h

```cpp
#pragma once

#include <entt/entt.hpp>
#include "engine/ecs/components.h"

inline void weaponSwitchSystem(entt::registry& registry) {
    auto view = registry.view<PlayerInput, WeaponInventory>();

    for (auto [entity, input, inv] : view.each()) {
        if (input.weaponSwitch >= 0 &&
            input.weaponSwitch < static_cast<int>(inv.weapons.size())) {
            inv.currentWeapon = input.weaponSwitch;
        }
    }
}
```

This is a separate system from combat. It runs before combat so that if you switch weapons and fire in the same frame, you fire the weapon you switched to. The `weaponSwitch` field is set to -1 each frame after being read — we handle that in the input polling section.

---

## Lifetime System

Projectiles and debug tracers need to auto-destroy. Without this, fired projectiles would fly forever and tracers would pile up:

### src/engine/ecs/systems/lifetime_system.h

```cpp
#pragma once

#include <entt/entt.hpp>

void lifetimeSystem(entt::registry& registry);
```

### src/engine/ecs/systems/lifetime_system.cpp

```cpp
#include "engine/ecs/systems/lifetime_system.h"
#include "engine/ecs/components.h"
#include "engine/physics/physics_config.h"

void lifetimeSystem(entt::registry& registry) {
    const auto& config = registry.ctx().get<PhysicsConfig>();
    float dt = config.fixedDeltaTime;

    auto view = registry.view<Lifetime>();
    std::vector<entt::entity> expired;

    for (auto [entity, lifetime] : view.each()) {
        lifetime.remaining -= dt;
        if (lifetime.remaining <= 0.0f) {
            expired.push_back(entity);
        }
    }

    for (auto e : expired) {
        registry.destroy(e);
    }
}
```

> **Why collect into a vector first?** You can't destroy entities while iterating a view — that would invalidate the iterator. Collect first, destroy after. This is a common ECS pattern you'll use whenever a system needs to remove entities.

---

## Splash Damage

Rockets and grenades deal area damage. Everything within the splash radius takes damage, scaled by distance:

The `applySplashDamage` function (defined in `combat_system.cpp` above) applies linear falloff — full damage at the centre, zero at the edge. It also applies knockback proportional to damage.

### Rocket Jumping

Notice the knockback code pushes entities away from explosions. If the player shoots a rocket at their own feet, the explosion pushes them upward. This is **rocket jumping** — an emergent mechanic from exactly this code. Quake didn't specifically code rocket jumping; it fell naturally out of the splash damage + knockback system.

Right now `applySplashDamage` skips the shooter with `if (entity == ignore) continue;`, so self-damage doesn't happen. To enable rocket jumping in a future chapter, you'd replace that skip with reduced self-damage while keeping full knockback.

---

## Connecting Input to the ECS

The `PlayerInput` component needs to be populated each frame from GLFW. This happens in `main.cpp`, in the input section — after camera mouse processing and the player position sync, but before the fixed timestep loop. We poll the mouse button directly with `glfwGetMouseButton` since our `InputManager` doesn't have mouse button support yet. Number keys 1 and 2 select weapons.

We also need to write the camera's forward direction into the registry context so the combat system knows which way the player is looking — systems can't access the `Camera` class directly.

Both of these are shown in the full `main.cpp` listing below.

---

## Setting Up the Player's Arsenal

Update `scene_setup.cpp` to give the player weapons and register the combat resources. Include the weapon definitions header at the top of the file:

```cpp
#include "engine/ecs/weapon_definitions.h"
```

In `setupScene()`, update the player entity to include weapon components:

```cpp
// ─── Player entity ────────────────────────────────────────
auto player = registry.create();
registry.emplace<Position>(player, glm::vec3(15.0f, 1.7f, 15.0f));
registry.emplace<AABBCollider>(player, glm::vec3(0.3f, 0.85f, 0.3f), false);
registry.emplace<Health>(player, 100.0f, 100.0f);
registry.emplace<PlayerInput>(player);
registry.emplace<TagPlayer>(player);

// Give the player two weapons: one hitscan, one projectile
WeaponInventory inv;
inv.weapons.push_back(createWeapon(WeaponType::Shotgun));
inv.weapons.push_back(createWeapon(WeaponType::RocketLauncher));
inv.currentWeapon = 0;  // Start with shotgun
registry.emplace<WeaponInventory>(player, std::move(inv));

registry.emplace<Ammo>(player, 25, 0, 5, 0);  // 25 shells, 5 rockets
```

Then at the end of `setupScene()`, register the combat resources in the registry context so the combat system can spawn tracers and projectiles:

```cpp
// ─── Combat resources (stored in registry context) ───────
auto& combatRes = registry.ctx().emplace<CombatResources>();
combatRes.cubeVAO = cubeMesh->getVAO();
combatRes.cubeIndexCount = cubeMesh->getIndexCount();
combatRes.shaderId = litShader->getId();
combatRes.projectileTextureId = gridRed->getId();    // Red cubes for rockets
combatRes.tracerTextureId = gridOrange->getId();      // Orange lines for hitscan
```

No weapon class hierarchy. No `WeaponBase` with virtual `fire()` methods. Just data and a system that reads it.

---

## Updated main.cpp

Add the new system includes at the top of `main.cpp`:

```cpp
#include "engine/ecs/systems/weapon_switch_system.h"
#include "engine/ecs/systems/combat_system.h"
#include "engine/ecs/systems/lifetime_system.h"
```

Here's the updated game loop showing where everything fits. The new lines are marked with comments:

```cpp
while (!window.shouldClose())
{
    fixedTimestep.accumulate((float)glfwGetTime());
    float frameTime = fixedTimestep.getFrameTime();

    input.update();
    window.pollEvents();

    // ─── Input ───────────────────────────────────────────────
    if (input.isKeyPressed(GLFW_KEY_ESCAPE))
        glfwSetWindowShouldClose(window.getHandle(), true);

    if (input.isKeyPressed(GLFW_KEY_W))
        camera.processKeyboard(Camera::FORWARD, frameTime);
    if (input.isKeyPressed(GLFW_KEY_S))
        camera.processKeyboard(Camera::BACKWARD, frameTime);
    if (input.isKeyPressed(GLFW_KEY_A))
        camera.processKeyboard(Camera::LEFT, frameTime);
    if (input.isKeyPressed(GLFW_KEY_D))
        camera.processKeyboard(Camera::RIGHT, frameTime);

    camera.processMouse(input.getMouseXOffset(), input.getMouseYOffset());

    // Sync player entity position to camera
    auto playerView = registry.view<Position, TagPlayer>();
    for (auto [entity, pos] : playerView.each()) {
        pos.value = camera.getPosition();
    }

    // ─── Write camera direction into registry context ────── NEW
    registry.ctx().insert_or_assign<glm::vec3>(camera.getFront());

    // ─── Populate PlayerInput from GLFW ─────────────────── NEW
    auto inputView = registry.view<PlayerInput>();
    for (auto [entity, playerInput] : inputView.each()) {
        playerInput.fire = (glfwGetMouseButton(window.getHandle(),
            GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS);
        playerInput.weaponSwitch = -1;
        if (input.isKeyPressed(GLFW_KEY_1)) playerInput.weaponSwitch = 0;
        if (input.isKeyPressed(GLFW_KEY_2)) playerInput.weaponSwitch = 1;
    }

    // ─── ECS Systems (tick order!) ───────────────────────────
    while (fixedTimestep.step())
    {
        weaponSwitchSystem(registry);              // NEW: switch before firing
        physicsSystem(registry);
        moverSystem(registry);
        collisionSystem(registry, spatialHash, level);
        movementSystem(registry);
        groundDetectionSystem(registry, level);
        combatSystem(registry, level);             // NEW: firing and projectile hits
        lifetimeSystem(registry);                  // NEW: destroy expired entities
        triggerSystem(registry);
        demoResetSystem(registry);
    }

    // Sync player position back to camera (handles teleportation)
    for (auto [entity, pos] : playerView.each()) {
        if (pos.value != camera.getPosition()) {
            camera.setPosition(pos.value);
        }
    }

    // ─── Render ──────────────────────────────────────────────
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    float aspectRatio = (float)window.getWidth() / (float)window.getHeight();
    renderSystem(registry, camera, aspectRatio);

    window.swapBuffers();
}
```

Key points about tick order:
- **weaponSwitchSystem** runs first so weapon switches apply before firing
- **combatSystem** runs after movement/collision so projectiles use final positions
- **lifetimeSystem** runs after combat so freshly-spawned tracers aren't immediately destroyed

### What you should see

- **Press 1**: equip the shotgun (hitscan). Left-click fires 6 pellets — you'll see a fan of green wireframe lines extending from your right side to wherever they hit (walls, floor, entities). The tracers hang in the air for 2 seconds so you can step sideways and inspect the spread pattern
- **Press 2**: equip the rocket launcher (projectile). Left-click spawns a small red cube that flies forward at 20 units/second. It explodes (is destroyed) when it hits any solid entity — the shelf, the hallCube, the door, etc. If nothing is hit, it self-destructs after 10 seconds via `Lifetime`
- Both weapons have cooldowns, so holding the mouse button fires at the weapon's fire rate, not every frame

---

## Updated Tick Order

```
1.  WeaponSwitchSystem       ← NEW
2.  PhysicsSystem
3.  MoverSystem
4.  CollisionSystem
5.  MovementSystem
6.  GroundDetectionSystem
7.  CombatSystem             ← NEW: handles firing and projectile collision
8.  LifetimeSystem           ← NEW: destroys expired entities
9.  TriggerSystem
10. DemoResetSystem
11. RenderSystem
```

---

## C++ Concept: `std::mt19937` — Random Numbers

```cpp
#include <random>

std::mt19937 rng(std::random_device{}());
std::uniform_real_distribution<float> dist(-0.1f, 0.1f);

float randomValue = dist(rng);  // Random float between -0.1 and 0.1
```

`std::mt19937` is the Mersenne Twister random number generator — fast, good quality randomness. `std::random_device{}()` seeds it from the OS's entropy source (hardware noise, etc.).

`std::uniform_real_distribution` produces evenly distributed floats in a range. There are also `std::normal_distribution` (bell curve), `std::uniform_int_distribution`, and others.

Never use `rand()` from C — it's poor quality, not thread-safe, and the modulo approach (`rand() % n`) isn't uniform.

---

## C++ Concept: `registry.ctx()` — Sharing Data Across Systems

We've now used `registry.ctx()` three times: `PhysicsConfig`, `CombatResources`, and the camera's front vector (`glm::vec3`). This is EnTT's **context** — a place to store singleton data that isn't attached to any entity.

```cpp
// Store (once, at setup):
registry.ctx().emplace<PhysicsConfig>();
registry.ctx().emplace<CombatResources>();

// Update (every frame):
registry.ctx().insert_or_assign<glm::vec3>(camera.getFront());

// Read (inside any system):
const auto& config = registry.ctx().get<PhysicsConfig>();
const auto& resources = registry.ctx().get<CombatResources>();
const auto& camDir = registry.ctx().get<glm::vec3>();
```

`emplace` creates the value once and fails if it already exists. `insert_or_assign` creates or overwrites — perfect for values that change every frame like the camera direction.

The context is typed: there can only be one `PhysicsConfig` in the context, one `CombatResources`, etc. For the camera direction we store a raw `glm::vec3`, which works fine as long as we only store one `glm::vec3` in the context. If you needed multiple, you'd wrap them in named structs.

---

## What's Next

In **Chapter 13**, we'll add items and pickups — health packs, ammo boxes, armour, and weapon pickups that respawn on timers. This completes the core item loop of an FPS.
