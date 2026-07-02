#pragma once
// AABB (Axis-Aligned Bounding Box) — value type used by combat hit tests and
// trigger overlap. (Relocated from physics/aabb.h per CODING_STANDARD §2.)

#include <glm/glm.hpp>
#include <optional>

struct AABB {
	glm::vec3 min;
	glm::vec3 max;

	// create from center and half-extends
	static AABB fromCentreSize
	(
		const glm::vec3& center,
		const glm::vec3& halfExtends
	)
	{
		return
		{
			center - halfExtends,
			center + halfExtends
		};
	};

	glm::vec3 center() const { return (min + max) * 0.5f; }
	glm::vec3 size() const { return max - min; }
	glm::vec3 halfExtends() const { return size() * 0.5f; }

	// does this AABB contain a point?
	bool contains(const glm::vec3& point) const
	{
		return
		point.x >= min.x && point.x <= max.x &&
		point.y >= min.y && point.y <= max.y &&
		point.z >= min.z && point.z <= max.z;
	}

	// do two AABBs overlap?
	bool intersects(const AABB& other) const
	{
		return
		(min.x <= other.max.x && max.x >= other.min.x) &&
		(min.y <= other.max.y && max.y >= other.min.y) &&
		(min.z <= other.max.z && max.z >= other.min.z);
	}

	// epand this AABB to include a point
	void encapsulate(const glm::vec3& point)
	{
		min = glm::min(min, point);
		max = glm::max(max, point);
	}

	// translate (move) the AABB
	AABB translated(const glm::vec3& offset) const
	{
		return { min + offset, max + offset};
	}
};
