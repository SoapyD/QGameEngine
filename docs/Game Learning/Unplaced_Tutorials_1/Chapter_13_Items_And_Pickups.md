# Chapter 13: Items & Pickups

## What You'll Learn
- Pickup components and the item loop
- Health, ammo, armour, and weapon pickups
- Respawning items on timers
- The pickup system — detecting and applying items
- Armour damage reduction
- How Quake's item system worked

---

## Quake's Item Model

Quake's items are simple:
1. An item sits in the world (rotating, bobbing up and down)
2. The player walks over it
3. If the player can pick it up (not full health, doesn't have the weapon, etc.), the item is consumed
4. The item disappears and starts a respawn timer
5. After the timer, the item reappears

No inventory screens, no drag-and-drop, no crafting. Walk over it, get it. Pure FPS.

---

## Pickup Components

Add to `components.h`:

```cpp
enum class PickupType {
    HealthSmall,        // +15 health
    HealthLarge,        // +50 health
    HealthMega,         // +100 health (can overheal)
    AmmoShells,
    AmmoNails,
    AmmoRockets,
    AmmoCells,
    ArmourGreen,        // +100 armour, 50% absorption
    ArmourYellow,       // +150 armour, 60% absorption
    ArmourRed,          // +200 armour, 80% absorption
    WeaponPickup        // Gives a weapon
};

struct Pickup {
    PickupType type;
    float value = 0.0f;          // Amount (health, ammo count, armour value)
    WeaponType weaponType;        // Only used for WeaponPickup
    bool active = true;           // Currently pickupable
    float respawnTime = 30.0f;    // Seconds to respawn (-1 = no respawn)
    float respawnTimer = 0.0f;    // Current countdown
};

struct Armour {
    float current = 0.0f;
    float max = 200.0f;
    float absorptionRate = 0.5f;  // 0.5 = absorbs 50% of damage
};

// Visual: items bob up and down and rotate
struct ItemBob {
    float bobSpeed = 2.0f;
    float bobHeight = 0.15f;
    float rotateSpeed = 90.0f;    // Degrees per second
    float baseY = 0.0f;           // Original Y position
    float time = 0.0f;
};
```

---

## Creating Pickup Entities

A factory function for spawning items. Add this to `src/engine/ecs/scene_setup.cpp` (or a dedicated `src/engine/ecs/item_factory.cpp` if you prefer to keep scene setup lean):

```cpp
entt::entity createPickup(entt::registry& registry, PickupType type,
                           const glm::vec3& position,
                           unsigned int meshVAO, unsigned int indexCount,
                           unsigned int shaderId, unsigned int textureId) {
    auto entity = registry.create();

    registry.emplace<Position>(entity, position);
    registry.emplace<MeshRenderer>(entity, meshVAO, 0u, shaderId,
                                    textureId, true, indexCount);
    registry.emplace<AABBCollider>(entity,
        glm::vec3(0.4f, 0.4f, 0.4f), true);  // Trigger — doesn't block
    registry.emplace<ItemBob>(entity, 2.0f, 0.15f, 90.0f, position.y, 0.0f);

    Pickup pickup{};
    pickup.type = type;

    switch (type) {
        case PickupType::HealthSmall:
            pickup.value = 15.0f;
            pickup.respawnTime = 20.0f;
            break;
        case PickupType::HealthLarge:
            pickup.value = 50.0f;
            pickup.respawnTime = 20.0f;
            break;
        case PickupType::HealthMega:
            pickup.value = 100.0f;
            pickup.respawnTime = 60.0f;
            break;
        case PickupType::AmmoShells:
            pickup.value = 20.0f;
            pickup.respawnTime = 30.0f;
            break;
        case PickupType::AmmoNails:
            pickup.value = 25.0f;
            pickup.respawnTime = 30.0f;
            break;
        case PickupType::AmmoRockets:
            pickup.value = 5.0f;
            pickup.respawnTime = 30.0f;
            break;
        case PickupType::AmmoCells:
            pickup.value = 6.0f;
            pickup.respawnTime = 30.0f;
            break;
        case PickupType::ArmourGreen:
            pickup.value = 100.0f;
            pickup.respawnTime = 30.0f;
            break;
        case PickupType::ArmourYellow:
            pickup.value = 150.0f;
            pickup.respawnTime = 30.0f;
            break;
        case PickupType::ArmourRed:
            pickup.value = 200.0f;
            pickup.respawnTime = 60.0f;
            break;
        case PickupType::WeaponPickup:
            pickup.value = 0.0f;
            pickup.respawnTime = 30.0f;
            break;
    }

    registry.emplace<Pickup>(entity, pickup);

    return entity;
}
```

Place items in the level (call these from `setupScene()`):

```cpp
createPickup(registry, PickupType::HealthLarge,
             glm::vec3(3.0f, 0.5f, -2.0f),
             itemMesh.getVAO(), itemMesh.getIndexCount(),
             litShader.getId(), healthTexture.getId());

createPickup(registry, PickupType::AmmoRockets,
             glm::vec3(-4.0f, 0.5f, 1.0f),
             itemMesh.getVAO(), itemMesh.getIndexCount(),
             litShader.getId(), ammoTexture.getId());
```

---

## The Item Bob System

Items in Quake float and rotate. A small system handles this:

```cpp
void itemBobSystem(entt::registry& registry, float dt) {
    auto view = registry.view<Position, Rotation, ItemBob, Pickup>();

    for (auto [entity, pos, rot, bob, pickup] : view.each()) {
        if (!pickup.active) continue;  // Don't animate inactive items

        bob.time += dt;

        // Sine wave for vertical bobbing
        pos.value.y = bob.baseY + std::sin(bob.time * bob.bobSpeed) * bob.bobHeight;

        // Constant rotation
        rot.euler.y += bob.rotateSpeed * dt;
        if (rot.euler.y > 360.0f) rot.euler.y -= 360.0f;
    }
}
```

---

## The Pickup System

### src/engine/ecs/systems/pickup_system.h

```cpp
#pragma once

#include <entt/entt.hpp>

void pickupSystem(entt::registry& registry, float dt);
```

### src/engine/ecs/systems/pickup_system.cpp

```cpp
#include "engine/ecs/systems/pickup_system.h"
#include "engine/ecs/components.h"
#include "engine/physics/aabb.h"
#include <algorithm>

// Check if the player can pick up this item (not full, etc.)
bool canPickup(entt::registry& registry, entt::entity player, const Pickup& pickup) {
    switch (pickup.type) {
        case PickupType::HealthSmall:
        case PickupType::HealthLarge: {
            if (!registry.all_of<Health>(player)) return false;
            auto& health = registry.get<Health>(player);
            return health.current < health.max;
        }

        case PickupType::HealthMega: {
            // Mega health can overheal (past max)
            return true;
        }

        case PickupType::AmmoShells:
        case PickupType::AmmoNails:
        case PickupType::AmmoRockets:
        case PickupType::AmmoCells: {
            // Could check against max ammo — for now always allow
            return registry.all_of<Ammo>(player);
        }

        case PickupType::ArmourGreen:
        case PickupType::ArmourYellow:
        case PickupType::ArmourRed: {
            // Only pick up if it would be an upgrade
            if (!registry.all_of<Armour>(player)) return true;
            auto& armour = registry.get<Armour>(player);
            float newValue = pickup.value;
            return newValue > armour.current;
        }

        case PickupType::WeaponPickup: {
            // Check if player already has this weapon
            if (!registry.all_of<WeaponInventory>(player)) return true;
            auto& inv = registry.get<WeaponInventory>(player);
            for (const auto& w : inv.weapons) {
                if (w.type == pickup.weaponType) return false;
            }
            return true;
        }
    }

    return false;
}

// Apply the pickup effect to the player
void applyPickup(entt::registry& registry, entt::entity player,
                  const Pickup& pickup) {
    switch (pickup.type) {
        case PickupType::HealthSmall:
        case PickupType::HealthLarge: {
            auto& health = registry.get<Health>(player);
            health.current = std::min(health.current + pickup.value, health.max);
            break;
        }

        case PickupType::HealthMega: {
            auto& health = registry.get<Health>(player);
            health.current += pickup.value;
            // Mega health decays back to max over time (handled elsewhere)
            break;
        }

        case PickupType::AmmoShells: {
            registry.get<Ammo>(player).shells += static_cast<int>(pickup.value);
            break;
        }
        case PickupType::AmmoNails: {
            registry.get<Ammo>(player).nails += static_cast<int>(pickup.value);
            break;
        }
        case PickupType::AmmoRockets: {
            registry.get<Ammo>(player).rockets += static_cast<int>(pickup.value);
            break;
        }
        case PickupType::AmmoCells: {
            registry.get<Ammo>(player).cells += static_cast<int>(pickup.value);
            break;
        }

        case PickupType::ArmourGreen: {
            if (!registry.all_of<Armour>(player)) {
                registry.emplace<Armour>(player, pickup.value, 200.0f, 0.5f);
            } else {
                auto& armour = registry.get<Armour>(player);
                armour.current = pickup.value;
                armour.absorptionRate = 0.5f;
            }
            break;
        }
        case PickupType::ArmourYellow: {
            if (!registry.all_of<Armour>(player)) {
                registry.emplace<Armour>(player, pickup.value, 200.0f, 0.6f);
            } else {
                auto& armour = registry.get<Armour>(player);
                armour.current = pickup.value;
                armour.absorptionRate = 0.6f;
            }
            break;
        }
        case PickupType::ArmourRed: {
            if (!registry.all_of<Armour>(player)) {
                registry.emplace<Armour>(player, pickup.value, 200.0f, 0.8f);
            } else {
                auto& armour = registry.get<Armour>(player);
                armour.current = pickup.value;
                armour.absorptionRate = 0.8f;
            }
            break;
        }

        case PickupType::WeaponPickup: {
            auto& inv = registry.get<WeaponInventory>(player);
            inv.weapons.push_back(createWeapon(pickup.weaponType));
            // Auto-switch to new weapon
            inv.currentWeapon = static_cast<int>(inv.weapons.size()) - 1;
            break;
        }
    }
}

void pickupSystem(entt::registry& registry, float dt) {
    // ─── Handle respawn timers ───────────────────────────────────
    auto pickupView = registry.view<Pickup, MeshRenderer>();

    for (auto [entity, pickup, mesh] : pickupView.each()) {
        if (pickup.active) continue;

        pickup.respawnTimer -= dt;
        if (pickup.respawnTimer <= 0.0f) {
            pickup.active = true;
            // Make visible again (restore shader/texture)
            // A cleaner approach: use a Visible component
        }
    }

    // ─── Check for player overlap with active pickups ────────────
    auto playerView = registry.view<Position, AABBCollider, TagPlayer>();

    for (auto [playerEntity, playerPos, playerCol, tag] : playerView.each()) {
        AABB playerBox = AABB::fromCenterSize(playerPos.value, playerCol.halfExtents);

        auto itemView = registry.view<Position, AABBCollider, Pickup>();

        for (auto [itemEntity, itemPos, itemCol, pickup] : itemView.each()) {
            if (!pickup.active) continue;

            AABB itemBox = AABB::fromCenterSize(itemPos.value, itemCol.halfExtents);

            if (!playerBox.intersects(itemBox)) continue;
            if (!canPickup(registry, playerEntity, pickup)) continue;

            // Pick it up!
            applyPickup(registry, playerEntity, pickup);

            // TODO: play pickup sound (Chapter 16)

            // Deactivate and start respawn timer
            if (pickup.respawnTime > 0.0f) {
                pickup.active = false;
                pickup.respawnTimer = pickup.respawnTime;
                // Hide the item visually
                // (could remove MeshRenderer, or set a Visible flag)
            } else {
                // No respawn — destroy the entity
                registry.destroy(itemEntity);
            }
        }
    }
}
```

---

## Armour and Damage Absorption

When the player takes damage, armour absorbs a percentage:

Update the damage-dealing code (in `combatSystem` or wherever damage is applied):

```cpp
void applyDamage(entt::registry& registry, entt::entity target, float damage) {
    if (!registry.all_of<Health>(target)) return;

    float actualDamage = damage;

    // Armour absorbs some damage
    if (registry.all_of<Armour>(target)) {
        auto& armour = registry.get<Armour>(target);
        if (armour.current > 0.0f) {
            float absorbed = damage * armour.absorptionRate;
            float remaining = damage - absorbed;

            // Drain armour
            armour.current -= absorbed;
            if (armour.current < 0.0f) {
                // Armour depleted — overflow goes to health
                remaining += std::abs(armour.current);
                armour.current = 0.0f;
            }

            actualDamage = remaining;
        }
    }

    registry.get<Health>(target).current -= actualDamage;
}
```

Example: Player has 100 health, 100 green armour (50% absorption).
- Takes 40 damage
- Armour absorbs 50%: 20 points from armour, 20 from health
- Result: 80 health, 80 armour

This is exactly Quake's armour system.

---

## Visibility Toggle

Rather than destroying and recreating entities for respawning items, a `Visible` component is cleaner:

```cpp
struct Visible {
    bool value = true;
};
```

The render system checks this before drawing:

```cpp
// In renderSystem, add this check:
if (registry.all_of<Visible>(entity) &&
    !registry.get<Visible>(entity).value) {
    continue;  // Skip invisible entities
}
```

When an item is picked up, set `visible = false`. When it respawns, set `visible = true`. The entity stays in the registry the whole time — only its visibility changes.

---

## The Complete Item Loop

```
Player walks over item
        │
        ▼
Can they pick it up? ──── No → Ignore
        │
       Yes
        │
        ▼
Apply effect (heal, give ammo, give weapon)
        │
        ▼
Play pickup sound
        │
        ▼
Hide item, start respawn timer
        │
        ▼
Timer expires → show item again
```

This loop is the heart of arena FPS item control. In competitive Quake, knowing item spawn timers and controlling the map's health/armour/weapon locations is half the game.

---

## What's Next

In **Chapter 14**, we'll add enemy AI — state machines for enemy behaviour, line-of-sight checks, pathfinding basics, and making enemies that chase, attack, and react to the player.
