#pragma once

#include "engine/ecs/components.h"

inline Weapon createWeapon(WeaponType type)
{
	Weapon w{};
	w.type = type;

	switch(type)
	{
		case WeaponType::Shotgun:
			w.fireMode = FireMode::Hitscan;
			w.damage = 4.0f;
			w.fireRate = 0.5f;
			w.range = 500.0f;
			w.spread = 0.05f;
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
			w.fireMode = FireMode::Hitscan;
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

		case WeaponType::LighteningGun:
			w.fireMode = FireMode::Hitscan;
			w.damage = 30.0f;
			w.fireRate = 0.05f;
			w.range = 15.0f;
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