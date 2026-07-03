#include "engine/ecs/systems/enemy/enemy_death_system.h"

#include "engine/ecs/components.h"
#include "engine/physics/jolt_world.h"
#include "engine/physics/physics_config.h"
#include "engine/audio/queue_sound.h"

#include <algorithm>
#include <vector>

void enemyDeathSystem(entt::registry& registry)
{
    const float dt = registry.ctx().get<PhysicsConfig>().fixedDeltaTime;

    // One pass over the enemies: fade hit-flashes and collect the dead.
    std::vector<entt::entity> dead;
    for (auto [entity, ai] : registry.view<AIState>().each())
    {
        if (DamageFlash* flash = registry.try_get<DamageFlash>(entity); flash && flash->timer > 0.0f)
            flash->timer = std::max(0.0f, flash->timer - dt);

        if (Health* health = registry.try_get<Health>(entity); health && health->current <= 0.0f)
            dead.push_back(entity);
    }

    if (dead.empty()) return;

    // Destroy dead enemies: pop a sound, remove the Jolt body, drop the entity.
    auto& bodyInterface = registry.ctx().get<JoltWorld>().getBodyInterface();
    for (entt::entity e : dead)
    {
        queueSoundAt(registry, "combat.explosion_small", registry.get<Position>(e).value);
        if (const JoltBody* body = registry.try_get<JoltBody>(e))
        {
            bodyInterface.RemoveBody(body->id);
            bodyInterface.DestroyBody(body->id);
        }
        registry.destroy(e);
    }
}
