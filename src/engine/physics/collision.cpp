#include "engine/physics/collision.h"
#include <algorithm>
#include <cmath>

SweepResult sweepAABB
(
	const AABB& moving, 
	const glm::vec3& velocity,
	const AABB& stationary
)
{
	SweepResult result;
	result.time = 1.0f;
	result.normal = glm::vec3(0.0f);
	result.hit = false;

	// minkowski difference: expand the stationary box by the moving box
	// then do a ray cast from the moving box's center
	AABB expanded;
	expanded.min = stationary.min - moving.halfExtends();
	expanded.max = stationary.max + moving.halfExtends();

	glm::vec3 origin = moving.center();

	// find entry and exit times for each axis
	float entryTime = 0.0f;
	float exitTime = 1.0f;
	glm::vec3 entryNormal(0.0f);

	for (int i = 0; i < 3; i++)
	{
		if (std::abs(velocity[i]) < 1e-8f)
		{
			// mot moving on this axis - must already be overlapping
			if (origin[i] < expanded.min[i] || origin[i] > expanded.max[i])
			{
				return result; // no collision possible
			}
		}
		else 
		{
			float t1 = (expanded.min[i] - origin[i]) / velocity[i];
			float t2 = (expanded.max[i] - origin[i]) / velocity[i];

			glm::vec3 normal(0.0f);
			if (t1 > t2)
			{
				std::swap(t1, t2);
				normal[i] = 1.0f;
			}
			else
			{
				normal[i] = -1.0f;
			}

			if (t1 > entryTime)
			{
				entryTime = t1;
				entryNormal = normal;
			}

			exitTime = std::min(exitTime, t2);
		}
	}

	// check for valid collision
	if (entryTime <= exitTime && entryTime >= 0.0f && entryTime < 1.0f)
	{
		result.time = entryTime;
		result.normal = entryNormal;
		result.hit = true;
	}

	return result;
}