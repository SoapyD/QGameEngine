#pragma once

#include <entt/entt.hpp>

struct Level;

// Enemy behaviour: sense the player (detect range + line-of-sight), chase by
// driving a CharacterVirtual along the A* path (collided locomotion — no wall
// clipping), and melee-attack in range. Runs BEFORE the physics step and owns
// the enemy's Position (joltSyncSystem skips them — they have no JoltBody).
// Death and hit/death feedback are handled by enemyDeathSystem.
void aiSystem(entt::registry& registry, const Level& level);
