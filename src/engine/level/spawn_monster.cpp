#include "engine/level/factories.h"

#include "engine/ecs/components.h"   // Position, Health, AIState, Colour, ...

// Enemy grunt archetype. Kept in its own file (factories.cpp is at its size cap).
// A solid coloured box that can be shot (raycastEntities keys off AABBCollider),
// takes damage (applyDamage keys off Health), and blocks the player (via the
// kinematic inner body of the CharacterVirtual built in initEnemyCharacters).
// aiSystem drives its movement.

namespace factories
{
    entt::entity spawnMonsterGrunt(entt::registry& reg, const MeshAssets& a, glm::vec3 pos)
    {
        auto e = reg.create();
        reg.emplace<Position>(e, pos);
        reg.emplace<Rotation>(e, glm::vec3(0.0f));
        reg.emplace<Scale>(e, glm::vec3(0.8f, 1.8f, 0.8f));          // humanoid-ish box
        reg.emplace<AABBCollider>(e, glm::vec3(0.4f, 0.9f, 0.4f), false); // solid: shootable + blocks
        reg.emplace<MeshRenderer>(e, cubeRenderer(a, 0u));           // untextured — coloured below
        reg.emplace<Colour>(e, glm::vec4(0.75f, 0.15f, 0.15f, 1.0f)); // red, via renderSystem albedo
        reg.emplace<Health>(e, 50.0f, 50.0f, 0.0f);
        reg.emplace<DamageFlash>(e, 0.0f, 0.12f);   // brief white blink when shot (renderSystem)
        reg.emplace<AIState>(e);
        reg.emplace<AIPath>(e);
        // No TagTriggerable: player-only volumes (lava, teleporters) must not
        // affect the grunt. It is NOT given PendingKnockback either (kinematic
        // bodies ignore impulses — knockback is deferred to the behaviour plan).
        return e;
    }

    // Ranged variant: the grunt archetype plus a RangedAttack, tinted differently
    // so it reads as a distinct threat. aiSystem keeps it at distance and fires
    // dodgeable bolts; it is otherwise identical (shootable, blocks, dies).
    entt::entity spawnMonsterRanged(entt::registry& reg, const MeshAssets& a, glm::vec3 pos)
    {
        auto e = spawnMonsterGrunt(reg, a, pos);
        reg.emplace<RangedAttack>(e);
        reg.get<Colour>(e).value = glm::vec4(0.35f, 0.35f, 0.85f, 1.0f);  // blue — ranged
        return e;
    }
}
