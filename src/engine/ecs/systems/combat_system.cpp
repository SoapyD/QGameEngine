#include "engine/ecs/systems/combat_system.h"
#include "engine/ecs/components.h"
#include "engine/physics/raycast.h"
#include "engine/physics/aabb.h"
#include "engine/physics/physics_config.h"
#include "engine/physics/jolt_world.h"
#include <random>
#include <iostream>

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
// Spawns a thin wireframe cube stretched between start and end,
// rotated to align with the fire direction.
void spawnTracer
(
	entt::registry& registry,
	const glm::vec3& start,
	const glm::vec3& end,
	const CombatResources& resources
)
{
	glm::vec3 diff = end - start;
	float length = glm::length(diff);
	if (length < 0.01f) return;

	glm::vec3 dir = diff / length;
	glm::vec3 midpoint = (start + end) * 0.5f;

	// Calculate Euler angles to align the cube's Z axis with the ray direction
	// Yaw: rotation around Y axis (horizontal angle)
	float yaw = glm::degrees(std::atan2(dir.x, dir.z));
	// Pitch: rotation around X axis (vertical angle)
	float pitch = glm::degrees(-std::asin(dir.y));

	auto tracer = registry.create();
	registry.emplace<Position>(tracer, midpoint);
	registry.emplace<Rotation>(tracer, glm::vec3(pitch, yaw, 0.0f));
	registry.emplace<Scale>(tracer, glm::vec3(0.03f, 0.03f, length));
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
	registry.emplace<TagDebugWireframe>(tracer);
	registry.emplace<Lifetime>(tracer, 2.0f);  // Hang in the air for 2 seconds
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
		if (health.invulnerableTimer <= 0.0f)
		{
			float before = health.current;
			health.current -= damage;
			if (health.current < 0.0f) health.current = 0.0f;

			// trigger damage flash if health actually decreated
			if (health.current < before && registry.all_of<DamageFlash>(entity))
			{
				auto& flash = registry.get<DamageFlash>(entity);
				flash.timer = flash.duration;
			}
		}

		// knockback - push entity away from explosion
		if (registry.all_of<Velocity>(entity))
		{
			glm::vec3 pushDir = glm::normalize(pos.value - center);
			float knockback = damage *  0.5f;
			registry.get<Velocity>(entity).value += pushDir * knockback;
		}
	}

	// push Jolt bodies away from explosion (even without Health)
	auto joltView = registry.view<Position, JoltBody>();
	for (auto [entity, pos, joltBody] : joltView.each())
	{
		if (entity == ignore) continue;

		float distance = glm::length(pos.value - center);
		if (distance > radius) continue;

		float scale = 1.0f - (distance / radius);
		glm::vec3 pushDir = (distance > 0.01f)
			? glm::normalize(pos.value - center)
			: glm::vec3(0.0f, 1.0f, 0.0f);
		float knockback = maxDamage * scale * 2.0f;

		auto& jolt = registry.ctx().get<JoltWorld>();
		jolt.getBodyInterface().AddImpulse
		(
			joltBody.id,
			JPH::Vec3(pushDir.x * knockback, pushDir.y * knockback, pushDir.z * knockback)
		);
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
		const auto& cameraDir = registry.ctx().get<CameraDirection>().value;

		// Fire from eye height — position is at body centre, offset upward
		
		// glm::vec3 fireOrigin = pos.value; // Already at eye height (synced from camera)
		float eyeOffset = 0.0f;
		if (registry.all_of<AABBCollider>(entity))
		{
			eyeOffset = registry.get<AABBCollider>(entity).halfExtents.y * kEyeHeightFraction;
		}
		glm::vec3 fireOrigin = pos.value + glm::vec3(0.0f, eyeOffset, 0.0f);
		
		if (weapon.fireMode == FireMode::Hitscan)
		{
			// std::cout << "Hitscan fired" << std::endl;
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

	// ─── Projectile movement & collision ─────────────────────────
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
		// level (eval 07 §7.2). Mirror the hitscan surface test.
		bool hitLevel = false;
		for (const auto& sector : level.sectors)
		{
			for (const auto& surface : sector.surfaces)
			{
				AABB surfBox;
				surfBox.min = glm::min(
					glm::min(surface.vertices[0], surface.vertices[1]),
					glm::min(surface.vertices[2], surface.vertices[3]));
				surfBox.max = glm::max(
					glm::max(surface.vertices[0], surface.vertices[1]),
					glm::max(surface.vertices[2], surface.vertices[3]));
				surfBox.min -= glm::vec3(0.05f);
				surfBox.max += glm::vec3(0.05f);

				if (projBox.intersects(surfBox)) { hitLevel = true; break; }
			}
			if (hitLevel) break;
		}
		if (hitLevel)
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
				// apply damage if the target has Health
				if (registry.all_of<Health>(target))
				{
					auto& health = registry.get<Health>(target);
					if (health.invulnerableTimer <= 0.0f)
					{
						float before = health.current;
						health.current -= proj.damage;
						if (health.current < 0.0f) health.current = 0.0f;  // NEW
					
						// trigger damage flash if health actually decreated
						if (health.current < before && registry.all_of<DamageFlash>(target))
						{
							auto& flash = registry.get<DamageFlash>(target);
							flash.timer = flash.duration;
						}

						// Knockback: push target away from projectile
						if (health.current < before && registry.all_of<PendingKnockback>(target))
						{
							glm::vec3 knockDir = glm::normalize(vel.value);
							registry.get<PendingKnockback>(target).impulse += knockDir * 1.6f;
						}
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