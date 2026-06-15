#include "engine/physics/jolt_bodies.h"

#include "engine/ecs/components.h"
#include "engine/physics/jolt_world.h"

void createDynamicBody(entt::registry& registry, entt::entity entity)
{
    auto& jolt = registry.ctx().get<JoltWorld>();
    auto& bodyInterface = jolt.getBodyInterface();
    auto& pos = registry.get<Position>(entity);
    auto& col = registry.get<AABBCollider>(entity);

    JPH::BoxShapeSettings shapeSettings(
        JPH::Vec3(col.halfExtents.x, col.halfExtents.y, col.halfExtents.z)
    );
    shapeSettings.SetEmbedded();

    auto shapeResult = shapeSettings.Create();
    if (!shapeResult.IsValid()) return;  // half-extent below convex radius → skip rather than crash

    JPH::BodyCreationSettings bodySettings(
        shapeResult.Get(),
        JPH::RVec3(pos.value.x, pos.value.y, pos.value.z),
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Dynamic,
        Layers::MOVING
    );

    // Match our gravity strength
    bodySettings.mGravityFactor = 1.0f;

    // Set initial velocity if the entity has one
    if (registry.all_of<Velocity>(entity))
    {
        auto& vel = registry.get<Velocity>(entity);
        bodySettings.mLinearVelocity = JPH::Vec3(vel.value.x, vel.value.y, vel.value.z);
    }

    JPH::BodyID bodyId = bodyInterface.CreateAndAddBody(
        bodySettings, JPH::EActivation::Activate
    );

    registry.emplace<JoltBody>(entity, bodyId);
}
