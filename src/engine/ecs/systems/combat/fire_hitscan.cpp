#include "engine/ecs/systems/combat/combat_internal.h"

#include "engine/ecs/components.h"
#include "engine/physics/raycast.h"
#include "engine/physics/types/aabb.h"

// ─── Fire hitscan ───────────────────────────────────────────────
void fireHitscan
(
	entt::registry& registry,
	const Level& level,
	entt::entity shooter,
	const Weapon& weapon,
	const glm::vec3& origin,
	const glm::vec3& direction,
	const CombatResources& resources
)
{
	for (int i = 0; i < weapon.pelletCount; i++)
	{
		glm::vec3 dir = applySpread(direction, weapon.spread);
		Ray ray { origin, dir };

		// check against entities
		auto entityHit = raycastEntities(registry, ray, shooter, weapon.range);

		// check against level geometry (floors, walls, ceilings)
		float levelDist = weapon.range;
		for (const auto& sector : level.sectors)
		{
			for (const auto& surface : sector.surfaces)
			{
				// Build a thin AABB for the surface
				AABB surfBox;
				surfBox.min = glm::min(
					glm::min(surface.vertices[0], surface.vertices[1]),
					glm::min(surface.vertices[2], surface.vertices[3]));
				surfBox.max = glm::max(
					glm::max(surface.vertices[0], surface.vertices[1]),
					glm::max(surface.vertices[2], surface.vertices[3]));
				// Inflate thin axes so the AABB has volume
				surfBox.min -= glm::vec3(0.05f);
				surfBox.max += glm::vec3(0.05f);

				auto surfHit = rayIntersectionsAABB(ray, surfBox);
				if (surfHit.has_value() && surfHit.value() < levelDist)
				{
					levelDist = surfHit.value();
				}
			}
		}

		// determine hit point for the tracer
		glm::vec3 hitPoint;
		if (entityHit.has_value() && entityHit->distance < levelDist)
		{
			hitPoint = entityHit->point;

			// apply damage
			if (registry.all_of<Health>(entityHit->entity))
			{
				auto& health = registry.get<Health>(entityHit->entity);
				if (health.invulnerableTimer <= 0.0f)
				{
					float before = health.current;
					health.current -= weapon.damage;
					if (health.current < 0.0f) health.current = 0.0f;  // NEW

					// trigger damage flash if health actually decreated
					if (health.current < before && registry.all_of<DamageFlash>(entityHit->entity))
					{
						auto& flash = registry.get<DamageFlash>(entityHit->entity);
						flash.timer = flash.duration;
					}

					// Knockback: push target in the direction of the shot
					if (health.current < before && registry.all_of<PendingKnockback>(entityHit->entity))
					{
						glm::vec3 knockDir = glm::normalize(direction);
						registry.get<PendingKnockback>(entityHit->entity).impulse += knockDir * 1.0f;
					}
				}
			}
		}
		else
		{
			// No entity hit — tracer extends to max range (or level hit)
			hitPoint = origin + dir * levelDist;
		}

		// Offset the tracer start to a "gun barrel" position — slightly
		// down and right of the camera so it's not edge-on invisible.
		// The ray itself fires from camera centre for accurate aiming.
		glm::vec3 right = glm::normalize(glm::cross(dir, glm::vec3(0, 1, 0)));
		glm::vec3 tracerStart = origin + right * 0.3f - glm::vec3(0, 0.2f, 0);
		spawnTracer(registry, tracerStart, hitPoint, resources);
	}
}
