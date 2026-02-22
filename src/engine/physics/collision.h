#pragma once

#include "engine/physics/aabb.h"

struct SweepResult
{
	float time; // 0.0 to 1.0 - how along the movement
	glm::vec3 normal; // surface normal of what was hit
	bool hit;
};

// test is a moving AABB hits a static AABB
SweepResult sweepAABB
(
	const AABB& moving, 
	const glm::vec3& velocity,
	const AABB& stationary
);

