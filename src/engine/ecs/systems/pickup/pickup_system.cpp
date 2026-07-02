#include "engine/ecs/systems/pickup/pickup_system.h"
#include "engine/ecs/components.h"
#include "engine/ecs/weapon_definitions.h"   // createWeapon
#include "engine/physics/types/aabb.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
    // Add ammo of the given kind to an Ammo component.
    void addAmmo(Ammo& ammo, PickupType type, int amount)
    {
        switch (type)
        {
            case PickupType::Shells:  ammo.shells  += amount; break;
            case PickupType::Nails:   ammo.nails   += amount; break;
            case PickupType::Rockets: ammo.rockets += amount; break;
            case PickupType::Cells:   ammo.cells   += amount; break;
            default: break;
        }
    }

    // The ammo kind a weapon draws from — a weapon pickup tops this up.
    PickupType ammoKindFor(WeaponType weapon)
    {
        switch (weapon)
        {
            case WeaponType::Shotgun:
            case WeaponType::SuperShotgun:   return PickupType::Shells;
            case WeaponType::Nailgun:        return PickupType::Nails;
            case WeaponType::RocketLauncher:
            case WeaponType::GrenadeLauncher:return PickupType::Rockets;
            case WeaponType::LighteningGun:
            case WeaponType::Railgun:        return PickupType::Cells;
        }
        return PickupType::Shells;
    }

    // Grant a weapon: add it to the inventory if not already held, then top up
    // the ammo pool it uses (so a picked-up weapon is immediately usable).
    void grantWeapon(entt::registry& reg, entt::entity receiver, const Pickup& pickup)
    {
        if (reg.all_of<WeaponInventory>(receiver))
        {
            auto& inv = reg.get<WeaponInventory>(receiver);
            bool held = std::any_of(inv.weapons.begin(), inv.weapons.end(),
                [&](const Weapon& w) { return w.type == pickup.weaponType; });
            if (!held) inv.weapons.push_back(createWeapon(pickup.weaponType));
        }
        if (reg.all_of<Ammo>(receiver))
            addAmmo(reg.get<Ammo>(receiver), ammoKindFor(pickup.weaponType), pickup.amount);
    }

    const char* weaponName(WeaponType w)
    {
        switch (w)
        {
            case WeaponType::Shotgun:         return "Shotgun";
            case WeaponType::SuperShotgun:    return "Super Shotgun";
            case WeaponType::Nailgun:         return "Nailgun";
            case WeaponType::RocketLauncher:  return "Rocket Launcher";
            case WeaponType::GrenadeLauncher: return "Grenade Launcher";
            case WeaponType::LighteningGun:   return "Lightning Gun";
            case WeaponType::Railgun:         return "Railgun";
        }
        return "Weapon";
    }

    // The toast text shown when this pickup is collected.
    std::string pickupMessage(const Pickup& p)
    {
        const int n = p.amount;
        switch (p.type)
        {
            case PickupType::Health:  return "Picked up Health +" + std::to_string(n);
            case PickupType::Shells:  return "Picked up " + std::to_string(n) + " Shells";
            case PickupType::Nails:   return "Picked up " + std::to_string(n) + " Nails";
            case PickupType::Rockets: return "Picked up " + std::to_string(n) + " Rockets";
            case PickupType::Cells:   return "Picked up " + std::to_string(n) + " Cells";
            case PickupType::Armor:   return "Picked up Armor +" + std::to_string(n);
            case PickupType::Weapon:  return std::string("Got the ") + weaponName(p.weaponType);
        }
        return "Picked up item";
    }

    // Apply a pickup's effect to the receiving entity.
    void applyPickup(entt::registry& reg, entt::entity receiver, const Pickup& pickup)
    {
        switch (pickup.type)
        {
            case PickupType::Health:
                if (reg.all_of<Health>(receiver))
                {
                    auto& h = reg.get<Health>(receiver);
                    h.current = std::min(h.current + (float)pickup.amount, h.max);
                }
                break;

            case PickupType::Armor:
                if (reg.all_of<Armor>(receiver))
                {
                    auto& a = reg.get<Armor>(receiver);
                    a.current = std::min(a.current + (float)pickup.amount, a.max);
                }
                break;

            case PickupType::Weapon:
                grantWeapon(reg, receiver, pickup);
                break;

            default: // ammo kinds
                if (reg.all_of<Ammo>(receiver))
                    addAmmo(reg.get<Ammo>(receiver), pickup.type, pickup.amount);
                break;
        }
    }
}

void pickupSystem(entt::registry& registry)
{
    auto pickupView   = registry.view<Position, AABBCollider, Pickup>();
    auto receiverView = registry.view<Position, AABBCollider, TagTriggerable>();

    // Collect consumed pickups and destroy after iterating (don't invalidate
    // the view mid-loop).
    std::vector<entt::entity> consumed;

    for (auto [pickupEntity, pickupPos, pickupCol, pickup] : pickupView.each())
    {
        AABB pickupBox = AABB::fromCentreSize(pickupPos.value, pickupCol.halfExtents);

        for (auto [receiver, recvPos, recvCol] : receiverView.each())
        {
            AABB recvBox = AABB::fromCentreSize(recvPos.value, recvCol.halfExtents);
            if (!pickupBox.intersects(recvBox)) continue;

            applyPickup(registry, receiver, pickup);

            // Show a HUD toast on the receiver, if it displays one.
            if (auto* msg = registry.try_get<PickupMessage>(receiver))
            {
                msg->text = pickupMessage(pickup);
                msg->timer = msg->duration;
            }

            consumed.push_back(pickupEntity);
            break; // one toucher consumes it
        }
    }

    for (entt::entity e : consumed)
        registry.destroy(e);
}
