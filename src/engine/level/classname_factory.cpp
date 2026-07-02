#include "engine/level/classname_factory.h"

#include "engine/level/factories.h"
#include "engine/ecs/components.h"   // Position (info_teleport_destination marker), Pickup

#include <iostream>
#include <string>
#include <unordered_map>

namespace factories
{
namespace
{
    // Box entities carry a full-extent `size`; the collider wants the half-extent.
    glm::vec3 halfOf(const SpawnParams& p) { return p.size * 0.5f; }

    // ─── One thin factory per classname — each just translates params and
    //     delegates to a typed factories:: builder. ────────────────────────

    entt::entity make_info_player_start(entt::registry& reg, const SpawnContext&, const SpawnParams& p)
    {
        return spawnPlayer(reg, p.origin);
    }

    entt::entity make_light(entt::registry& reg, const SpawnContext& ctx, const SpawnParams& p)
    {
        return spawnPointLight(reg, ctx.assets, p.origin,
            p.getVec3("_color", glm::vec3(1.0f)),
            p.getFloat("ambient", 0.05f),
            p.getFloat("linear", 0.09f),
            p.getFloat("quadratic", 0.032f),
            ctx.texture(p.getString("marker", "grid_grey")));
    }

    entt::entity make_light_environment(entt::registry& reg, const SpawnContext&, const SpawnParams& p)
    {
        return spawnDirectionalLight(reg,
            p.getVec3("direction", glm::vec3(-0.2f, -1.0f, -0.3f)),
            p.getVec3("_color", glm::vec3(1.0f)),
            p.getFloat("ambient", 0.08f));
    }

    entt::entity make_func_static(entt::registry& reg, const SpawnContext& ctx, const SpawnParams& p)
    {
        return spawnStaticBox(reg, ctx.assets, p.origin, p.size, halfOf(p),
            ctx.texture(p.getString("texture", "grid_grey")));
    }

    entt::entity make_prop_dynamic(entt::registry& reg, const SpawnContext& ctx, const SpawnParams& p)
    {
        return spawnDemoCube(reg, ctx.assets, p.origin,
            p.getVec3("velocity", glm::vec3(0.0f)),
            p.getFloat("interval", 5.0f),
            ctx.texture(p.getString("texture", "grid_orange")));
    }

    // func_door and func_plat share the Mover archetype; the classname is only a
    // semantic hint (horizontal door vs vertical lift). Travel is carried as an
    // explicit endpos until the .map loader derives it from angle/lip/height.
    entt::entity make_func_mover(entt::registry& reg, const SpawnContext& ctx, const SpawnParams& p)
    {
        return spawnMover(reg, ctx.assets, p.origin,
            p.getVec3("endpos", p.origin), p.size, halfOf(p),
            p.getFloat("speed", 3.0f),
            p.getFloat("wait", 3.0f),
            p.getFloat("startdelay", 0.0f),
            ctx.texture(p.getString("texture", "grid_orange")));
    }

    entt::entity make_trigger_multiple(entt::registry& reg, const SpawnContext&, const SpawnParams& p)
    {
        // target (→ mover) resolved in spawnScene's second pass.
        return spawnTrigger(reg, p.origin, halfOf(p), TriggerAction::ActivateMover,
            entt::null, glm::vec3(0.0f), 0.0f, p.getFloat("cooldown", 1.0f));
    }

    entt::entity make_trigger_teleport(entt::registry& reg, const SpawnContext&, const SpawnParams& p)
    {
        // destination overridden from the linked info_teleport_destination in pass 2;
        // a literal "destination" prop is a fallback when no target is given.
        return spawnTrigger(reg, p.origin, halfOf(p), TriggerAction::Teleport,
            entt::null, p.getVec3("destination", p.origin), 0.0f,
            p.getFloat("cooldown", 1.0f));
    }

    entt::entity make_trigger_hurt(entt::registry& reg, const SpawnContext&, const SpawnParams& p)
    {
        return spawnTrigger(reg, p.origin, halfOf(p), TriggerAction::Damage,
            entt::null, glm::vec3(0.0f), p.getFloat("dmg", 25.0f),
            p.getFloat("cooldown", 0.0f));
    }

    entt::entity make_info_teleport_destination(entt::registry& reg, const SpawnContext&, const SpawnParams& p)
    {
        // Marker: carries only an origin so a trigger_teleport can resolve it by name.
        auto e = reg.create();
        reg.emplace<Position>(e, p.origin);
        return e;
    }

    entt::entity make_func_decor(entt::registry& reg, const SpawnContext& ctx, const SpawnParams& p)
    {
        return spawnDecorBox(reg, ctx.assets, p.origin, p.size,
            ctx.texture(p.getString("texture", "grid_grey")));
    }

    entt::entity make_wireframe(entt::registry& reg, const SpawnContext& ctx, const SpawnParams& p)
    {
        // Engine-internal (leading underscore): editor debug volume, not a map entity.
        return spawnDebugWireframe(reg, ctx.assets, p.origin, p.size,
            ctx.texture(p.getString("texture", "grid_green")));
    }

    // ─── Item pickups ──────────────────────────────────────────────────
    // A helper builds a Pickup-carrying entity; the item_* / weapon_* factories
    // differ only in PickupType, default amount, and marker texture.
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

    entt::entity make_weapon_shotgun (entt::registry& r, const SpawnContext& c, const SpawnParams& p)
    { return makePickup(r, c, p, PickupType::Weapon, 10, "grid_grey", WeaponType::Shotgun); }
    entt::entity make_weapon_nailgun (entt::registry& r, const SpawnContext& c, const SpawnParams& p)
    { return makePickup(r, c, p, PickupType::Weapon, 25, "grid_grey", WeaponType::Nailgun); }
    entt::entity make_weapon_rocket  (entt::registry& r, const SpawnContext& c, const SpawnParams& p)
    { return makePickup(r, c, p, PickupType::Weapon,  5, "grid_grey", WeaponType::RocketLauncher); }
    entt::entity make_weapon_railgun (entt::registry& r, const SpawnContext& c, const SpawnParams& p)
    { return makePickup(r, c, p, PickupType::Weapon, 10, "grid_grey", WeaponType::Railgun); }

    using SpawnFn = entt::entity(*)(entt::registry&, const SpawnContext&, const SpawnParams&);

    const std::unordered_map<std::string, SpawnFn>& table()
    {
        static const std::unordered_map<std::string, SpawnFn> t = {
            { "info_player_start",         &make_info_player_start },
            { "light",                     &make_light },
            { "light_environment",         &make_light_environment },
            { "func_static",               &make_func_static },
            { "prop_dynamic",              &make_prop_dynamic },
            { "func_door",                 &make_func_mover },
            { "func_plat",                 &make_func_mover },
            { "trigger_multiple",          &make_trigger_multiple },
            { "trigger_teleport",          &make_trigger_teleport },
            { "trigger_hurt",              &make_trigger_hurt },
            { "info_teleport_destination", &make_info_teleport_destination },
            { "func_decor",                &make_func_decor },
            { "_wireframe",                &make_wireframe },
            { "item_health",               &make_item_health },
            { "item_shells",               &make_item_shells },
            { "item_nails",                &make_item_nails },
            { "item_rockets",              &make_item_rockets },
            { "item_cells",                &make_item_cells },
            { "item_armor",                &make_item_armor },
            { "weapon_shotgun",            &make_weapon_shotgun },
            { "weapon_nailgun",            &make_weapon_nailgun },
            { "weapon_rocketlauncher",     &make_weapon_rocket },
            { "weapon_railgun",            &make_weapon_railgun },
        };
        return t;
    }
} // namespace

entt::entity spawnByClassname(entt::registry& reg, const SpawnContext& ctx, const SpawnParams& p)
{
    auto it = table().find(p.classname);
    if (it == table().end())
    {
        std::cerr << "[factories] unknown classname '" << p.classname << "' — skipped\n";
        return entt::null;
    }
    return it->second(reg, ctx, p);
}

} // namespace factories
