#include "engine/physics/raycast.h"

#include <algorithm>
#include <cmath>
#include <limits>

std::optional<float> rayIntersectionsAABB
(
	// slab method, fidn the overlap of ray intervals on each axis
	const Ray& ray,
	const AABB& box
)
{
	float tmin = 0.0f;
	float tmax = std::numeric_limits<float>::max();

	for (int i = 0; i < 3; i++)
	{
		float origin = ray.origin[i];
		float dir = ray.direction[i];
		float bmin = box.min[i];
		float bmax = box.max[i];

		if (std::abs(dir) < 1e-8f)
		{
			// ray is parallell to this axis
			if (origin < bmin || origin > bmax)
			{
				return std::nullopt; // ray misses entirely
			}
		}
		else
		{
			float t1 = (bmin - origin) / dir;
			float t2 = (bmax - origin) / dir;

			if (t1 > t2) std::swap(t1, t2);

			tmin = std::max(tmin, t1);
			tmax = std::min(tmax, t2);

			if (tmin > tmax)
			{
				return std::nullopt; // no overlap
			}
		}
	}

	if (tmin < 0.0f) return std::nullopt; // hit is behind the ray

	return tmin;
}
