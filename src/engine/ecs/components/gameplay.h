#pragma once

#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include <string>

// Gameplay/state components: health and damage feedback, movers (doors/lifts),
// trigger volumes, lifetimes, and the demo-reset prop helper.

struct Health
{
	float current;
	float max;
	float invulnerableTimer = 0.0f; // seconds of remaining invulnerability
};

struct DamageFlash
{
	float timer = 0.0f; // remaining flash time
	float duration = 0.3f; // total flash length (seconds)
};

struct PendingKnockback
{
	glm::vec3 impulse = glm::vec3(0.0f);
};

enum class MoverState
{
	Idle, // at start position
	StartDelay, // triggered, waiting before moving
	Moving, // moving to end position
	Waiting, // at end position, waiting before returning
	Returning // moving back to start position
};

struct Mover {
	glm::vec3 startPos; // where it starts (closed position)
	glm::vec3 endPos; // where it ends (open position)
	float speed = 2.0f; // units per second
	float waitTime = 3.0f; // seconds to stay open
	float startDelay = 0.0f; // delay before movement begins
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

// Auto-destroy after a duration (projectiles, tracers, effects)
struct Lifetime
{
	float remaining = 5.0f;
};

// ─── Showcase ────────────────────────────────────────────────────
struct DemoReset
{
	glm::vec3 startPosition;
	glm::vec3 startVelocity = glm::vec3(0.0f);
	float interval = 5.0f; // seconds between resets
	float timer = 0.0f; // counts up each ticket
};
