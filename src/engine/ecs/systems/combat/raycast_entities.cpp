#include "engine/ecs/systems/combat/combat_internal.h"

#include "engine/ecs/components.h"
#include "engine/physics/raycast.h"
#include "engine/physics/types/aabb.h"

// find the closest entity hit by a ray
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
