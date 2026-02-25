#include "engine/ecs/systems/combat_system.h"
#include "engine/ecs/components.h"
#include "engine/physics/raycast.h"
#include "engine/physics/aabb.h"
#include "engine/physics/physics_config.h"
#include <random>

// random number generator for spread
static std::mt19937 rng(std::random_device{}());

// apply spreadh to a direction vector
glm::vec3 applySpread(const glm::vec3& direction, float spread)
{
	if (spread <= 0.0f) return direction;

	std::uniform_real_distribution<float> dist(-spread, spread);
	glm::vec3 spread_dir = direction;
	spread_dir.x += dist(rng);
	spread_dir.y += dist(rng);
	spread_dir.z += dist(rng);
	return glm::normalize(spread_dir);
};

// fidn the closest entity hit by a ray
struct EntityHit
{
	entt::entity entity;
	float distance;
	glm::vec3 point;
};

std::optional<EntityHit> raycastEntities
(
	entt::registry& registry,
	const Ray& ray,
	entt::entity ignore,
	float maxRange
)
{
	std::optional<EntityHit> closest;
	float closestDist = maxRange;

	auto view = registry.view<Position, AABBCollider>();
	for (auto [entity, pos, col] : view.each())
	{
		if (entity == ignore) continue;
		if (col.isTrigger) continue;

		AABB box = AABB::fromCentreSize(pos.value, col.halfExtents);
		auto hit = rayIntersectionsAABB(ray, box);

		if (hit.has_value() && hit.value() < closestDist)
		{
			closestDist = hit.value();
			closest = EntityHit
			{
				entity,
				hit.value(),
				ray.pointAt(hit.value())
			};
		}
	}

	return closest;
}

// ─── Hitscan tracer (debug visualisation) ────────────────────────
void spawnTracer
(
	entt::registry& registry,
	const glm::vec3& start,
	const glm::vec3& end,
	const CombatResources& resources
)
{
	glm::vec3 midpoint = (start + end) * 0.5f;
	glm::vec3 diff = end - start;
	float length = glm::length(diff);
	if (length < 0.01f) return;

	auto tracer = registry.create();
	registry.emplace<Position>(tracer, midpoint);
	registry.emplace<Scale>(tracer, glm::vec3(0.02f, 0.02f, length * 0.5f));
	registry.emplace<MeshRenderer>
	(
		tracer,
		resources.cubeVAO,
		0u,
		resources.shaderId,
		resources.tracerTextureId,
		true,
		resources.cubeIndexCount
	);
	registry.emplace<Lifetime>(tracer, 0.15f);
}

// ─── Splash damage ──────────────────────────────────────────────
void applySplashDamage
(
	entt::registry& registry,
	const glm::vec3 center,
	float radius,
	float maxDamage,
	entt::entity ignore
)
{
	auto view = registry.view<Position, Health>();

	for (auto [entity, pos, health] : view.each())
	{
		if (entity == ignore) continue;

		float distance = glm::length(pos.value - center);
		if (distance > radius) continue;

		// Linear falloff: full damage at center, zero at edge
		float scale = 1.0f - (distance / radius);
		float damage = maxDamage * scale;
		health.current -= damage;

		// knockback - push entity away from explosion
		if (registry.all_of<Velocity>(entity))
		{
			glm::vec3 pushDir = glm::normalize(pos.value - center);
			float knockback = damage *  0.5f;
			registry.get<Velocity>(entity).value += pushDir * knockback;
		}
	}
}


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

		// check against level geometry
		float levelDist = weapon.range;
		// (Would trace ray against level surfaces here — similar to Chapter 9)

		// determine hit point for the tracer
		glm::vec3 hitPoint;
		if (entityHit.has_value() && entityHit->distance < levelDist)
		{
			hitPoint = entityHit->point;

			// apply damage
			if (registry.all_of<Health>(entityHit->entity))
			{
				registry.get<Health>(entityHit->entity).current -= weapon.damage;
			}
		}
		else
		{
			// No entity hit — tracer extends to max range (or level hit)
			hitPoint = origin + dir * levelDist;
		}

		// spawn a visible tracer line
		spawnTracer(registry, origin, hitPoint, resources);
	}
}

// ─── Fire projectile ────────────────────────────────────────────
void fireProjectile
(
	entt::registry& registry,
	entt::entity shooter,
	const Weapon& weapon,
	const glm::vec3& origin,
	const glm::vec3& direction,
	const CombatResources& resources
)
{
	auto projectile = registry.create();

	// Spawn slightly in front of the shooter so it doesn't collide immediately
	registry.emplace<Position>(projectile, origin + direction * 0.5f);
	registry.emplace<Velocity>(projectile, direction * weapon.projectileSpeed);
	registry.emplace<AABBCollider>
	(
		projectile,
		glm::vec3(0.15f, 0.15f, 0.15f),
		false
	);
	registry.emplace<Projectile>
	(
		projectile,
		weapon.damage,
		weapon.splashRadius,
		weapon.splashDamage,
		shooter
	);
	registry.emplace<Lifetime>(projectile, 10.0f); // Despawn after 10 seconds

	// visual: a small coloured cube
	registry.emplace<Scale>(projectile, glm::vec3(0.3f));
	registry.emplace<MeshRenderer>
	(
		projectile, 
		resources.cubeVAO,
		0u,
		resources.shaderId,
		resources.projectileTextureId,
		true,
		resources.cubeIndexCount
	);
}

// ─── Main combat system ─────────────────────────────────────────

void combatSystem
(
	entt::registry& registry,
	const Level& level	
)
{
	const auto& config = registry.ctx().get<PhysicsConfig>();
	const auto& resources = registry.ctx().get<CombatResources>();
	float dt = config.fixedDeltaTime;

	 // ─── Weapon cooldowns ────────────────────────────────────────
	 auto weaponView = registry.view<WeaponInventory>();
	 for (auto [entity, inv] : weaponView.each())
	 {
		for (auto& weapon : inv.weapons)
		{
			if (weapon.cooldownRemaining > 0.0f)
			{
				weapon.cooldownRemaining -= dt;
			}
		}
	 }

	 // ─── Handle fire input ───────────────────────────────────────

	 auto shooterView = registry.view<Position, PlayerInput, WeaponInventory>();

	 for (auto [entity, pos, input, inv] : shooterView.each())
	 {
		if (!input.fire) continue;
		if (inv.weapons.empty()) continue;
	
		Weapon& weapon = inv.weapons[inv.currentWeapon];
		if (weapon.cooldownRemaining > 0.0f) continue;

        // Get firing direction from camera front vector
        // The camera direction is written into the registry context each frame
		const auto& cameraDir = registry.ctx().get<glm::vec3>();

		glm::vec3 fireOrigin = pos.value + glm::vec3(0.0f, 0.7f, 0.0f); // Eye height

		if (weapon.fireMode == FireMode::Hitscan)
		{
			fireHitscan
			(
				registry,
				level,
				entity,
				weapon,
				fireOrigin,
				cameraDir,
				resources
			);
		}
		else
		{
			fireProjectile
			(
				registry,
				entity,
				weapon,
				fireOrigin,
				cameraDir,
				resources
			);
		}

		weapon.cooldownRemaining = weapon.fireRate;
	 }

	// ─── Projectile collision ────────────────────────────────────
	auto projView = registry.view<Position, Velocity, AABBCollider, Projectile>();
	std::vector<entt::entity> toDestroy;

	for (auto [projEntity, pos, vel, col, proj] : projView.each())
	{
		AABB projBox = AABB::fromCentreSize(pos.value, col.halfExtents);

		// check against entities with health
		auto entityView = registry.view<Position, AABBCollider, Health>();
		for (auto [target, tPos, tCol, health] : entityView.each())
		{
			if (target == proj.owner) continue;
			if (tCol.isTrigger) continue;

			AABB targetBox = AABB::fromCentreSize(tPos.value, tCol.halfExtents);
			if (projBox.intersects(targetBox))
			{
				// direct hit
				health.current -= proj.damage;
				// splash damage - hurt nearby entities too
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