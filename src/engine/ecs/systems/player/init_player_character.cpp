#include "engine/ecs/systems/player/player_character_system.h"
#include "engine/ecs/components.h"
#include "engine/physics/jolt_world.h"

#include <Jolt/Physics/Character/CharacterVirtual.h>

// One-time setup: build the player's CharacterVirtual from its AABBCollider.
// Call once after scene setup, before the first playerCharacterSystem tick.
void initPlayerCharacter(entt::registry& registry)
{
	auto& jolt = registry.ctx().get<JoltWorld>();

	auto view = registry.view<Position, AABBCollider, TagPlayer>();
	for (auto [entity, pos, col] : view.each())
	{
        // Create a capsule shape for the player
        // Capsule height = total height minus the two hemisphere caps
		float radius = col.halfExtents.x; // 0.3
		float halfHeight = col.halfExtents.y - radius;	// 0.85 - 0.3 = 0.55
		if (halfHeight < 0.01f) halfHeight = 0.01f;

		JPH::Ref<JPH::Shape> capsuleShape = new JPH::CapsuleShape(halfHeight, radius);

		// Configure the character
		JPH::Ref<JPH::CharacterVirtualSettings> settings = new JPH::CharacterVirtualSettings();
		settings->mShape = capsuleShape;
		settings->mMaxSlopeAngle = JPH::DegreesToRadians(50.0f);
		settings->mMaxStrength = 100.0f;
		settings->mMass = 70.0f;
		settings->mPredictiveContactDistance = 0.1f;

		// create the character at the entity's current position
		JPH::Ref<JPH::CharacterVirtual> character = new JPH::CharacterVirtual
		(
			settings,
			JPH::RVec3(pos.value.x, pos.value.y, pos.value.z),
			JPH::Quat::sIdentity(),
			0, // user data
			jolt.physicsSystem.get()
		);
		registry.emplace<JoltCharacter>(entity, character);

	}
}
