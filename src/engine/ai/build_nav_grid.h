#pragma once

#include <entt/entt.hpp>

struct Level;
struct NavGrid;

// Build the enemy walkability grid from the level (wall surfaces) and the solid
// prop/mover colliders currently in the registry. Triggers, the player, enemies,
// and dynamic demo props are ignored. Call once the scene is populated.
NavGrid buildNavGrid(entt::registry& registry, const Level& level);
