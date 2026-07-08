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

// Transient HUD event signals (combat/AI → HUD), recomputed each fixed tick by
// hudSignalSystem and read by debugHudSystem. Lives in the registry context so
// the *state* is testable headless — the GL HUD only draws it. Timers count down
// in seconds; a value > 0 means "show this for now".
struct HudSignals
{
	float     crosshairGap    = 2.0f;            // half-gap (px): base + spread + movement + recoil
	float     recoil          = 0.0f;            // kick derived from the current weapon's cooldown
	float     hitMarkerTimer  = 0.0f;            // >0 briefly after a player shot damages an enemy
	float     killMarkerTimer = 0.0f;            // >0 briefly after a player shot kills an enemy
	float     damageDirTimer  = 0.0f;            // >0 briefly after the player takes a hit
	glm::vec2 damageDir       = glm::vec2(0.0f); // world-XZ unit dir toward the last attacker
	bool      lowAmmo         = false;           // current weapon's pool at/under the low threshold
	bool      showDebug       = true;            // gate the FPS/debug text (clean production HUD)

	static constexpr float kMarkerTime    = 0.25f; // hit/kill marker lifetime
	static constexpr float kDamageDirTime = 1.2f;  // damage-direction arc lifetime
};

// Debug-visualisation toggles (stored in registry context). renderSystem reads
// these; the game loop flips them from key input. `showTriggerVolumes` draws the
// otherwise-invisible activate/hurt/teleport zones as green wireframe boxes.
struct DebugRenderConfig
{
	bool showTriggerVolumes = true;
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
