#include "engine/ecs/systems/combat/combat_internal.h"

// True if `box` overlaps any level surface. Level geometry isn't made of ECS
// entities, so projectile (and could-be hitscan) collision must test it here.
bool boxHitsLevel(const Level& level, const AABB& box)
{
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
			// Inflate thin axes so the AABB has volume
			surfBox.min -= glm::vec3(0.05f);
			surfBox.max += glm::vec3(0.05f);

			if (box.intersects(surfBox)) return true;
		}
	}
	return false;
}
