#pragma once

#include <glm/glm.hpp>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>

#include "engine/physics/collision_layers.h"

// Physics-facing components: links to Jolt bodies/characters, colliders, ground
// state, movement tuning, and respawn points.

struct JoltBody
{
    JPH::BodyID id;
};

struct JoltCharacter
{
	JPH::Ref<JPH::CharacterVirtual> character;
};

struct AABBCollider
{
	glm::vec3 halfExtents = glm::vec3(0.5f);
	bool isTrigger = false; // triggers detect overlap but don't block
	// NOTE: `layer`/`mask` are inert under Jolt (it uses its own ObjectLayer
	// system) but `collision_layers.h` is still a LIVE include — these defaults
	// reference CollisionLayers. See docs/processes/physics.md.
	uint32_t layer = CollisionLayers::World; //what layer am I on?
	uint32_t mask = CollisionLayers::All; // what layers do I collide with
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
	float stepHeight = 0.7f; // Max height of a step the player can walk up
	// Anti-runaway ceiling on the player's OWN horizontal speed (excludes platform
	// carry). Generous so bunny-hopping still feels fast; catches pathological
	// speed-ups (e.g. being carried/shoved by a moving kinematic body).
	float maxHorizontalSpeed = 20.0f;
};

// ─── Spawn / Respawn ─────────────────────────────────────────
struct SpawnPoint
{
	glm::vec3 position = glm::vec3(0.0f);
	float yaw = 0.0f;  // facing direction on respawn (degrees)
};
