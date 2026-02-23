#pragma once

#include <cstdint>

namespace CollisionLayers
{
	// Each layer is a single bit. Use bitwise OR to combine.
	// An entity collides with another if (a.layer & b.mask) != 0.

	constexpr uint32_t None = 0x00;	
	constexpr uint32_t Player = 0x01;
	constexpr uint32_t World = 0x02;
	constexpr uint32_t Enemy = 0x04;
	constexpr uint32_t Projectile = 0x08;
	constexpr uint32_t Trigger = 0x10;

	// common combined masks for convenience
	constexpr uint32_t All = 0xFFFFFFFF;
	constexpr uint32_t Solid = Player | World | Enemy;	
	constexpr uint32_t Shootable = Enemy | World;	
}