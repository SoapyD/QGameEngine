#include "engine/ecs/systems/debug_hud/debug_hud_internal.h"

#include "engine/ecs/components.h"

#include <cstdio>

// Draw the player's current weapon name + ammo count at (x, y).
void drawAmmo
(
	entt::registry& registry,
	float x, float y,
	unsigned int shaderId, const glm::mat4& projection, float scale
)
{
	auto ammoView = registry.view<Ammo, WeaponInventory, TagPlayer>();
	for (auto [ entity, ammo, inv] : ammoView.each())
	{
		if (!inv.owned[inv.currentWeapon]) continue;
		const Weapon& currentWeapon = inv.weapons[inv.currentWeapon];

		const char* weaponName = "Uknown";
		int ammoCount = 0;
		switch (currentWeapon.type)
		{
			case WeaponType::Shotgun:
			case WeaponType::SuperShotgun:
				weaponName = "Shotgun";
				ammoCount = ammo.shells;
			break;
			case WeaponType::Nailgun:
				weaponName = "Nailgun";
				ammoCount = ammo.nails;
			break;
			case WeaponType::RocketLauncher:
			case WeaponType::GrenadeLauncher:
				weaponName = "Rockets";
				ammoCount = ammo.rockets;
			break;
			case WeaponType::LighteningGun:
			case WeaponType::Railgun:
				weaponName = "Cells";
				ammoCount = ammo.cells;
			break;
		}

		char ammoText[64];
		snprintf
		(
			ammoText, sizeof(ammoText),
			"%s /%d", weaponName, ammoCount
		);
		// Red when the current pool is low (flag computed by hudSignalSystem).
		const HudSignals* hud = registry.ctx().find<HudSignals>();
		glm::vec3 col = (hud && hud->lowAmmo) ? glm::vec3(0.9f, 0.1f, 0.1f) : glm::vec3(0.0f);
		drawText(x, y, ammoText, shaderId, projection, scale, col);
	}
}
