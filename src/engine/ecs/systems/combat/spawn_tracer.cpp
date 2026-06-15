#include "engine/ecs/systems/combat/combat_internal.h"

#include "engine/ecs/components.h"

#include <cmath>

// ─── Hitscan tracer (debug visualisation) ────────────────────────
// Spawns a thin wireframe cube stretched between start and end,
// rotated to align with the fire direction.
void spawnTracer
(
	entt::registry& registry,
	const glm::vec3& start,
	const glm::vec3& end,
	const CombatResources& resources
)
{
	glm::vec3 diff = end - start;
	float length = glm::length(diff);
	if (length < 0.01f) return;

	glm::vec3 dir = diff / length;
	glm::vec3 midpoint = (start + end) * 0.5f;

	// Calculate Euler angles to align the cube's Z axis with the ray direction
	// Yaw: rotation around Y axis (horizontal angle)
	float yaw = glm::degrees(std::atan2(dir.x, dir.z));
	// Pitch: rotation around X axis (vertical angle)
	float pitch = glm::degrees(-std::asin(dir.y));

	auto tracer = registry.create();
	registry.emplace<Position>(tracer, midpoint);
	registry.emplace<Rotation>(tracer, glm::vec3(pitch, yaw, 0.0f));
	registry.emplace<Scale>(tracer, glm::vec3(0.03f, 0.03f, length));
	registry.emplace<MeshRenderer>
	(
		tracer,
		resources.cubeVAO,
		0u,
		resources.shaderId,
		resources.tracerTextureId,
		true,
		resources.cubeIndexCount
	);
	registry.emplace<TagDebugWireframe>(tracer);
	registry.emplace<Lifetime>(tracer, 2.0f);  // Hang in the air for 2 seconds
}
