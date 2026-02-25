#pragma once

#include <entt/entt.hpp>
#include "engine/ecs/components.h"

inline void weaponSwitchSystem(entt::registry& registry)
{
	auto view = registry.view<PlayerInput, WeaponInventory>();

	for (auto [entity, input, inv] : view.each())
	{
		if (input.weaponSwitch >= 0 &&
		input.weaponSwitch < static_cast<int>(inv.weapons.size()))
		{
			inv.currentWeapon = input.weaponSwitch;
		}
	}
}