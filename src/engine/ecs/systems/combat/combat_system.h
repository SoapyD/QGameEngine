#pragma once

#include <entt/entt.hpp>
#include "engine/level/level.h"

// Public entry point for the combat system. Implementation is split across
// systems/combat/*.cpp (see combat_internal.h for the shared helpers).
void combatSystem(entt::registry& registry, const Level& level);
