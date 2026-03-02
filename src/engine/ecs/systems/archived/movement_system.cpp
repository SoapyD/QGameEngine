#include "engine/ecs/systems/movement_system.h"
#include "engine/ecs/components.h"
#include "engine/physics/physics_config.h"

void movementSystem(entt::registry& registry)
{
	const auto& config = registry.ctx().get<PhysicsConfig>();
	auto view = registry.view<Position, Velocity>();

	for (auto [entity, pos, vel] : view.each())
	{
		if (registry.all_of<JoltBody>(entity)) continue;  // Jolt handles this

		pos.value += vel.value * config.fixedDeltaTime;
	}
};