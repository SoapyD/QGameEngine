#pragma once

#include <entt/entt.hpp>
#include "engine/core/resource_manager.h"
#include "engine/level/level.h"

#include <memory>

// set up the initial scene entities
// this replaces the inline entity creation that was in main.
// `headless` skips building GL render meshes for the level (no GL context).
Level setupScene
(
	entt::registry& registry,
	const ResourceManager& resources,
	bool headless = false
);