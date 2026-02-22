#include "engine/physics/raycast.h"
#include <algorithm>
#include <cmath>


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


std::optional<float> rayIntersectsTriangle
(
	const Ray& ray,
	const glm::vec3 v0,
	const glm::vec3 v1,
	const glm::vec3 v2
)
{
	const float EPSILON = 1e-7f;

	glm::vec3 edge1 = v1 - v0;
	glm::vec3 edge2 = v2 - v0;
	glm::vec3 h = glm::cross(ray.direction, edge2);
	float a = glm::dot(edge1, h);

	// ray is parallell to triangle
	if (std::abs(a) < EPSILON) return std::nullopt;

	float f = 1.0f / a;
	glm::vec3 s = ray.origin - v0;
	float u = f * glm::dot(s, h);

	if (u < 0.0f || u > 1.0f) return std::nullopt;

	glm::vec3 q = glm::cross(s, edge1);
	float v = f * glm::dot(ray.direction, q);

	if ( v < 0.0f || u + v > 1.0f) return std::nullopt;

	float t = f * glm::dot(edge2, q);

	if (t > EPSILON)
	{
		return t;
	}

	return std::nullopt; // behind the ray
};