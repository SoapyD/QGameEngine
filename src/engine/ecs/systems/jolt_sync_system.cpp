
#include "engine/ecs/systems/jolt_sync_system.h"
#include "engine/ecs/components.h"
#include "engine/physics/jolt_world.h"


void joltSyncSystem(entt::registry& registry)
{
	auto& jolt = registry.ctx().get<JoltWorld>();
	auto& bodyInterface = jolt.getBodyInterface();

	auto view = registry.view<Position, JoltBody>();
	for (auto [entity, pos, joltBody] : view.each())
	{
		// read position from Jolt
		JPH::RVec3 joltPos = bodyInterface.GetCenterOfMassPosition(joltBody.id);
		pos.value = glm::vec3(joltPos.GetX(), joltPos.GetY(), joltPos.GetZ());

		// update ground state if the entity has one
		if (registry.all_of<OnGround>(entity))
		{
			auto& ground = registry.get<OnGround>(entity);
         	// A body is "on ground" if it has very low vertical velocity
            // and is not in free-fall. This is a simple heuristic —
            // Chapter 15 replaces this with CharacterVirtual's ground detection.
			JPH::Vec3 joltVel = bodyInterface.GetLinearVelocity(joltBody.id);
			ground.value = std::abs(joltVel.GetY()) < 0.5f;
		}
	}
}