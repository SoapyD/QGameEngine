#include "engine/ecs/systems/enemy/init_enemy_characters.h"

#include "engine/ecs/components.h"
#include "engine/physics/jolt_world.h"
#include "engine/physics/jolt_setup.h"   // Layers::MOVING

#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>

// See init_player_character.cpp — this is the enemy analogue. The one difference
// is the INNER BODY: the player's CharacterVirtual has none (nothing needs to
// collide WITH the player), but enemies must block the player and each other, so
// each enemy character gets a kinematic inner body on the MOVING layer (the same
// layer the old standalone kinematic enemy body used).
void initEnemyCharacters(entt::registry& registry)
{
    auto& jolt = registry.ctx().get<JoltWorld>();

    auto view = registry.view<Position, AABBCollider, AIState>(entt::exclude<JoltCharacter>);
    for (auto [entity, pos, col, ai] : view.each())
    {
        (void)ai;   // view filter only — enemies are the AIState entities
        // Capsule from the collider's half-extents (radius + the two caps).
        float radius     = col.halfExtents.x;
        float halfHeight = col.halfExtents.y - radius;
        if (halfHeight < 0.01f) halfHeight = 0.01f;

        JPH::Ref<JPH::Shape> capsule = new JPH::CapsuleShape(halfHeight, radius);

        JPH::Ref<JPH::CharacterVirtualSettings> settings = new JPH::CharacterVirtualSettings();
        settings->mShape                 = capsule;
        settings->mMaxSlopeAngle         = JPH::DegreesToRadians(50.0f);
        settings->mMaxStrength           = 100.0f;
        settings->mMass                  = 70.0f;
        settings->mPredictiveContactDistance = 0.1f;
        settings->mInnerBodyShape        = capsule;         // physical presence...
        settings->mInnerBodyLayer        = Layers::MOVING;  // ...that blocks the player

        JPH::Ref<JPH::CharacterVirtual> character = new JPH::CharacterVirtual(
            settings,
            JPH::RVec3(pos.value.x, pos.value.y, pos.value.z),
            JPH::Quat::sIdentity(),
            0,
            jolt.physicsSystem.get());

        registry.emplace<JoltCharacter>(entity, character);
    }
}
