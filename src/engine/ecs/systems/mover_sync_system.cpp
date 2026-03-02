#include "engine/ecs/systems/mover_sync_system.h"
#include "engine/ecs/components.h"
#include "engine/physics/jolt_world.h"
#include "engine/physics/physics_config.h"


void moverSyncSystem(entt::registry& registry)
{
	const auto& config = registry.ctx().get<PhysicsConfig>();
	auto& jolt = registry.ctx().get<JoltWorld>();
	auto& bodyInterface = jolt.getBodyInterface();
	
	auto view = registry.view<Position, Mover, JoltBody>();
	for (auto [entity, pos, mover, joltBody] : view.each())
	{
		// Move the kinematic body to match the mover's current position
		bodyInterface.MoveKinematic
		(
			joltBody.id,
			JPH::RVec3(pos.value.x, pos.value.y, pos.value.z),
			JPH::Quat::sIdentity(),
			config.fixedDeltaTime // Jolt uses this to compute velocity
		);
	}
}