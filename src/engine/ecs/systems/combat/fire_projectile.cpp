#include "engine/ecs/systems/combat/combat_internal.h"

#include "engine/ecs/components.h"

// ─── Fire projectile ────────────────────────────────────────────
void fireProjectile
(
	entt::registry& registry,
	entt::entity shooter,
	const Weapon& weapon,
	const glm::vec3& origin,
	const glm::vec3& direction,
	const CombatResources& resources
)
{
	auto projectile = registry.create();

	// Spawn slightly in front of the shooter so it doesn't collide immediately
	registry.emplace<Position>(projectile, origin + direction * 0.5f);
	registry.emplace<Velocity>(projectile, direction * weapon.projectileSpeed);
	registry.emplace<AABBCollider>
	(
		projectile,
		glm::vec3(0.15f, 0.15f, 0.15f),
		false
	);
	registry.emplace<Projectile>
	(
		projectile,
		weapon.damage,
		weapon.splashRadius,
		weapon.splashDamage,
		shooter
	);
	registry.emplace<Lifetime>(projectile, 10.0f); // Despawn after 10 seconds

	// visual: a small coloured cube
	registry.emplace<Scale>(projectile, glm::vec3(0.3f));
	registry.emplace<MeshRenderer>
	(
		projectile,
		resources.cubeVAO,
		0u,
		resources.shaderId,
		resources.projectileTextureId,
		true,
		resources.cubeIndexCount
	);
}
