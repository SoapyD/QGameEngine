#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include "engine/ecs/components/rendering.h"  // MeshRenderer
#include "engine/ecs/components/gameplay.h"   // TriggerAction, Pickup
#include "engine/ecs/types/mesh_assets.h"

// ─────────────────────────────────────────────────────────────────────
// Entity factories
// ─────────────────────────────────────────────────────────────────────
// One function per spawnable entity type. Each builds the entity's full
// component set and returns its handle. This is the layer the TrenchBroom
// `.map` loader will map `classname` → factory onto (Phase 5), and it keeps
// scene_setup.cpp declarative (what is in the scene) rather than mechanical
// (how each entity is assembled).
//
// Physics note: factories that need a Jolt *static* or *dynamic* body create it
// here (the body can exist immediately). Kinematic bodies for movers are created
// later in buildWorld(), after the broad-phase is first optimised, so spawnMover
// only attaches the Mover/render components.

namespace factories
{
    // MeshAssets lives in ecs/types/mesh_assets.h (included above).

    // A MeshRenderer for the shared cube mesh with the given texture.
    MeshRenderer cubeRenderer(const MeshAssets& a, unsigned int textureId);

    // Player: collider, movement, health, spawn point, weapons and ammo.
    entt::entity spawnPlayer(entt::registry& reg, glm::vec3 pos);

    entt::entity spawnDirectionalLight(entt::registry& reg, glm::vec3 direction,
                                       glm::vec3 color, float ambientStrength);

    // Point light plus its small debug cube marker (same position).
    entt::entity spawnPointLight(entt::registry& reg, const MeshAssets& a, glm::vec3 pos,
                                 glm::vec3 color, float ambientStrength, float linear,
                                 float quadratic, unsigned int markerTexture);

    // Static box: renders and gets a Jolt static body.
    entt::entity spawnStaticBox(entt::registry& reg, const MeshAssets& a, glm::vec3 pos,
                                glm::vec3 scale, glm::vec3 halfExtents, unsigned int textureId);

    // Dynamic physics cube that periodically resets to its start (demo prop).
    entt::entity spawnDemoCube(entt::registry& reg, const MeshAssets& a, glm::vec3 startPos,
                               glm::vec3 startVel, float resetInterval, unsigned int textureId);

    // Kinematic mover (door / lift). Renders and gets a Mover; its Jolt
    // kinematic body is created later in buildWorld().
    entt::entity spawnMover(entt::registry& reg, const MeshAssets& a, glm::vec3 startPos,
                            glm::vec3 endPos, glm::vec3 scale, glm::vec3 halfExtents,
                            float speed, float waitTime, float startDelay, unsigned int textureId);

    // Trigger volume. `target` is the entity it activates (movers); pass
    // entt::null for teleport/damage/heal. `destination` is used by Teleport,
    // `value` by Damage/Heal.
    entt::entity spawnTrigger(entt::registry& reg, glm::vec3 pos, glm::vec3 halfExtents,
                              TriggerAction action, entt::entity target, glm::vec3 destination,
                              float value, float cooldown);

    // Green-by-default wireframe box visualising a trigger volume.
    entt::entity spawnDebugWireframe(entt::registry& reg, const MeshAssets& a, glm::vec3 pos,
                                     glm::vec3 scale, unsigned int textureId);

    // Decorative box with no physics (poles, markers, lava surface).
    entt::entity spawnDecorBox(entt::registry& reg, const MeshAssets& a, glm::vec3 pos,
                               glm::vec3 scale, unsigned int textureId);

    // Item pickup: a small rendered cube with a sensor AABB (ECS overlap only,
    // no Jolt body) carrying the given Pickup. pickupSystem grants + destroys it.
    entt::entity spawnPickup(entt::registry& reg, const MeshAssets& a, glm::vec3 pos,
                             const Pickup& pickup, unsigned int textureId);

    // Weapon pickup: like spawnPickup but rendered as the actual gun mesh for the
    // weapon (from MeshAssets::gunVAO), flat-coloured by weaponColor via a Colour
    // component, angled so its profile reads. Sensor AABB, no Jolt body.
    entt::entity spawnWeaponPickup(entt::registry& reg, const MeshAssets& a, glm::vec3 pos,
                                   const Pickup& pickup, WeaponType weapon);

    // Enemy grunt: a solid, coloured humanoid-ish box with Health + AIState. Its
    // CharacterVirtual (so it stands, blocks the player, and walks) is created in
    // buildWorld (initEnemyCharacters); aiSystem drives melee behaviour.
    entt::entity spawnMonsterGrunt(entt::registry& reg, const MeshAssets& a, glm::vec3 pos);

    // Enemy ranged grunt: like the grunt but carries a RangedAttack, so aiSystem
    // holds it at standoff range and fires dodgeable Enemy-faction projectiles.
    entt::entity spawnMonsterRanged(entt::registry& reg, const MeshAssets& a, glm::vec3 pos);
}
