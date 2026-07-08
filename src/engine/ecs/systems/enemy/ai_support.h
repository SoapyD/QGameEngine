#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>

// Support helpers for aiSystem, split out to keep ai_system.cpp focused on the
// state machine (CODING_STANDARD §4 size cap). Physics/geometry/combat glue only.

namespace JPH { class CharacterVirtual; }
struct JoltWorld;
struct Level;
struct RangedAttack;
struct CombatResources;

// Step an enemy CharacterVirtual one tick with a desired horizontal velocity,
// letting gravity + stick-to-floor keep it grounded (collided locomotion).
void aiStepCharacter(JPH::CharacterVirtual* ch, glm::vec3 horiz, float dt, JoltWorld& jolt);

// True if `self` has an unobstructed line from `from` to `to` (level surfaces +
// solid entities, ignoring the player itself and in-flight projectiles).
bool aiClearLineOfSight(entt::registry& reg, const Level& level, entt::entity self,
                        entt::entity player, glm::vec3 from, glm::vec3 to);

// Fire one dodgeable Enemy-faction bolt from `eye` toward `dir` using the combat
// projectile path. No splash — it can never friendly-fire other enemies.
void aiFireEnemyBolt(entt::registry& reg, entt::entity self, glm::vec3 eye, glm::vec3 dir,
                     const RangedAttack& r, const CombatResources& res);
