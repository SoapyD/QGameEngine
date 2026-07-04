#pragma once

#include <entt/entt.hpp>

struct Level;

// Enemy behaviour: sense the player (detect range + line-of-sight), chase by
// steering the kinematic body toward them, and melee-attack in range. Runs
// BEFORE the physics step so the grunt's MoveKinematic target is swept this
// tick. Death and hit/death feedback are handled by enemyDeathSystem.
void aiSystem(entt::registry& registry, const Level& level);
