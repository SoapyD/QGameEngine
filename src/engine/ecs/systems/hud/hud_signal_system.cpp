#include "engine/ecs/systems/hud/hud_signal_system.h"

#include "engine/ecs/components.h"
#include "engine/physics/physics_config.h"

#include <Jolt/Physics/Character/CharacterVirtual.h>

#include <algorithm>
#include <cmath>

namespace
{
    constexpr float kBaseGap    = 2.0f;   // resting crosshair half-gap (px)
    constexpr float kSpreadPx   = 40.0f;  // weapon spread (radians) → px
    constexpr float kMovePx     = 0.9f;   // per unit/s of horizontal speed
    constexpr float kMaxMove    = 7.0f;   // movement term saturates here
    constexpr float kRecoilKick = 8.0f;   // full kick right after a shot
    constexpr int   kLowShots   = 4;      // "low ammo" when fewer than this many shots remain

    int ammoInPool(const Ammo& ammo, WeaponType type)
    {
        switch (type)
        {
            case WeaponType::Shotgun:
            case WeaponType::SuperShotgun:    return ammo.shells;
            case WeaponType::Nailgun:         return ammo.nails;
            case WeaponType::RocketLauncher:
            case WeaponType::GrenadeLauncher: return ammo.rockets;
            case WeaponType::LighteningGun:
            case WeaponType::Railgun:         return ammo.cells;
        }
        return 0;
    }

    float horizontalSpeed(entt::registry& reg, entt::entity player)
    {
        if (auto* jc = reg.try_get<JoltCharacter>(player))
        {
            JPH::Vec3 v = jc->character->GetLinearVelocity();
            return std::sqrt(v.GetX() * v.GetX() + v.GetZ() * v.GetZ());
        }
        return 0.0f;
    }
}

void hudSignalSystem(entt::registry& registry)
{
    HudSignals* hud = registry.ctx().find<HudSignals>();
    if (!hud) return;
    const float dt = registry.ctx().get<PhysicsConfig>().fixedDeltaTime;

    // Decay the transient event markers (set at their event sites).
    hud->hitMarkerTimer  = std::max(0.0f, hud->hitMarkerTimer  - dt);
    hud->killMarkerTimer = std::max(0.0f, hud->killMarkerTimer - dt);
    hud->damageDirTimer  = std::max(0.0f, hud->damageDirTimer  - dt);

    entt::entity player = entt::null;
    for (auto e : registry.view<TagPlayer, WeaponInventory>()) { player = e; break; }
    if (player == entt::null) return;

    const auto& inv = registry.get<WeaponInventory>(player);
    const Weapon& weapon = inv.weapons[inv.currentWeapon];

    // Recoil is derived from how far into its cooldown the weapon is — full kick the
    // instant it fires, decaying to zero as it becomes ready again.
    hud->recoil = (weapon.fireRate > 0.0f)
        ? (weapon.cooldownRemaining / weapon.fireRate) * kRecoilKick : 0.0f;

    float spreadTerm = weapon.spread * kSpreadPx;
    float moveTerm   = std::min(horizontalSpeed(registry, player), kMaxMove) * kMovePx;
    hud->crosshairGap = kBaseGap + spreadTerm + moveTerm + hud->recoil;

    // Low-ammo cue: fewer than kLowShots shots left in this weapon's pool.
    if (const Ammo* ammo = registry.try_get<Ammo>(player))
    {
        int perShot = std::max(1, weapon.ammoPerShot);
        hud->lowAmmo = inv.owned[inv.currentWeapon]
                    && ammoInPool(*ammo, weapon.type) < kLowShots * perShot;
    }
    else hud->lowAmmo = false;
}
