#include "engine/ecs/systems/physics_system.h"
#include "engine/ecs/components.h"
#include "engine/physics/physics_config.h"
#include "engine/physics/raycast.h"
#include "engine/level/level.h"

void physicsSystem(entt::registry& registry)
{
	const auto& config = registry.ctx().get<PhysicsConfig>();

	// ─── Apply gravity ───────────────────────────────────────────
	auto gravityView = registry.view<Velocity, Gravity, OnGround>();
	
	for (auto [entity, vel, grav, ground] : gravityView.each())
	{
		if (!ground.value)
		{
			vel.value.y -= grav.strength * config.fixedDeltaTime;

			// terminal velocity (cap fall speed)
			if (vel.value.y < -config.terminalVelocity)
			{
				vel.value.y = -config.terminalVelocity;
			}
		}

	}

	// ─── Apply friction ──────────────────────────────────────────
	auto frictionView = registry.view<Velocity, OnGround, CharacterPhysics>();

	for (auto [entity, vel, ground, phys] : frictionView.each())
	{
		float friction = ground.value ? phys.groundFriction : phys.airFriction;

		// only apply friction to horizontal movement (X and Z)
		glm::vec2 horizontal(vel.value.x, vel.value.z);
		float speed = glm::length(horizontal);

		if (speed < 0.1f)
		{
			// below threshold - just stop
			if (ground.value)
			{
				vel.value.x = 0.0f;
				vel.value.z = 0.0f;
			}
			continue;
		}

		// friction reduces speed
		float drop = speed * friction * config.fixedDeltaTime;
		float newSpeed = std::max(speed - drop, 0.0f);
		float scale = newSpeed / speed;

		vel.value.x *= scale;
		vel.value.z *= scale;
	}
}

void groundDetectionSystem(entt::registry& registry, const Level& level)
{
	auto view = registry.view<Position, AABBCollider, OnGround>();

	for (auto [entity, pos, col, ground] : view.each())
	{
		// Cast a short ray downward from the bottom of the collider
		glm::vec3 feetPos = pos.value - glm::vec3(0.0f, col.halfExtents.y, 0.0f);
		float probeDistance = 0.1f;  // Small distance below feet

		Ray downRay;
		downRay.origin = feetPos;
		downRay.direction = glm::vec3(0.0f, -1.0f, 0.0f);

		ground.value = false;

		// Check against level geometry
		for (const auto& sector : level.sectors)
		{
			for (const auto& surface : sector.surfaces)
			{
				// Only check roughly horizontal surfaces (floors)
				if (surface.normal.y < 0.7f) continue;

				// Build a thin AABB for the surface
				AABB surfBox;
				surfBox.min = glm::min(
					glm::min(surface.vertices[0], surface.vertices[1]),
					glm::min(surface.vertices[2], surface.vertices[3]));
				surfBox.max = glm::max(
					glm::max(surface.vertices[0], surface.vertices[1]),
					glm::max(surface.vertices[2], surface.vertices[3]));
				surfBox.min.y -= 0.05f;
				surfBox.max.y += 0.05f;

				auto hit = rayIntersectionsAABB(downRay, surfBox);
				if (hit.has_value() && hit.value() <= probeDistance)
				{
					ground.value = true;
					break;
				}
			}
			if (ground.value) break;
		}
	}
}