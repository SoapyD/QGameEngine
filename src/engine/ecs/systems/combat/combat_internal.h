#pragma once
// Internal helpers shared across the combat system's split .cpp files. NOT the
// public API — that is combat_system.h. Each helper is defined in its own file.

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <optional>

#include "engine/ecs/components/combat.h"   // Weapon, CombatResources
#include "engine/ecs/types/entity_hit.h"
#include "engine/level/level.h"
#include "engine/physics/types/aabb.h"
#include "engine/physics/types/ray.h"

// Perturb a fire direction by a random cone (shotgun spread).
glm::vec3 applySpread(const glm::vec3& direction, float spread);

// Closest entity hit by a ray (ignoring `ignore`, within `maxRange`).
std::optional<EntityHit> raycastEntities(entt::registry& registry, const Ray& ray,
                                         entt::entity ignore, float maxRange);

// Spawn a short-lived wireframe tracer between start and end.
void spawnTracer(entt::registry& registry, const glm::vec3& start, const glm::vec3& end,
                 const CombatResources& resources);

// Radial damage + knockback around a point (rockets/grenades).
void applySplashDamage(entt::registry& registry, const glm::vec3 center, float radius,
                       float maxDamage, entt::entity ignore);

// Fire a hitscan weapon (raycast vs entities + level, damage, tracer).
void fireHitscan(entt::registry& registry, const Level& level, entt::entity shooter,
                 const Weapon& weapon, const glm::vec3& origin, const glm::vec3& direction,
                 const CombatResources& resources);

// Spawn a moving projectile entity.
void fireProjectile(entt::registry& registry, entt::entity shooter, const Weapon& weapon,
                    const glm::vec3& origin, const glm::vec3& direction,
                    const CombatResources& resources);

// True if an axis-aligned box overlaps any level surface (walls/floors aren't
// ECS entities, so projectile/hitscan code must test them explicitly).
bool boxHitsLevel(const Level& level, const AABB& box);

// Advance projectiles, resolve level + entity collisions, destroy on impact.
void updateProjectiles(entt::registry& registry, const Level& level, float dt);
