#pragma once

#include <entt/entt.hpp>
#include "engine/physics/spatial_hash.h"
#include "engine/level/level.h"

void collisionSystem
(
	entt::registry& registry,
	SpatialHash& spatialHash,
	const Level& level
);