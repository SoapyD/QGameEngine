#include "engine/ecs/systems/player/camera_follow_system.h"

#include "engine/ecs/components.h"
#include "engine/renderer/camera.h"

#include <glm/glm.hpp>

void cameraFollowSystem(entt::registry& registry, Camera& camera, float alpha)
{
    auto playerView = registry.view<Position, AABBCollider, TagPlayer>();
    for (auto [entity, pos, col] : playerView.each())
    {
        glm::vec3 followPos = pos.value;
        if (registry.all_of<PrevPosition>(entity))
        {
            const glm::vec3& prev = registry.get<PrevPosition>(entity).value;
            // Lerp only for normal motion; snap on big jumps (teleport/respawn).
            if (glm::distance(prev, pos.value) < 3.0f)
                followPos = glm::mix(prev, pos.value, alpha);
        }
        // Camera sits near the top of the collider (eye height).
        glm::vec3 eyePos = followPos;
        eyePos.y += col.halfExtents.y * kEyeHeightFraction;
        camera.setPosition(eyePos);
    }
}
