#include "engine/ecs/systems/enemy/ai_support.h"

#include "engine/physics/jolt_world.h"
#include "engine/physics/jolt_setup.h"   // Layers::MOVING

#include <Jolt/Physics/Character/CharacterVirtual.h>

namespace
{
    constexpr float kStepHeight = 0.5f;   // seam/step the enemy walks up (like the player)
}

// Step an enemy's CharacterVirtual one tick with a desired horizontal velocity,
// letting gravity + stick-to-floor keep it grounded. Collided locomotion — this
// is what stops the follower clipping walls on a corner-cut (the whole point).
void aiStepCharacter(JPH::CharacterVirtual* ch, glm::vec3 horiz, float dt, JoltWorld& jolt)
{
    bool onGround = ch->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
    float vy = onGround ? 0.0f
                        : ch->GetLinearVelocity().GetY() - jolt.physicsSystem->GetGravity().Length() * dt;
    ch->SetLinearVelocity(JPH::Vec3(horiz.x, vy, horiz.z));

    JPH::CharacterVirtual::ExtendedUpdateSettings us;
    us.mStickToFloorStepDown = JPH::Vec3(0.0f, -kStepHeight, 0.0f);
    us.mWalkStairsStepUp     = JPH::Vec3(0.0f,  kStepHeight, 0.0f);
    ch->ExtendedUpdate(dt,
        -ch->GetUp() * jolt.physicsSystem->GetGravity().Length(),
        us,
        jolt.physicsSystem->GetDefaultBroadPhaseLayerFilter(Layers::MOVING),
        jolt.physicsSystem->GetDefaultLayerFilter(Layers::MOVING),
        {}, {}, *jolt.tempAllocator);
}
