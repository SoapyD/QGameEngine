#include "engine/ecs/systems/combat/combat_internal.h"

#include <random>

// random number generator for spread
static std::mt19937 rng(std::random_device{}());

// apply spread to a direction vector
glm::vec3 applySpread(const glm::vec3& direction, float spread)
{
	if (spread <= 0.0f) return direction;

	std::uniform_real_distribution<float> dist(-spread, spread);
	glm::vec3 spread_dir = direction;
	spread_dir.x += dist(rng);
	spread_dir.y += dist(rng);
	spread_dir.z += dist(rng);
	return glm::normalize(spread_dir);
}
