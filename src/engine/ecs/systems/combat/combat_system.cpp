#include "engine/ecs/systems/combat/combat_system.h"
#include "engine/ecs/systems/combat/combat_internal.h"

#include "engine/ecs/components.h"
#include "engine/physics/physics_config.h"

namespace
{
	// The ammo pool a weapon draws from.
	int& ammoPool(Ammo& ammo, WeaponType type)
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
		return ammo.shells;
	}
}

// ─── Main combat system ─────────────────────────────────────────
// Ticks weapon cooldowns, dispatches fire input to hitscan/projectile, then
// advances live projectiles. The heavy lifting lives in the sibling files
// (fire_hitscan, fire_projectile, update_projectiles, ...).
void combatSystem
(
	entt::registry& registry,
	const Level& level
)
{
	const auto& config = registry.ctx().get<PhysicsConfig>();
	const auto& resources = registry.ctx().get<CombatResources>();
	float dt = config.fixedDeltaTime;

	 // ─── Weapon cooldowns ────────────────────────────────────────
	 auto weaponView = registry.view<WeaponInventory>();
	 for (auto [entity, inv] : weaponView.each())
	 {
		for (auto& weapon : inv.weapons)
		{
			if (weapon.cooldownRemaining > 0.0f)
			{
				weapon.cooldownRemaining -= dt;
			}
		}
	 }

	 // ─── Handle fire input ───────────────────────────────────────

	 auto shooterView = registry.view<Position, PlayerInput, WeaponInventory>();

	 for (auto [entity, pos, input, inv] : shooterView.each())
	 {
		if (!input.fire) continue;
		if (inv.weapons.empty()) continue;

		Weapon& weapon = inv.weapons[inv.currentWeapon];
		if (weapon.cooldownRemaining > 0.0f) continue;

		// Ammo gate: a shooter that tracks Ammo must have enough, and firing
		// consumes it. Shooters with no Ammo component (e.g. enemies) fire freely.
		if (Ammo* ammo = registry.try_get<Ammo>(entity))
		{
			int& pool = ammoPool(*ammo, weapon.type);
			if (pool < weapon.ammoPerShot) continue;   // out of ammo — no shot
			pool -= weapon.ammoPerShot;
		}

        // Get firing direction from camera front vector
        // The camera direction is written into the registry context each frame
		const auto& cameraDir = registry.ctx().get<CameraDirection>().value;

		// Fire from eye height — position is at body centre, offset upward
		float eyeOffset = 0.0f;
		if (registry.all_of<AABBCollider>(entity))
		{
			eyeOffset = registry.get<AABBCollider>(entity).halfExtents.y * kEyeHeightFraction;
		}
		glm::vec3 fireOrigin = pos.value + glm::vec3(0.0f, eyeOffset, 0.0f);

		if (weapon.fireMode == FireMode::Hitscan)
		{
			fireHitscan(registry, level, entity, weapon, fireOrigin, cameraDir, resources);
		}
		else
		{
			fireProjectile(registry, entity, weapon, fireOrigin, cameraDir, resources);
		}

		weapon.cooldownRemaining = weapon.fireRate;
	 }

	// ─── Projectile movement & collision ─────────────────────────
	updateProjectiles(registry, level, dt);
}
