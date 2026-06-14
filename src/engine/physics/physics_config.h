#pragma once

struct PhysicsConfig
{
	// Downward gravity acceleration (units/s^2, positive magnitude). Single
	// source of truth: JoltWorld::init() feeds it to SetGravity, and
	// playerCharacterSystem uses it for the manual airborne gravity step, so
	// the two can no longer silently disagree.
	float gravity = 20.0f;

	// Maximum fall speed (units per second, positive value).
	// Only read by the archived physics_system (pre-Jolt); Jolt handles
	// fall speed itself. Kept for that legacy reference.
	float terminalVelocity = 50.0f;

	// Fixed physics timestep (seconds per tick).
	// Stored here so systems can read it from context instead of
	// receiving it as a parameter.
	float fixedDeltaTime = 1.0f / 60.0f;
};