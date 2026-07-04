#pragma once

#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include <string>
#include <vector>

// WeaponType is defined in components/combat.h; a Weapon pickup only stores it
// by value, so an opaque declaration keeps this header light.
enum class WeaponType;

// Gameplay/state components: health and damage feedback, movers (doors/lifts),
// trigger volumes, lifetimes, the demo-reset prop helper, and item pickups.

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

// ─── Item pickups ────────────────────────────────────────────────
enum class PickupType
{
	Health,   // restores Health (capped at max)
	Shells,   // Ammo.shells
	Nails,    // Ammo.nails
	Rockets,  // Ammo.rockets
	Cells,    // Ammo.cells
	Armor,    // restores Armor (capped at max)
	Weapon    // grants weaponType if not held; tops up its ammo
};

// A sensor entity that grants an effect to a TagTriggerable toucher, then is
// consumed. pickupSystem does the overlap + grant + destroy.
struct Pickup
{
	PickupType type = PickupType::Health;
	int amount = 0;                              // health/armour/ammo granted
	WeaponType weaponType = static_cast<WeaponType>(0); // used only when type == Weapon
};

// Transient on-screen toast ("Picked up …"). pickupSystem sets text + timer on
// the receiver; the HUD draws it centred and fades it out.
struct PickupMessage
{
	std::string text;
	float timer = 0.0f;    // remaining display time (seconds)
	float duration = 2.5f; // total display length
};

// ─── Enemy AI ────────────────────────────────────────────────────
enum class AIStateKind
{
	Idle,   // standing still (the only state the setup plan produces)
	Chase,  // moving toward the target
	Attack, // in range, attacking on a cooldown
	Dead    // health depleted; awaiting cleanup
};

// Marks an enemy and holds its behaviour state. Created by the setup plan (the
// grunt archetype); the state machine that drives it lands in the behaviour plan.
struct AIState
{
	AIStateKind  state = AIStateKind::Idle;
	float        attackCooldown = 0.0f;      // seconds until the next attack is allowed
	entt::entity target = entt::null;        // who to chase/attack (null = not aggroed)
};

// Enemy navigation path: the waypoints (grid-cell centres) the grunt is walking
// toward its target, plus the follow cursor and a repath timer. Filled by
// aiSystem via A* over the NavGrid.
struct AIPath
{
	std::vector<glm::vec3> waypoints;
	size_t index = 0;           // current waypoint being walked toward
	float  repathTimer = 0.0f;  // counts down; a new path is computed at ≤ 0
};
