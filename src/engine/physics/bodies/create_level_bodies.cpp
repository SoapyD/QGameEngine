#include "engine/physics/jolt_bodies.h"

#include "engine/ecs/components.h"
#include "engine/physics/jolt_world.h"

#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>

namespace
{
    // A static body from a Jolt shape at the origin (level geometry lives in world
    // space, so no offset transform is needed).
    void addStaticBody(JPH::BodyInterface& bi, JPH::ShapeRefC shape)
    {
        JPH::BodyCreationSettings settings(
            shape,
            JPH::RVec3::sZero(),
            JPH::Quat::sIdentity(),
            JPH::EMotionType::Static,
            Layers::NON_MOVING);
        bi.CreateAndAddBody(settings, JPH::EActivation::DontActivate);
    }

    // One convex-hull static body per brush (true angled-brush collision).
    void addBrushHulls(JPH::BodyInterface& bi, const Level& level)
    {
        for (const auto& hull : level.collisionHulls)
        {
            if (hull.size() < 4) continue;   // ConvexHull needs a bounded solid

            JPH::Array<JPH::Vec3> points;
            points.reserve(hull.size());
            for (const glm::vec3& p : hull) points.push_back(JPH::Vec3(p.x, p.y, p.z));

            JPH::ConvexHullShapeSettings shapeSettings(points);
            shapeSettings.SetEmbedded();
            auto result = shapeSettings.Create();
            if (!result.IsValid()) continue;   // co-planar/degenerate — skip
            addStaticBody(bi, result.Get());
        }
    }

    // Fallback: an AABB box per surface (exact for the axis-aligned C++ showcase,
    // which carries no collision hulls). Thin axes are fattened to clear Jolt's
    // convex radius.
    void addSurfaceBoxes(JPH::BodyInterface& bi, const Level& level)
    {
        for (const auto& sector : level.sectors)
            for (const auto& surface : sector.surfaces)
            {
                glm::vec3 mn = glm::min(glm::min(surface.vertices[0], surface.vertices[1]),
                                        glm::min(surface.vertices[2], surface.vertices[3]));
                glm::vec3 mx = glm::max(glm::max(surface.vertices[0], surface.vertices[1]),
                                        glm::max(surface.vertices[2], surface.vertices[3]));
                for (int i = 0; i < 3; i++)
                    if (mx[i] - mn[i] < 0.01f) { mn[i] -= 0.1f; mx[i] += 0.1f; }

                glm::vec3 half   = (mx - mn) * 0.5f;
                glm::vec3 centre = (mn + mx) * 0.5f;

                JPH::BoxShapeSettings shapeSettings(JPH::Vec3(half.x, half.y, half.z));
                shapeSettings.SetEmbedded();
                auto result = shapeSettings.Create();
                if (!result.IsValid()) continue;

                JPH::BodyCreationSettings settings(
                    result.Get(),
                    JPH::RVec3(centre.x, centre.y, centre.z),
                    JPH::Quat::sIdentity(),
                    JPH::EMotionType::Static,
                    Layers::NON_MOVING);
                bi.CreateAndAddBody(settings, JPH::EActivation::DontActivate);
            }
    }
}

// Static Jolt bodies for the level geometry. A `.map` level carries per-brush
// convex hulls (angled-brush accurate); the hard-coded showcase carries none, so
// it falls back to the AABB-per-surface path it has always used.
void createLevelBodies(entt::registry& registry, const Level& level)
{
    auto& bodyInterface = registry.ctx().get<JoltWorld>().getBodyInterface();

    if (!level.collisionHulls.empty())
        addBrushHulls(bodyInterface, level);
    else
        addSurfaceBoxes(bodyInterface, level);
}
