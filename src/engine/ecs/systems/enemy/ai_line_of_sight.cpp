#include "engine/ecs/systems/enemy/ai_support.h"

#include "engine/ecs/components.h"
#include "engine/ecs/systems/combat/combat_internal.h"   // raycastEntities
#include "engine/level/level.h"
#include "engine/physics/raycast.h"

// True if `self` has an unobstructed line from `from` to `to` — tests level
// surfaces first, then solid entities (ignoring the player itself and, via
// raycastEntities, in-flight projectiles).
bool aiClearLineOfSight(entt::registry& reg, const Level& level, entt::entity self,
                        entt::entity player, glm::vec3 from, glm::vec3 to)
{
    glm::vec3 delta = to - from;
    float dist = glm::length(delta);
    if (dist < 0.001f) return true;
    Ray ray{ from, delta / dist };

    for (const auto& sector : level.sectors)
        for (const auto& s : sector.surfaces)
        {
            AABB box;
            box.min = glm::min(glm::min(s.vertices[0], s.vertices[1]),
                               glm::min(s.vertices[2], s.vertices[3])) - glm::vec3(0.05f);
            box.max = glm::max(glm::max(s.vertices[0], s.vertices[1]),
                               glm::max(s.vertices[2], s.vertices[3])) + glm::vec3(0.05f);
            auto hit = rayIntersectionsAABB(ray, box);
            if (hit && *hit < dist - 0.1f) return false;
        }

    auto hit = raycastEntities(reg, ray, self, dist);
    return !(hit && hit->entity != player && hit->distance < dist - 0.1f);
}
