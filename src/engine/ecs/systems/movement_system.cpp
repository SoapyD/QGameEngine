#include "engine/ecs/systems/movement_system.h"
#include "engine/ecs/components.h"

void movementSystem(entt::registry& registry, float dt)
{
	auto view = registry.view<Position, Velocity>();

	for (auto [entity, pos, vel] : view.each())
	{
		pos.value += vel.value * dt;
	}
};