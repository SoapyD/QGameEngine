#include "engine/physics/raycast.h"

#include <cmath>

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
}
