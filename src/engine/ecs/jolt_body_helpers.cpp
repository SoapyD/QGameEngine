#include "engine/ecs/jolt_body_helpers.h"
#include "engine/level/level.h"
#include "engine/physics/jolt_world.h"

void createLevelBodies(entt::registry& registry, const Level& level)
{
    auto& jolt = registry.ctx().get<JoltWorld>();
    auto& bodyInterface = jolt.getBodyInterface();

    for (const auto& sector : level.sectors)
    {
        for (const auto& surface : sector.surfaces)
        {
            // Compute AABB from the surface vertices
            glm::vec3 surfMin = glm::min(
                glm::min(surface.vertices[0], surface.vertices[1]),
                glm::min(surface.vertices[2], surface.vertices[3])
            );
            glm::vec3 surfMax = glm::max(
                glm::max(surface.vertices[0], surface.vertices[1]),
                glm::max(surface.vertices[2], surface.vertices[3])
            );

            // Fatten thin dimensions (same as old collision system)
            for (int i = 0; i < 3; i++)
            {
                if (surfMax[i] - surfMin[i] < 0.01f)
                {
                    surfMin[i] -= 0.1f;
                    surfMax[i] += 0.1f;
                }
            }

            // Jolt box shape takes half-extents
            glm::vec3 halfExtents = (surfMax - surfMin) * 0.5f;
            glm::vec3 centre = (surfMin + surfMax) * 0.5f;

            JPH::BoxShapeSettings shapeSettings(
                JPH::Vec3(halfExtents.x, halfExtents.y, halfExtents.z)
            );
            shapeSettings.SetEmbedded();

            auto shapeResult = shapeSettings.Create();
            if (!shapeResult.IsValid()) continue;

            JPH::BodyCreationSettings bodySettings(
                shapeResult.Get(),
                JPH::RVec3(centre.x, centre.y, centre.z),
                JPH::Quat::sIdentity(),
                JPH::EMotionType::Static,
                Layers::NON_MOVING
            );

            bodyInterface.CreateAndAddBody(bodySettings, JPH::EActivation::DontActivate);
        }
    }
}

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

void createKinematicBody(entt::registry& registry, entt::entity entity)
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
        JPH::EMotionType::Kinematic,
        Layers::MOVING
    );

    JPH::BodyID bodyId = bodyInterface.CreateAndAddBody(
        bodySettings, JPH::EActivation::Activate
    );

    registry.emplace<JoltBody>(entity, bodyId);
}

void createStaticBody(entt::registry& registry, entt::entity entity)
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
        JPH::EMotionType::Static,
        Layers::NON_MOVING
    );

    JPH::BodyID bodyId = bodyInterface.CreateAndAddBody(
        bodySettings, JPH::EActivation::DontActivate
    );

    registry.emplace<JoltBody>(entity, bodyId);
}

void createSensorBody(entt::registry& registry, entt::entity entity)
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
        JPH::EMotionType::Static,
        Layers::SENSOR
    );
    bodySettings.mIsSensor = true;

    JPH::BodyID bodyId = bodyInterface.CreateAndAddBody(
        bodySettings, JPH::EActivation::DontActivate
    );

    registry.emplace<JoltBody>(entity, bodyId);
}