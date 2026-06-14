#include "engine/ecs/systems/trigger_system.h"
#include "engine/ecs/components.h"
#include "engine/physics/aabb.h"
#include "engine/physics/physics_config.h"

void triggerSystem(entt::registry& registry)
{
	const auto& config = registry.ctx().get<PhysicsConfig>();
	float dt = config.fixedDeltaTime;
	auto triggerView = registry.view<Position, AABBCollider, TriggerVolume>();

	for (auto [trigEntity, trigPos, trigCol, trigger] : triggerView.each())
	{
		// skip disabled triggers
		if (trigger.onlyOnce && trigger.triggered) continue;

		// cooldown
		if (trigger.cooldownTimer > 0.0f)
		{
			trigger.cooldownTimer -= dt;
			continue;
		}

		AABB triggerBox = AABB::fromCentreSize(trigPos.value, trigCol.halfExtents);

		// Check against all triggerable entities (currently just the player,
		// but enemies/props can opt in via TagTriggerable without changing this).
		auto entityView = registry.view<Position, AABBCollider, TagTriggerable>();

		for (auto [entity, entPos, entCol] : entityView.each())
		{

			AABB entityBox = AABB::fromCentreSize(entPos.value, entCol.halfExtents);

			if (!triggerBox.intersects(entityBox)) continue;

			// ─── Trigger activated! ──────────────────────────────
			switch(trigger.action)
			{
				case TriggerAction::ActivateMover: 
				{
					if (trigger.target != entt::null &&
						registry.valid(trigger.target) &&
						registry.all_of<Mover>(trigger.target)
					)
					{
						auto& mover = registry.get<Mover>(trigger.target);
						if (mover.state == MoverState::Idle)
						{
							if (mover.startDelay > 0.0f)
							{
								mover.state = MoverState::StartDelay;
								mover.timer = mover.startDelay;
							}
							else
							{
								mover.state = MoverState::Moving;
							}
						}
					}
					break;
				}

				case TriggerAction::Teleport:
				{
					entPos.value = trigger.destination;
					// The player is a CharacterVirtual whose position is
					// authoritative — playerCharacterSystem rewrites Position
					// from it every tick, so setting Position alone is undone
					// next tick. Move the character itself (as playerDeathSystem
					// does) or the teleport never takes effect.
					if (registry.all_of<JoltCharacter>(entity))
					{
						auto& character = registry.get<JoltCharacter>(entity).character;
						character->SetPosition(JPH::RVec3(
							trigger.destination.x, trigger.destination.y, trigger.destination.z));
						character->SetLinearVelocity(JPH::Vec3::sZero());
					}
					// reset velocity to prevent flying out of the teleporter
					if (registry.all_of<Velocity>(entity))
					{
						registry.get<Velocity>(entity).value = glm::vec3(0.0f);
					}
					break;
				}

				case TriggerAction::Damage:
				{
					if (registry.all_of<Health>(entity))
					{
						auto& health = registry.get<Health>(entity);
						if (health.invulnerableTimer <= 0.0f)
						{
							float before = health.current;
							health.current -= trigger.value * dt;
							if (health.current < 0.0f) health.current = 0.0f;
						
							// trigger damage flash if health actually decreated
							if (health.current < before && registry.all_of<DamageFlash>(entity))
							{
								auto& flash = registry.get<DamageFlash>(entity);
								flash.timer = flash.duration;
							}

							// Knockback: push player upward out of lava
							if (health.current < before && registry.all_of<PendingKnockback>(entity))
							{
								registry.get<PendingKnockback>(entity).impulse += glm::vec3(0.0f, 1.0f, 0.0f);
							}
						}

					}
					break;
				}

				case TriggerAction::Heal:
				{
					if (registry.all_of<Health>(entity))
					{
						auto& health = registry.get<Health>(entity);
						health.current = std::min
						(
							health.current + trigger.value * dt, health.max
						);
					}
					break;
				}

				case TriggerAction::ChangeLevel:
				{
                    // Set a flag or event — the game layer handles level loading
                    // We won't implement this here yet
                    break;
				}

				case TriggerAction::Message:
				{
					// todo: display trigger.message on HUD (chapter 15)
					break;
				}
			}

			// mark as triggered and start cooldown
			trigger.triggered = true;
			trigger.cooldownTimer = trigger.cooldown;

			if (trigger.onlyOnce) break;
		}
	}
};