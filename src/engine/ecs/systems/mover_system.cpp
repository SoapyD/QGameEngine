#include "engine/ecs/systems/movement_system.h"
#include "engine/ecs/components.h"
#include "engine/physics/physics_config.h"


void moverSystem(entt::registry& registry)
{
	const auto& config = registry.ctx().get<PhysicsConfig>();
	float dt = config.fixedDeltaTime;
	auto view = registry.view<Position, Mover>();

	for (auto [entity, pos, mover] :view.each())
	{
		switch(mover.state)
		{
			case MoverState::Idle:
			// sitting at start position, waiting to be triggered
			break;

			case MoverState::StartDelay:
			{
				// triggered, counting down before moving
				mover.timer -= dt;
				if (mover.timer <= 0.0f)
				{
					mover.state = MoverState::Moving;
				}
				break;
			}

			case MoverState::Moving:
			{
				// move toward end position
				float distance = glm::length(mover.endPos - mover.startPos);
				float step = (mover.speed / distance) * dt;
				mover.progress += step;
				
				if (mover.progress >= 1.0f)
				{
					mover.progress = 1.0f;
					mover.state = MoverState::Waiting;
					mover.timer = mover.waitTime;
				}

				// Interpolate position
				pos.value = glm::mix(mover.startPos, mover.endPos, mover.progress);
				
				break;
			}

			case MoverState::Waiting:
			{
				// at end position, counting down
				mover.timer -= dt;

				if (mover.timer <= 0.0f)
				{
					mover.state = MoverState::Returning;
				}

				break;
			}

			case MoverState::Returning:
			{
				// move back to start
				float distance = glm::length(mover.endPos - mover.startPos);
				float step = (mover.speed / distance) * dt;
				mover.progress -= step;

				if(mover.progress <= 0.0f)
				{
					mover.progress = 0.0f;
					mover.state = MoverState::Idle;
				}

				pos.value = glm::mix(mover.startPos, mover.endPos, mover.progress);
				break;
			}
		}
	}
};