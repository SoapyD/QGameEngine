#include "engine/ecs/systems/lifetime/lifetime_system.h"
#include "engine/ecs/components.h"
#include "engine/physics/physics_config.h"

void lifetimeSystem(entt::registry& registry)
{
	const auto& config = registry.ctx().get<PhysicsConfig>();
	float dt = config.fixedDeltaTime;

	auto view = registry.view<Lifetime>();
	std::vector<entt::entity> expired;

	for (auto [entity, lifetime] : view.each())
	{
		lifetime.remaining -= dt;
		if (lifetime.remaining <= 0.0f)
		{
			expired.push_back(entity);
		}
	}

	for (auto e : expired)
	{
		registry.destroy(e);
	}
}