#pragma once

#include <entt/entt.hpp>
#include "engine/core/resource_manager.h"

#include <memory>

// set up the initial scene entities
// this replaces the inline entity creation that was in main
void setupScene
(
	entt::registry& registry,
	const ResourceManager& resources
);
