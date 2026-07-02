#include "engine/ecs/systems/demo/demo_reset_system.h"
#include "engine/ecs/components.h"
#include "engine/physics/physics_config.h"
#include "engine/physics/jolt_world.h"

void demoResetSystem(entt::registry& registry)
{
	const auto& config = registry.ctx().get<PhysicsConfig>();

	auto view = registry.view<Position, Velocity, DemoReset>();

	for (auto [entity, pos, vel, demo] : view.each())
	{
		demo.timer += config.fixedDeltaTime;

		if (demo.timer >= demo.interval)
		{
			pos.value = demo.startPosition;
			vel.value = demo.startVelocity;
			demo.timer = 0.0f;

			// reset ground state so gravity applies immediately
			if (registry.all_of<OnGround>(entity))
			{
				registry.get<OnGround>(entity).value = false;
			}

			// teleport the Jolt body back to the start position
			if (registry.all_of<JoltBody>(entity))
			{
				auto& jolt = registry.ctx().get<JoltWorld>();
				auto& body = registry.get<JoltBody>(entity);
				auto& bi = jolt.getBodyInterface();
				bi.SetPosition(body.id,
					JPH::RVec3(demo.startPosition.x, demo.startPosition.y, demo.startPosition.z),
					JPH::EActivation::Activate);
				bi.SetLinearVelocity(body.id,
					JPH::Vec3(demo.startVelocity.x, demo.startVelocity.y, demo.startVelocity.z));
			}
		}
	}
}