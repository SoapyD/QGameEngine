#include "engine/ecs/systems/enemy/ai_system.h"

#include "engine/ecs/components.h"
#include "engine/ecs/apply_damage.h"
#include "engine/ecs/systems/combat/combat_internal.h"   // raycastEntities
#include "engine/ai/find_path.h"
#include "engine/ai/types/nav_grid.h"
#include "engine/physics/jolt_world.h"
#include "engine/physics/physics_config.h"
#include "engine/physics/raycast.h"
#include "engine/audio/queue_sound.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr float kDetectRange  = 20.0f;  // acquire the player within this (needs LoS)
    constexpr float kPursueRange  = 28.0f;  // once aggroed, chase until beyond this
    constexpr float kAttackRange  = 2.2f;
    constexpr float kMoveSpeed    = 3.0f;   // units / second
    constexpr float kAttackDamage = 8.0f;
    constexpr float kAttackPeriod = 1.0f;   // seconds between hits
    constexpr float kEyeOffset    = 0.6f;
    constexpr float kRepathPeriod = 0.4f;   // seconds between A* recomputes
    constexpr float kWaypointHit  = 0.5f;   // distance at which a waypoint is "reached"
    constexpr int   kRepathBudget = 4;      // A* recomputes allowed per tick (stagger enemies)

    bool clearLineOfSight(entt::registry& reg, const Level& level, entt::entity self,
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

    void faceDir(entt::registry& reg, entt::entity e, glm::vec3 d)
    {
        if (glm::length(d) < 0.001f) return;
        if (auto* rot = reg.try_get<Rotation>(e))
            rot->euler.y = glm::degrees(std::atan2(d.x, d.z));
    }
}

void aiSystem(entt::registry& registry, const Level& level)
{
    const float dt = registry.ctx().get<PhysicsConfig>().fixedDeltaTime;
    auto& bodyInterface = registry.ctx().get<JoltWorld>().getBodyInterface();
    const NavGrid* nav = registry.ctx().find<NavGrid>();

    entt::entity player = entt::null;
    glm::vec3 playerPos(0.0f);
    for (auto [e, pos] : registry.view<Position, TagPlayer>().each())
    { player = e; playerPos = pos.value; break; }
    if (player == entt::null) return;

    int repathBudget = kRepathBudget;

    for (auto [entity, ai, pos, body, path] : registry.view<AIState, Position, JoltBody, AIPath>().each())
    {
        auto haltInPlace = [&] {
            bodyInterface.MoveKinematic(body.id,
                JPH::RVec3(pos.value.x, pos.value.y, pos.value.z), JPH::Quat::sIdentity(), dt);
        };

        glm::vec3 flat = playerPos - pos.value; flat.y = 0.0f;
        float dist = glm::length(flat);
        glm::vec3 toPlayer = dist > 0.001f ? flat / dist : glm::vec3(0.0f);
        bool los = clearLineOfSight(registry, level, entity, player,
                                    pos.value + glm::vec3(0.0f, kEyeOffset, 0.0f),
                                    playerPos + glm::vec3(0.0f, 0.4f, 0.0f));

        // ─── Aggro: acquire on sight, drop when the player escapes ───
        if (ai.target == entt::null)
        {
            if (los && dist < kDetectRange) ai.target = player;
        }
        else if (dist > kPursueRange)
        {
            ai.target = entt::null;
        }

        if (ai.target == entt::null)
        {
            ai.state = AIStateKind::Idle;
            path.waypoints.clear();
            haltInPlace();
            continue;
        }

        if (ai.attackCooldown > 0.0f)
            ai.attackCooldown = std::max(0.0f, ai.attackCooldown - dt);

        // ─── Attack: in range with a clear shot ──────────────────
        if (los && dist <= kAttackRange)
        {
            ai.state = AIStateKind::Attack;
            path.waypoints.clear();
            faceDir(registry, entity, toPlayer);
            haltInPlace();
            if (ai.attackCooldown <= 0.0f && applyDamage(registry, player, kAttackDamage))
            {
                queueSoundAt(registry, "weapon.gauntlet", pos.value);
                ai.attackCooldown = kAttackPeriod;
            }
            continue;
        }

        // ─── Chase: path toward the player, routing around obstacles ─
        ai.state = AIStateKind::Chase;
        path.repathTimer -= dt;
        bool atEnd = path.index >= path.waypoints.size();
        if (nav && repathBudget > 0 && (path.waypoints.empty() || atEnd || path.repathTimer <= 0.0f))
        {
            path.waypoints = findPath(*nav, pos.value, playerPos);
            path.index = 0;
            path.repathTimer = kRepathPeriod;
            --repathBudget;
        }

        // Advance the follow cursor, then steer to the current waypoint (or, with
        // no path, straight at the player).
        glm::vec3 stepDir = toPlayer;
        if (path.index < path.waypoints.size())
        {
            glm::vec3 toWp = path.waypoints[path.index] - pos.value; toWp.y = 0.0f;
            if (glm::length(toWp) < kWaypointHit) ++path.index;
            if (path.index < path.waypoints.size())
            {
                toWp = path.waypoints[path.index] - pos.value; toWp.y = 0.0f;
                if (glm::length(toWp) > 0.001f) stepDir = glm::normalize(toWp);
            }
        }

        glm::vec3 target = pos.value + stepDir * kMoveSpeed * dt;
        bodyInterface.MoveKinematic(body.id,
            JPH::RVec3(target.x, target.y, target.z), JPH::Quat::sIdentity(), dt);
        faceDir(registry, entity, stepDir);
    }
}
