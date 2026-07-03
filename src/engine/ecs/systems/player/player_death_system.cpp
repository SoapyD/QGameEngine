#include "engine/ecs/systems/player/player_death_system.h"
#include "engine/ecs/components.h"
#include "engine/audio/queue_sound.h"

#include "engine/physics/jolt_world.h"
#include "engine/physics/physics_config.h"

#include <Jolt/Physics/Character/CharacterVirtual.h>


void playerDeathSystem(entt::registry& registry)
{
	const auto& config = registry.ctx().get<PhysicsConfig>();
	float dt = config.fixedDeltaTime;

	auto view = registry.view<Health, Position, SpawnPoint, JoltCharacter, TagPlayer>();
	for (auto [entity, health, pos, spawn, joltChar] : view.each())
	{
		// ─── Tick down invulnerability ───────────────────────
		if (health.invulnerableTimer > 0.0f)
		{
			health.invulnerableTimer -= dt;
			if (health.invulnerableTimer < 0.0f)
				health.invulnerableTimer = 0.0f;
		}

		// ─── Check for death ────────────────────────────────
		if (health.current > 0.0f) continue;
		
		// reset health
		health.current = health.max;
		health.invulnerableTimer = 1.0f;  // 1 second of invulnerability

		// move to spawn point
		pos.value = spawn.position;

		queueSound(registry, "player.death");

		// Teleport the Jolt CharacterVirtual to the spawn position
		auto& character = joltChar.character;
		character->SetPosition(JPH::RVec3(spawn.position.x, spawn.position.y, spawn.position.z));
		character->SetLinearVelocity(JPH::Vec3::sZero());
	}
};