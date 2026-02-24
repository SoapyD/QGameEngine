#pragma once

#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include "engine/physics/collision_layers.h"

// ─── Spatial Components ──────────────────────────────────────────

struct Position {
	glm::vec3 value = glm::vec3(0.0f);
};

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

// ─── Physics Components ──────────────────────────────────────────
struct AABBCollider
{
	glm::vec3 halfExtents = glm::vec3(0.5f);
	bool isTrigger = false; // triggers detect overlap but don't block
	uint32_t layer = CollisionLayers::World; //what layer am I on?
	uint32_t mask = CollisionLayers::All; // what layers do I collide with
};

struct Gravity
{
	float strength = 20.0f; // quake uses ~800 units/s² (our scale is smaller)
};

struct OnGround
{
	bool value = false;
};

struct CharacterPhysics
{
	float groundFriction = 6.0f;
	float airFriction = 0.1f;
	float maxGroundSpeed = 7.0f;
	float maxAirSpeed = 1.0f; // Quake's air speed cap (enables bunny hopping!)
	float groundAcceleration = 10.0f;
	float airAcceleration = 10.0f;
	float jumpForce = 8.0f;
	float stepHeight = 0.5f; // Max height of a step the player can walk up
};

// ─── State Components ────────────────────────────────────────────

struct Health
{
	float current;
	float max;
};

enum class MoverState
{
	Idle, // at start position
	Moving, // moving to end position
	Waiting, // at end position, waiting before returning
	Returning // moving back to start position
};

struct Mover {
	glm::vec3 startPos; // where it starts (closed position)
	glm::vec3 endPos; // where it ends (open position)
	float speed = 2.0f; // units per second
	float waitTime = 3.0f; // seconds to stay open
	float timer = 0.0f; // current time
	float progress = 0.0f; //0.0 = start, 1.0 = end
	MoverState state = MoverState::Idle;
	bool requiresTrigger = true; // must be triggered to start
};

enum class TriggerAction
{
    ActivateMover,     // Open a door, start a lift
    Teleport,          // Move the player somewhere
    Damage,            // Hurt the player (lava, spikes)
    Heal,              // Heal zone
    ChangeLevel,       // Load next level
    Message            // Display text to the player
};

struct TriggerVolume {
    TriggerAction action = TriggerAction::ActivateMover;
    entt::entity target = entt::null;  // Entity to activate (for ActivateMover)
    glm::vec3 destination;              // For teleport
    float value = 0.0f;                 // Damage/heal amount
    std::string message;                // For Message action
    bool onlyOnce = false;              // Fire once then disable
    bool triggered = false;             // Has been triggered (for onlyOnce)
    float cooldown = 0.0f;             // Minimum time between triggers
    float cooldownTimer = 0.0f;
};

// ─── Lighting Components ─────────────────────────────────────────

struct DirectionalLight {
	glm::vec3 direction = glm::vec3(-0.2f, -1.0f, -0.3f);
	glm::vec3 color = glm::vec3(1.0f);
	float ambientStrength = 0.1f;
};

struct PointLight {
	glm::vec3 color = glm::vec3(1.0f);
	float ambientStrength = 0.05f;
	float linear = 0.09f;
	float quadratic = 0.032f;	
};

// ─── Rendering Components ────────────────────────────────────────

struct MeshRenderer {
	unsigned int vao = 0; // Vertex Array Object handle
	unsigned int vertexCount = 0; // Number of vertices to draw
	unsigned int shaderId = 0; // Shader program to use
	unsigned int textureId = 0; // 0 means no texture
	bool useIndices = false; //for index drawing
	unsigned int indexCount = 0; // number of indices
};

struct Colour {
	glm::vec4 value = glm::vec4(1.0f); // RGBA
};

// ─── Showcase Components ─────────────────────────────────────────
struct DemoReset
{
	glm::vec3 startPosition;
	glm::vec3 startVelocity = glm::vec3(0.0f);
	float interval = 5.0f; // seconds between resets
	float timer = 0.0f; // counts up each ticket
};


// ─── Tags ────────────────────────────────────────────────────────
// Tags are empty structs — they mark entities without adding data

struct TagPlayer {};
struct TagDebugWireframe {};