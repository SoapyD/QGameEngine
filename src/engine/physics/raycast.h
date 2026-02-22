#pragma once

#include "engine/physics/aabb.h"
#include "glm/glm.hpp"
#include "entt/entt.hpp"
#include <optional>

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

std::optional<float> rayIntersectionsAABB
(
	const Ray& ray,
	const AABB& box
);

std::optional<float> rayIntersectsTriangle
(
	const Ray& ray,
	const glm::vec3 v0,
	const glm::vec3 v1,
	const glm::vec3 v2
);