#pragma once
// Result of an entity raycast — closest entity hit and where. (Relocated from
// combat_system.cpp per CODING_STANDARD §2.)

#include <entt/entt.hpp>
#include <glm/glm.hpp>

struct EntityHit
{
	entt::entity entity;
	float distance;
	glm::vec3 point;
};
