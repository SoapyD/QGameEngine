#pragma once

#include <entt/entt.hpp>
#include "engine/ecs/components/core.h"     // PlayerInput
#include "engine/ecs/components/combat.h"   // WeaponInventory
#include "engine/audio/queue_sound.h"

inline void weaponSwitchSystem(entt::registry& registry)
{
	auto view = registry.view<PlayerInput, WeaponInventory>();

	for (auto [entity, input, inv] : view.each())
	{
		if (input.weaponSwitch >= 0 &&
		input.weaponSwitch < static_cast<int>(inv.weapons.size()) &&
		input.weaponSwitch != inv.currentWeapon)
		{
			inv.currentWeapon = input.weaponSwitch;
			queueSound(registry, "weapon.switch");
		}
	}
}