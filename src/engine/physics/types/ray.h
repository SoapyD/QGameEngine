#pragma once
// Ray + RayHit — value types for raycasting. (Relocated from physics/raycast.h
// per CODING_STANDARD §2; the raycast *functions* stay in physics/raycast.h.)

#include <glm/glm.hpp>
#include <entt/entt.hpp>

struct Ray
{
	glm::vec3 origin;
	glm::vec3 direction; //should be normalised

	glm::vec3 pointAt(float t) const
	{
		return origin + direction * t;
	}
};

struct RayHit
{
	float distance; // how far along the ray
	glm::vec3 point; // world-space hit point
	glm::vec3 normal; // surface normal at hit point
	entt::entity entity; // what was hit
};
