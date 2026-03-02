#include "engine/ecs/systems/player_movement_system.h"
#include "engine/ecs/components.h"
#include "engine/physics/physics_config.h"

void playerMovementSystem(entt::registry& registry)
{
	const auto& config = registry.ctx().get<PhysicsConfig>();
	float dt = config.fixedDeltaTime;

	auto view = registry.view<PlayerInput, Velocity, OnGround, CharacterPhysics>();

	for (auto [entity, input, vel, ground, phys] : view.each())
	{
		// ─── Jumping ─────────────────────────────────────────
		if (input.jump && ground.value)
		{
			vel.value.y = phys.jumpForce;
			ground.value = false; // leave the ground immediately
		}

		// ─── Horizontal acceleration (Quake-style) ──────────
		glm::vec3 wishDir = input.wishDir;
		float wishSpeed;
		float accel;

		if (ground.value)
		{
			wishSpeed = phys.maxGroundSpeed;
			accel = phys.groundAcceleration;
		}
		else
		{
			wishSpeed = phys.maxAirSpeed;
			accel = phys.airAcceleration;
		}

		if (glm::length(wishDir) < 0.01f) continue; // no input, let friction handle deceleration

		float currentSpeed = glm::dot
		(
			glm::vec3(vel.value.x, 0.0f, vel.value.z),
			wishDir
		);
		float addSpeed = wishSpeed - currentSpeed;

		if (addSpeed <= 0.0f) continue; // already at or above wish speed in this direction

		float accelSpeed = accel * wishSpeed * dt;
		if (accelSpeed > addSpeed)
		{
			accelSpeed = addSpeed;
		}

		vel.value.x += wishDir.x * accelSpeed;
		vel.value.z += wishDir.z * accelSpeed;
	}
}
