#pragma once

#include <entt/entt.hpp>
#include <vector>

// Weapon, ammo, projectile, and combat-resource components.

enum class WeaponType
{
	Shotgun,
	SuperShotgun,
	Nailgun,
	RocketLauncher,
	GrenadeLauncher,
	LighteningGun,
	Railgun
};

enum class FireMode
{
	Hitscan,
	Projectile
};

struct Weapon
{
	WeaponType type;
	FireMode fireMode;
	float damage = 10.0f;
    float fireRate = 0.5f;          // Seconds between shots
    float cooldownRemaining = 0.0f;
    float range = 1000.0f;          // Hitscan max range
    float spread = 0.0f;            // Cone of inaccuracy (radians)
    int pelletCount = 1;            // Shotguns fire multiple pellets
    float projectileSpeed = 0.0f;   // For projectile weapons
    float splashRadius = 0.0f;      // Area of effect damage radius
    float splashDamage = 0.0f;      // Damage at center of splash
    int ammoPerShot = 1;
};

struct WeaponInventory
{
	std::vector<Weapon> weapons;
	int currentWeapon = 0;
};

struct Ammo
{
	int shells = 0;
	int nails = 0;
	int rockets = 0;
	int cells = 0;
};

// attacked to projectile entities
struct Projectile
{
	float damage;
	float splashRadius;
	float splashDamage;
	entt::entity owner = entt::null; // Who fired it (for kill credit)
};

// Resources the combat system needs to spawn projectiles and tracers
struct CombatResources
{
	unsigned int cubeVAO = 0;
	unsigned int cubeIndexCount = 0;
	unsigned int shaderId = 0;
	unsigned int projectileTextureId = 0; // Colour for projectile cubes
	unsigned int tracerTextureId = 0;	 // Colour for hitscan tracers
};
