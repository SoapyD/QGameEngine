#include "engine/level/factories.h"

#include "engine/ecs/components.h"   // spawns touch every component group
#include "engine/physics/jolt_bodies.h"
#include "engine/ecs/weapon_definitions.h"

#include <utility>

namespace factories
{
    MeshRenderer cubeRenderer(const MeshAssets& a, unsigned int textureId)
    {
        return MeshRenderer{ a.cubeVAO, 0u, a.litShader, textureId, true, a.cubeIndexCount };
    }

    entt::entity spawnPlayer(entt::registry& reg, glm::vec3 pos)
    {
        auto player = reg.create();
        reg.emplace<Position>(player, pos);
        reg.emplace<AABBCollider>(player, glm::vec3(0.3f, 0.85f, 0.3f), false);
        reg.emplace<OnGround>(player);
        reg.emplace<CharacterPhysics>(player);
        reg.emplace<PlayerInput>(player);
        reg.emplace<TagPlayer>(player);
        reg.emplace<TagTriggerable>(player);  // can activate trigger volumes
        reg.emplace<Health>(player, 100.0f, 100.0f, 0.0f);
        reg.emplace<SpawnPoint>(player, pos, 0.0f);
        reg.emplace<DamageFlash>(player);
        reg.emplace<PendingKnockback>(player);

        // Two weapons: one hitscan, one projectile. Start on the shotgun.
        WeaponInventory inv;
        inv.weapons.push_back(createWeapon(WeaponType::Shotgun));
        inv.weapons.push_back(createWeapon(WeaponType::RocketLauncher));
        inv.currentWeapon = 0;
        reg.emplace<WeaponInventory>(player, std::move(inv));

        reg.emplace<Ammo>(player, 25, 0, 5, 0);  // 25 shells, 5 rockets
        return player;
    }

    entt::entity spawnDirectionalLight(entt::registry& reg, glm::vec3 direction,
                                       glm::vec3 color, float ambientStrength)
    {
        auto e = reg.create();
        reg.emplace<DirectionalLight>(e, direction, color, ambientStrength);
        return e;
    }

    entt::entity spawnPointLight(entt::registry& reg, const MeshAssets& a, glm::vec3 pos,
                                 glm::vec3 color, float ambientStrength, float linear,
                                 float quadratic, unsigned int markerTexture)
    {
        auto light = reg.create();
        reg.emplace<Position>(light, pos);
        reg.emplace<PointLight>(light, color, ambientStrength, linear, quadratic);

        // Small debug cube marking the light's position.
        auto marker = reg.create();
        reg.emplace<Position>(marker, pos);
        reg.emplace<Scale>(marker, glm::vec3(0.2f));
        reg.emplace<MeshRenderer>(marker, cubeRenderer(a, markerTexture));
        return light;
    }

    entt::entity spawnStaticBox(entt::registry& reg, const MeshAssets& a, glm::vec3 pos,
                                glm::vec3 scale, glm::vec3 halfExtents, unsigned int textureId)
    {
        auto e = reg.create();
        reg.emplace<Position>(e, pos);
        reg.emplace<Scale>(e, scale);
        reg.emplace<AABBCollider>(e, halfExtents, false);
        reg.emplace<MeshRenderer>(e, cubeRenderer(a, textureId));
        createStaticBody(reg, e);
        return e;
    }

    entt::entity spawnDemoCube(entt::registry& reg, const MeshAssets& a, glm::vec3 startPos,
                               glm::vec3 startVel, float resetInterval, unsigned int textureId)
    {
        auto cube = reg.create();
        reg.emplace<Position>(cube, startPos);
        reg.emplace<Velocity>(cube, startVel);
        reg.emplace<AABBCollider>(cube, glm::vec3(0.5f), false);
        reg.emplace<OnGround>(cube);
        reg.emplace<DemoReset>(cube, startPos, startVel, resetInterval, 0.0f);
        reg.emplace<MeshRenderer>(cube, cubeRenderer(a, textureId));
        createDynamicBody(reg, cube);
        return cube;
    }

    entt::entity spawnMover(entt::registry& reg, const MeshAssets& a, glm::vec3 startPos,
                            glm::vec3 endPos, glm::vec3 scale, glm::vec3 halfExtents,
                            float speed, float waitTime, float startDelay, unsigned int textureId)
    {
        auto e = reg.create();
        reg.emplace<Position>(e, startPos);
        reg.emplace<Scale>(e, scale);
        reg.emplace<MeshRenderer>(e, cubeRenderer(a, textureId));
        reg.emplace<Mover>(e, startPos, endPos, speed, waitTime, startDelay,
                           0.0f, 0.0f, MoverState::Idle, true);
        reg.emplace<AABBCollider>(e, halfExtents, false);
        return e;
    }

    entt::entity spawnTrigger(entt::registry& reg, glm::vec3 pos, glm::vec3 halfExtents,
                              TriggerAction action, entt::entity target, glm::vec3 destination,
                              float value, float cooldown)
    {
        auto e = reg.create();
        reg.emplace<Position>(e, pos);
        reg.emplace<AABBCollider>(e, halfExtents, true);
        reg.emplace<TriggerVolume>(e, action, target, destination, value, std::string(),
                                   false, false, cooldown, 0.0f);
        return e;
    }

    entt::entity spawnDebugWireframe(entt::registry& reg, const MeshAssets& a, glm::vec3 pos,
                                     glm::vec3 scale, unsigned int textureId)
    {
        auto e = reg.create();
        reg.emplace<Position>(e, pos);
        reg.emplace<Scale>(e, scale);
        reg.emplace<MeshRenderer>(e, cubeRenderer(a, textureId));
        reg.emplace<TagDebugWireframe>(e);
        return e;
    }

    entt::entity spawnDecorBox(entt::registry& reg, const MeshAssets& a, glm::vec3 pos,
                               glm::vec3 scale, unsigned int textureId)
    {
        auto e = reg.create();
        reg.emplace<Position>(e, pos);
        reg.emplace<Scale>(e, scale);
        reg.emplace<MeshRenderer>(e, cubeRenderer(a, textureId));
        return e;
    }
}
