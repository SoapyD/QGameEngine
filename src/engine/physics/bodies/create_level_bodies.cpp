#include "engine/physics/jolt_bodies.h"

#include "engine/ecs/components.h"
#include "engine/physics/jolt_world.h"

// Static Jolt bodies for the level surfaces (walls/floors/ceilings). Built from
// surface geometry, not the render mesh.
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
