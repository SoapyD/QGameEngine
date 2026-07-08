#pragma once

#include <entt/entt.hpp>

// One-time setup: build a CharacterVirtual for each enemy (Position + AABBCollider
// + AIState) so aiSystem drives them with COLLIDED locomotion (no wall-clipping on
// corner-cuts), mirroring the player. Each character carries a kinematic INNER BODY
// on Layers::MOVING so it still blocks the player and separates from other enemies;
// the character ignores its own inner body, and the inner body is destroyed with
// the character. Call once after level bodies exist, before the first aiSystem tick.
void initEnemyCharacters(entt::registry& registry);
