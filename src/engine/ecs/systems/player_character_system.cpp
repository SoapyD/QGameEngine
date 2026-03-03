#include "engine/ecs/systems/player_character_system.h"
#include "engine/ecs/components.h"
#include "engine/physics/jolt_world.h"
#include "engine/physics/physics_config.h"

#include <Jolt/Physics/Character/CharacterVirtual.h>

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

void playerCharacterSystem(entt::registry& registry)
{
	auto& config = registry.ctx().get<PhysicsConfig>();
	float dt = config.fixedDeltaTime;
	auto& jolt = registry.ctx().get<JoltWorld>();

	auto view = registry.view<Position, JoltCharacter, PlayerInput, CharacterPhysics, OnGround>();
	for (auto [entity, pos, joltChar, input, physics, ground] : view.each())
	{
		auto& character = joltChar.character;

		// ─── Read ground state from Jolt ────────────────────────
		bool onGround = character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
		ground.value = onGround;

		// ─── Build velocity from input ──────────────────────────		
		JPH::Vec3 currentVel = character->GetLinearVelocity();
		JPH::Vec3 desiredVel(0.0f, 0.0f, 0.0f);

		if (onGround)
		{
			// Ground movement — Quake-style acceleration
			JPH::Vec3 wishDir(input.wishDir.x, 0.0f, input.wishDir.z);
			float wishSpeed = physics.maxGroundSpeed;
		
			if (wishDir.LengthSq() > 0.0f)
			{
				wishDir = wishDir.Normalized();
				float currentSpeed = currentVel.Dot(wishDir);
				float addSpeed = wishSpeed - currentSpeed;
				if (addSpeed > 0.0f)
				{
					float accelSpeed = physics.groundAcceleration * wishSpeed * dt;
					if (accelSpeed > addSpeed) accelSpeed = addSpeed;
					desiredVel = JPH::Vec3(currentVel.GetX(), 0.0f, currentVel.GetZ()) + wishDir * accelSpeed;
				}
				else
				{
					desiredVel = JPH::Vec3(currentVel.GetX(), 0.0f, currentVel.GetZ());
				}
			}
			else
			{
				// no input = apply ground friction
				JPH::Vec3 horizontalVel(currentVel.GetX(), 0.0f, currentVel.GetZ());
				float speed = horizontalVel.Length();
				if (speed > 0.f)
				{
					float drop = speed * physics.groundFriction * dt;
					float newSpeed = std::max(speed - drop, 0.0f);
					desiredVel = horizontalVel * (newSpeed / speed);
				} 
			}

			// jump
			if (input.jump)
			{
				desiredVel += JPH::Vec3(0.0f, physics.jumpForce, 0.0f);
			}
			else
			{
				// Keep ground velocity vertical component
				desiredVel += JPH::Vec3(0.0f, currentVel.GetY(), 0.0f);
			}
		}
		else
		{
			// Air movement — limited air control
			JPH::Vec3 wishDir(input.wishDir.x, 0.0f, input.wishDir.z);

			if (wishDir.LengthSq() > 0.0f)
			{
				wishDir = wishDir.Normalized();
				float currentSpeed = JPH::Vec3(currentVel.GetX(), 0.0f, currentVel.GetZ()).Dot(wishDir);
				float addSpeed = physics.maxAirSpeed - currentSpeed;
				if (addSpeed > 0.0f)
				{
					float accelSpeed = physics.airAcceleration * physics.maxAirSpeed * dt;
					if (accelSpeed > addSpeed) accelSpeed = addSpeed;
					desiredVel = JPH::Vec3(currentVel.GetX(), currentVel.GetY(), currentVel.GetZ()) + wishDir * accelSpeed;
				}
				else
				{
					desiredVel = JPH::Vec3(currentVel.GetX(), currentVel.GetY(), currentVel.GetZ());
				}
			}
			else
			{
				desiredVel = JPH::Vec3(currentVel.GetX(), currentVel.GetY(), currentVel.GetZ());
			}

			// apply gravity while in the air
			desiredVel += JPH::Vec3(0.0f, -20.0f * dt, 0.0f);
		}

		// ─── Apply pending knockback ────────────────────────
		if (registry.all_of<PendingKnockback>(entity))
		{
			auto& kb = registry.get<PendingKnockback>(entity);
			if (kb.impulse.x != 0.0f || kb.impulse.y != 0.0f || kb.impulse.z != 0.0f)
			{
				desiredVel += JPH::Vec3(kb.impulse.x, kb.impulse.y, kb.impulse.z);
				kb.impulse = glm::vec3(0.0f);  // consumed
			}
		}

		character->SetLinearVelocity(desiredVel);

        // ─── Step the character ─────────────────────────────────
        // ExtendedUpdate handles collision, stair stepping, and floor sticking

		JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;
		updateSettings.mStickToFloorStepDown = JPH::Vec3(0.0f, -physics.stepHeight, 0.0f);
		updateSettings.mWalkStairsStepUp = JPH::Vec3(0.0f, physics.stepHeight, 0.0f);

		character->ExtendedUpdate
		(
			dt,
			-character->GetUp() * jolt.physicsSystem->GetGravity().Length(),
			updateSettings,
			jolt.physicsSystem->GetDefaultBroadPhaseLayerFilter(Layers::MOVING),
			jolt.physicsSystem->GetDefaultLayerFilter(Layers::MOVING),
			{}, // body filter
			{}, // shape filter
			*jolt.tempAllocator
		);

		// ─── Write position back to ECS ─────────────────────────
		JPH::RVec3 charPos = character->GetPosition();
		pos.value = glm::vec3(charPos.GetX(), charPos.GetY(), charPos.GetZ());
	}

}