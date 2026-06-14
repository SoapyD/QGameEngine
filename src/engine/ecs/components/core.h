#pragma once

#include <glm/glm.hpp>

// Core engine components: per-frame config/context, player input, and spatial
// transforms. Included via "engine/ecs/components.h".

// ─── Config / Context ────────────────────────────────────────────

// Config for the debug HUD overlay (stored in registry context)
struct HudConfig
{
	unsigned int shaderId = 0;
};

// Player's view/aim direction, published to the registry context each frame
// (combatSystem reads it for firing). A named type avoids the fragile bare
// glm::vec3-in-context coupling.
struct CameraDirection
{
	glm::vec3 value = glm::vec3(0.0f, 0.0f, -1.0f);
};

// Input state for the player — set each frame from InputManager
struct PlayerInput
{
	bool fire = false;
	int weaponSwitch = -1; // -1 = no switch, 0+ = weapon slot
	// movement
	glm::vec3 wishDir = glm::vec3(0.0f);  // desired move direction (normalised)
	bool jump = false;
};

// ─── Spatial Components ──────────────────────────────────────────

struct Position {
	glm::vec3 value = glm::vec3(0.0f);
};

// Position at the start of the previous fixed tick. The renderer lerps
// between this and Position by the fixed-timestep alpha so motion is smooth
// at frame rates above the 60 Hz tick rate.
struct PrevPosition {
	glm::vec3 value = glm::vec3(0.0f);
};

// Fraction of the collider half-height the camera/eye sits above centre.
inline constexpr float kEyeHeightFraction = 0.7f;

struct Rotation {
	glm::vec3 euler = glm::vec3(0.0f); // pitch, yaw, roll in degrees
};

struct Scale {
	glm::vec3 value = glm::vec3(1.0f);
};

struct Velocity {
	glm::vec3 value = glm::vec3(0.0f);
};

struct Vertex {
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 normal = glm::vec3(0.0f);      // For lighting (Chapter 7)
    glm::vec2 texCoords = glm::vec2(0.0f);
};
