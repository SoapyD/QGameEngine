#pragma once

struct PhysicsConfig
{
	// Maximum fall speed (units per second, positive value).
	// Currently a magic number (-50.0f) inside physicsSystem.
	float terminalVelocity = 50.0f;

	// Fixed physics timestep (seconds per tick).
	// Stored here so systems can read it from context instead of
	// receiving it as a parameter.
	float fixedDeltaTime = 1.0f / 60.0f;
};