#include "engine/ecs/systems/collision_system.h"
#include "engine/ecs/components.h"
#include "engine/physics/aabb.h"
#include "engine/physics/collision.h"
#include "engine/physics/physics_config.h"

void collisionSystem
(
	entt::registry& registry,
	SpatialHash& spatialHash,
	const Level& level
)
{
	const auto& config = registry.ctx().get<PhysicsConfig>();
	// rebuild the spatial hash each frame
	spatialHash.clear();

	auto collisables = registry.view<Position, AABBCollider>();
	for (auto [entity, pos, col] : collisables.each())
	{
		spatialHash.insert(entity, pos.value, col.halfExtents);
	}

	// 2 for each entity with velocity, sweep against nerby entities and level
	auto movers = registry.view<Position, Velocity, AABBCollider>();

	for (auto [entity, pos, vel, col] : movers.each())
	{
		glm::vec3 movement = vel.value * config.fixedDeltaTime;

		if (glm::length(movement) < 1e-6f) continue; // not moving

		AABB entityBox = AABB::fromCentreSize(pos.value, col.halfExtents);

		// check against level surfaces (simplified: treat each surface AABB)
		for (const auto& sector : level.sectors)
		{
			for (const auto& surface : sector.surfaces)
			{
				// build an AABB for the surface (thin slab)
				glm::vec3 surfMin = glm::min
				(
					glm::min(surface.vertices[0], surface.vertices[1]),
					glm::min(surface.vertices[2], surface.vertices[3]) 
				);
				glm::vec3 surfMax = glm::max
				(
					glm::max(surface.vertices[0], surface.vertices[1]),
					glm::max(surface.vertices[2], surface.vertices[3]) 
				);

				for (int i = 0; i < 3; i++)
				{
					if(surfMax[i] - surfMin[i] < 0.01f)
					{
						surfMin[i] -= 0.01f;
						surfMax[i] += 0.01f;
					}
				}

				AABB surfaceBox = { surfMin, surfMax };
			
				SweepResult hit = sweepAABB(entityBox, movement, surfaceBox);
				if (hit.hit)
				{
					// slide along the surface: remove the velocity component
					// that goes into the wall
					float dot = glm::dot(vel.value, hit.normal);
					if (dot < 0.0f)
					{
						vel.value -= hit.normal * dot;
					} 
				}
			}

			// check against other entities
			auto nearby = spatialHash.query(pos.value, col.halfExtents + glm::vec3(2.0f));
			for (auto other : nearby)
			{
				if (other == entity) continue;
				if (!registry.all_of<Position, AABBCollider>(other)) continue;

				auto& otherPos = registry.get<Position>(other);
				auto& otherCol = registry.get<AABBCollider>(other);
				AABB otherBox = AABB::fromCentreSize(otherPos.value, otherCol.halfExtents);

				// if it's a trigger, don't resolve - just detect
				if (otherCol.isTrigger)
				{
					if (entityBox.intersects(otherBox))
					{
						// trigger detected - capter 11 will handle this
					}
					continue;
				}

				bool shouldCollide = (col.layer & otherCol.mask) != 0 &&
								(otherCol.layer & col.mask) != 0;

				if (!shouldCollide) continue;

				SweepResult hit = sweepAABB(entityBox, movement, otherBox);
				if (hit.hit)
				{
					float dot = glm::dot(vel.value, hit.normal);
					if (dot < 0.0f)
					{
						vel.value -= hit.normal * dot;
					}
				}
			}
		}
	}
}
