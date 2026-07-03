#include "engine/level/classname_factory_items.h"

#include "engine/level/factories.h"
#include "engine/ecs/components.h"   // Pickup, PickupType, WeaponType

namespace factories
{
namespace
{
    // A helper builds a Pickup-carrying entity; the item_* / weapon_* factories
    // differ only in PickupType, default amount, marker texture, and (for weapons)
    // the WeaponType granted.
    entt::entity makePickup(entt::registry& reg, const SpawnContext& ctx, const SpawnParams& p,
                            PickupType type, int defaultAmount, const char* defaultTexture,
                            WeaponType weapon = static_cast<WeaponType>(0))
    {
        Pickup pickup;
        pickup.type = type;
        pickup.amount = p.getInt("amount", defaultAmount);
        pickup.weaponType = weapon;
        return spawnPickup(reg, ctx.assets, p.origin, pickup,
            ctx.texture(p.getString("texture", defaultTexture)));
    }

    // Weapon pickups render as the actual gun mesh (spawnWeaponPickup) rather
    // than a textured cube.
    entt::entity makeWeaponPickup(entt::registry& reg, const SpawnContext& ctx,
                                  const SpawnParams& p, WeaponType weapon, int defaultAmount)
    {
        Pickup pickup;
        pickup.type = PickupType::Weapon;
        pickup.amount = p.getInt("amount", defaultAmount);
        pickup.weaponType = weapon;
        return spawnWeaponPickup(reg, ctx.assets, p.origin, pickup, weapon);
    }

    // ─── Ammo/health/armour items ──────────────────────────────────────
    entt::entity make_item_health (entt::registry& r, const SpawnContext& c, const SpawnParams& p)
    { return makePickup(r, c, p, PickupType::Health,  25, "grid_green"); }
    entt::entity make_item_shells (entt::registry& r, const SpawnContext& c, const SpawnParams& p)
    { return makePickup(r, c, p, PickupType::Shells,  10, "grid_orange"); }
    entt::entity make_item_nails  (entt::registry& r, const SpawnContext& c, const SpawnParams& p)
    { return makePickup(r, c, p, PickupType::Nails,   25, "grid_orange"); }
    entt::entity make_item_rockets(entt::registry& r, const SpawnContext& c, const SpawnParams& p)
    { return makePickup(r, c, p, PickupType::Rockets,  5, "grid_red"); }
    entt::entity make_item_cells  (entt::registry& r, const SpawnContext& c, const SpawnParams& p)
    { return makePickup(r, c, p, PickupType::Cells,   25, "grid_blue"); }
    entt::entity make_item_armor  (entt::registry& r, const SpawnContext& c, const SpawnParams& p)
    { return makePickup(r, c, p, PickupType::Armor,   50, "grid_blue"); }

    // ─── Weapon pickups (grant the weapon + top up its ammo pool) ──────
    entt::entity make_weapon_shotgun        (entt::registry& r, const SpawnContext& c, const SpawnParams& p)
    { return makeWeaponPickup(r, c, p, WeaponType::Shotgun, 10); }
    entt::entity make_weapon_supershotgun   (entt::registry& r, const SpawnContext& c, const SpawnParams& p)
    { return makeWeaponPickup(r, c, p, WeaponType::SuperShotgun, 10); }
    entt::entity make_weapon_nailgun        (entt::registry& r, const SpawnContext& c, const SpawnParams& p)
    { return makeWeaponPickup(r, c, p, WeaponType::Nailgun, 25); }
    entt::entity make_weapon_rocketlauncher (entt::registry& r, const SpawnContext& c, const SpawnParams& p)
    { return makeWeaponPickup(r, c, p, WeaponType::RocketLauncher, 5); }
    entt::entity make_weapon_grenadelauncher(entt::registry& r, const SpawnContext& c, const SpawnParams& p)
    { return makeWeaponPickup(r, c, p, WeaponType::GrenadeLauncher, 5); }
    entt::entity make_weapon_lightninggun   (entt::registry& r, const SpawnContext& c, const SpawnParams& p)
    { return makeWeaponPickup(r, c, p, WeaponType::LighteningGun, 50); }
    entt::entity make_weapon_railgun        (entt::registry& r, const SpawnContext& c, const SpawnParams& p)
    { return makeWeaponPickup(r, c, p, WeaponType::Railgun, 10); }
}

void registerItemClassnames(std::unordered_map<std::string, SpawnFn>& t)
{
    t["item_health"]            = &make_item_health;
    t["item_shells"]            = &make_item_shells;
    t["item_nails"]             = &make_item_nails;
    t["item_rockets"]           = &make_item_rockets;
    t["item_cells"]             = &make_item_cells;
    t["item_armor"]             = &make_item_armor;
    t["weapon_shotgun"]         = &make_weapon_shotgun;
    t["weapon_supershotgun"]    = &make_weapon_supershotgun;
    t["weapon_nailgun"]         = &make_weapon_nailgun;
    t["weapon_rocketlauncher"]  = &make_weapon_rocketlauncher;
    t["weapon_grenadelauncher"] = &make_weapon_grenadelauncher;
    t["weapon_lightninggun"]    = &make_weapon_lightninggun;
    t["weapon_railgun"]         = &make_weapon_railgun;
}

} // namespace factories
