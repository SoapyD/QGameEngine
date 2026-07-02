#pragma once

#include "engine/physics/types/aabb.h"
#include "engine/physics/types/ray.h"
#include "glm/glm.hpp"
#include <optional>

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
