#include "engine/ecs/systems/combat/combat_internal.h"

#include "engine/ecs/components.h"
#include "engine/ecs/apply_damage.h"
#include "engine/physics/jolt_world.h"

// ─── Splash damage ──────────────────────────────────────────────
void applySplashDamage
(
	entt::registry& registry,
	const glm::vec3 center,
	float radius,
	float maxDamage,
	entt::entity ignore
)
{
	auto view = registry.view<Position, Health>();

	for (auto [entity, pos, health] : view.each())
	{
		if (entity == ignore) continue;

		float distance = glm::length(pos.value - center);
		if (distance > radius) continue;

		// Linear falloff: full damage at center, zero at edge
		float scale = 1.0f - (distance / radius);
		float damage = maxDamage * scale;

		// Armour-aware damage (invuln + flash handled inside).
		applyDamage(registry, entity, damage);

		// knockback - push entity away from explosion
		if (registry.all_of<Velocity>(entity))
		{
			glm::vec3 pushDir = glm::normalize(pos.value - center);
			float knockback = damage *  0.5f;
			registry.get<Velocity>(entity).value += pushDir * knockback;
		}
	}

	// push Jolt bodies away from explosion (even without Health)
	auto joltView = registry.view<Position, JoltBody>();
	for (auto [entity, pos, joltBody] : joltView.each())
	{
		if (entity == ignore) continue;

		float distance = glm::length(pos.value - center);
		if (distance > radius) continue;

		float scale = 1.0f - (distance / radius);
		glm::vec3 pushDir = (distance > 0.01f)
			? glm::normalize(pos.value - center)
			: glm::vec3(0.0f, 1.0f, 0.0f);
		float knockback = maxDamage * scale * 2.0f;

		auto& jolt = registry.ctx().get<JoltWorld>();
		jolt.getBodyInterface().AddImpulse
		(
			joltBody.id,
			JPH::Vec3(pushDir.x * knockback, pushDir.y * knockback, pushDir.z * knockback)
		);
	}
}
