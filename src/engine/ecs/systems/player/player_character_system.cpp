#include "engine/ecs/systems/player/player_character_system.h"
#include "engine/ecs/components.h"
#include "engine/audio/queue_sound.h"
#include "engine/physics/jolt_world.h"
#include "engine/physics/physics_config.h"

#include <Jolt/Physics/Character/CharacterVirtual.h>

// initPlayerCharacter lives in init_player_character.cpp (one-time setup).

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

		// Velocity of the surface underfoot — non-zero when standing on a
		// moving kinematic platform (lift/door). Used to carry the player.
		JPH::Vec3 groundVel = character->GetGroundVelocity();

		if (onGround)
		{
			// Ground movement — Quake-style acceleration, computed in the ground's
			// reference frame so a moving platform's velocity is inherited exactly
			// once (currentVel already carries last tick's groundHoriz — adding it
			// again each tick is what made speed run away on horizontal movers).
			JPH::Vec3 groundHoriz(groundVel.GetX(), 0.0f, groundVel.GetZ());
			JPH::Vec3 relVel = JPH::Vec3(currentVel.GetX(), 0.0f, currentVel.GetZ()) - groundHoriz;
			JPH::Vec3 wishDir(input.wishDir.x, 0.0f, input.wishDir.z);
			JPH::Vec3 moveVel = relVel;

			if (wishDir.LengthSq() > 0.0f)
			{
				wishDir = wishDir.Normalized();
				float currentSpeed = relVel.Dot(wishDir);
				float addSpeed = physics.maxGroundSpeed - currentSpeed;
				if (addSpeed > 0.0f)
				{
					float accelSpeed = physics.groundAcceleration * physics.maxGroundSpeed * dt;
					if (accelSpeed > addSpeed) accelSpeed = addSpeed;
					moveVel = relVel + wishDir * accelSpeed;
				}
			}
			else
			{
				// no input = apply ground friction (to the player's own velocity)
				float speed = relVel.Length();
				if (speed > 0.0f)
				{
					float drop = speed * physics.groundFriction * dt;
					float newSpeed = std::max(speed - drop, 0.0f);
					moveVel = relVel * (newSpeed / speed);
				}
				else
				{
					moveVel = JPH::Vec3::sZero();
				}
			}

			// Anti-runaway: clamp the player's OWN horizontal speed. Platform
			// carry is added afterward, so riding a fast platform is never clamped.
			float ownSpeed = moveVel.Length();
			if (ownSpeed > physics.maxHorizontalSpeed)
				moveVel = moveVel * (physics.maxHorizontalSpeed / ownSpeed);

			desiredVel = moveVel + groundHoriz;   // inherit the platform once

			if (input.jump)
			{
				desiredVel.SetY(physics.jumpForce);
				queueSound(registry, "player.jump");
			}
			else
			{
				// Ride the platform's vertical motion (0 on static ground).
				desiredVel.SetY(groundVel.GetY());
			}
		}
		else
		{
			// Air movement — limited air control
			JPH::Vec3 wishDir(input.wishDir.x, 0.0f, input.wishDir.z);
			desiredVel = JPH::Vec3(currentVel.GetX(), currentVel.GetY(), currentVel.GetZ());

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
			}

			// Anti-runaway: clamp horizontal air speed too.
			JPH::Vec3 airHoriz(desiredVel.GetX(), 0.0f, desiredVel.GetZ());
			float airSpeed = airHoriz.Length();
			if (airSpeed > physics.maxHorizontalSpeed)
			{
				airHoriz = airHoriz * (physics.maxHorizontalSpeed / airSpeed);
				desiredVel = JPH::Vec3(airHoriz.GetX(), desiredVel.GetY(), airHoriz.GetZ());
			}

			// apply gravity while in the air (shared magnitude from PhysicsConfig)
			desiredVel += JPH::Vec3(0.0f, -config.gravity * dt, 0.0f);
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