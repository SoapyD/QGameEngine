#include "engine/ecs/systems/combat/combat_internal.h"

#include "engine/ecs/components.h"
#include "engine/ecs/apply_damage.h"
#include "engine/physics/types/aabb.h"
#include "engine/physics/jolt_world.h"

#include <vector>

// ─── Projectile movement & collision ─────────────────────────────
// Advance each projectile, test it against level geometry and entity
// colliders, apply damage/knockback/splash on impact, then destroy.
void updateProjectiles(entt::registry& registry, const Level& level, float dt)
{
	auto projView = registry.view<Position, Velocity, AABBCollider, Projectile>();
	std::vector<entt::entity> toDestroy;

	for (auto [projEntity, pos, vel, col, proj] : projView.each())
	{
		// move projectile (no Jolt body — simple velocity integration)
		pos.value += vel.value * dt;

		AABB projBox = AABB::fromCentreSize(pos.value, col.halfExtents);

		// ── Level geometry collision ────────────────────────────
		// Walls/floors are not ECS entities, so the entity sweep below
		// never sees them — without this, projectiles tunnel through the
		// level (eval 07 §7.2).
		if (boxHitsLevel(level, projBox))
		{
			if (proj.splashRadius > 0.0f)
			{
				applySplashDamage(registry, pos.value,
					proj.splashRadius, proj.splashDamage, proj.owner);
			}
			toDestroy.push_back(projEntity);
			continue;  // projectile consumed by the wall/floor
		}

		// check against ALL colliders (not just entities with Health)
		auto entityView = registry.view<Position, AABBCollider>();
		for (auto [target, tPos, tCol] : entityView.each())
		{
			if (target == projEntity) continue;  // don't collide with self
			if (target == proj.owner) continue;
			if (tCol.isTrigger) continue;

			AABB targetBox = AABB::fromCentreSize(tPos.value, tCol.halfExtents);
			if (projBox.intersects(targetBox))
			{
				// apply direct-hit damage (armour-aware; flashes internally)
				if (applyDamage(registry, target, proj.damage))
				{
					// Knockback: push target away from projectile
					if (registry.all_of<PendingKnockback>(target))
					{
						glm::vec3 knockDir = glm::normalize(vel.value);
						registry.get<PendingKnockback>(target).impulse += knockDir * 1.6f;
					}
				}

				// push Jolt bodies on impact
				if (registry.all_of<JoltBody>(target))
				{
					auto& joltBody = registry.get<JoltBody>(target);
					auto& jolt = registry.ctx().get<JoltWorld>();
					glm::vec3 dir = glm::normalize(vel.value);
					float impulseMag = proj.damage * 2.0f;
					jolt.getBodyInterface().AddImpulse
					(
						joltBody.id,
						JPH::Vec3(dir.x * impulseMag, dir.y * impulseMag, dir.z * impulseMag)
					);
				}

				// splash damage — hurt nearby entities too
				if (proj.splashRadius > 0.0f)
				{
					applySplashDamage
					(
						registry,
						pos.value,
						proj.splashRadius,
						proj.splashDamage,
						proj.owner
					);
				}

				toDestroy.push_back(projEntity);
				break;
			}
		}
	}

	for(auto e : toDestroy)
	{
		if (registry.valid(e))
		{
			registry.destroy(e);
		}
	}
}
