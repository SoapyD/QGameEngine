// QEngine headless harness
// ─────────────────────────────────────────────────────────────────────
// Runs the real fixed-tick simulation (qengine::stepSimulation) with NO
// rendering and SCRIPTED input, recording per-tick state and asserting on
// it. Lets the physics bugs from the eval plans reproduce and regression-
// test without a display or a human at the keyboard.
//
//   QEngineHeadless [scenario]
//
// Exit code 0 = all assertions passed, 1 = a failure (CI-friendly).

#include "engine/core/resource_manager.h"
#include "engine/app/simulation.h"
#include "engine/ecs/components.h"
#include "engine/physics/jolt_world.h"
#include "engine/physics/physics_config.h"

#include <Jolt/Physics/Character/CharacterVirtual.h>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    // ─── Entity lookup ───────────────────────────────────────────────
    entt::entity findPlayer(entt::registry& reg)
    {
        auto view = reg.view<TagPlayer, JoltCharacter, Position, AABBCollider>();
        for (auto e : view) return e;
        return entt::null;
    }

    // The lift is the Mover with the lowest start — door starts at y=1.5,
    // lift at y=0.2.
    entt::entity findLift(entt::registry& reg)
    {
        entt::entity best = entt::null;
        float bestY = 1e9f;
        for (auto [e, mover] : reg.view<Mover>().each())
        {
            if (mover.startPos.y < bestY) { bestY = mover.startPos.y; best = e; }
        }
        return best;
    }

    // ─── Input driving ───────────────────────────────────────────────
    struct Input
    {
        glm::vec3 wishDir{0.0f};
        glm::vec3 lookDir{0.0f, 0.0f, -1.0f};
        bool jump = false;
        bool fire = false;
        int  weaponSwitch = -1;
    };

    void applyInput(entt::registry& reg, entt::entity player, const Input& in)
    {
        auto& pi = reg.get<PlayerInput>(player);
        pi.wishDir = in.wishDir;
        pi.jump = in.jump;
        pi.fire = in.fire;
        pi.weaponSwitch = in.weaponSwitch;
        // combatSystem reads the camera/aim direction from the context
        reg.ctx().insert_or_assign<CameraDirection>(CameraDirection{in.lookDir});
    }

    void teleportPlayer(entt::registry& reg, entt::entity player, glm::vec3 p)
    {
        auto& jc = reg.get<JoltCharacter>(player);
        jc.character->SetPosition(JPH::RVec3(p.x, p.y, p.z));
        jc.character->SetLinearVelocity(JPH::Vec3::sZero());
        reg.get<Position>(player).value = p;
    }

    float playerVelY(entt::registry& reg, entt::entity player)
    {
        return reg.get<JoltCharacter>(player).character->GetLinearVelocity().GetY();
    }

    // ─── Reporting ───────────────────────────────────────────────────
    bool report(const std::string& name, bool pass, const std::string& detail)
    {
        std::cout << "[" << (pass ? "PASS" : "FAIL") << "] " << name
                  << " — " << detail << std::endl;
        return pass;
    }

    // ─── Scenarios ───────────────────────────────────────────────────

    // Stand still on the floor; player Y should converge and stay put.
    bool scenario_rest_no_jitter(entt::registry& reg, JoltWorld& jolt, const Level& level, float dt)
    {
        entt::entity player = findPlayer(reg);
        Input idle;

        // Settle onto the floor.
        for (int i = 0; i < 90; i++) { applyInput(reg, player, idle); qengine::stepSimulation(reg, jolt, level, dt); }

        // Measure over the next 240 ticks.
        float minY = 1e9f, maxY = -1e9f, maxAbsVelY = 0.0f;
        for (int i = 0; i < 240; i++)
        {
            applyInput(reg, player, idle);
            qengine::stepSimulation(reg, jolt, level, dt);
            float y = reg.get<Position>(player).value.y;
            minY = std::min(minY, y);
            maxY = std::max(maxY, y);
            maxAbsVelY = std::max(maxAbsVelY, std::abs(playerVelY(reg, player)));
        }

        float band = maxY - minY;
        char buf[160];
        std::snprintf(buf, sizeof(buf), "resting Y band=%.4f (max-min over 240 ticks), maxAbsVelY=%.4f", band, maxAbsVelY);
        return report("rest_no_jitter", band < 0.02f && maxAbsVelY < 0.5f, buf);
    }

    // Place the player on the lift, idle while it rises; the player's feet
    // should track the lift's top surface within a small band.
    bool scenario_ride_lift_up(entt::registry& reg, JoltWorld& jolt, const Level& level, float dt)
    {
        entt::entity player = findPlayer(reg);
        entt::entity lift   = findLift(reg);
        if (lift == entt::null) return report("ride_lift_up", false, "no lift mover found");

        const float playerHalfY = reg.get<AABBCollider>(player).halfExtents.y; // 0.85
        const float liftHalfY   = reg.get<AABBCollider>(lift).halfExtents.y;    // 0.10
        const glm::vec3 liftStart = reg.get<Mover>(lift).startPos;              // (10,0.2,25)

        // Drop the player onto the lift centre.
        teleportPlayer(reg, player, glm::vec3(liftStart.x,
                                              liftStart.y + liftHalfY + playerHalfY + 0.02f,
                                              liftStart.z));

        Input idle;
        // Let the player settle + the lift's startDelay (2s) elapse.
        for (int i = 0; i < 150; i++) { applyInput(reg, player, idle); qengine::stepSimulation(reg, jolt, level, dt); }

        // Ride. The failure mode that matters is SEPARATION — the player
        // floating up off the lift (foot rising above the lift top) or
        // jittering. The player being below the lift top is benign contact
        // (e.g. the lift compressing into a ceiling-pinned player at the very
        // top of travel), not a carry bug.
        float maxSeparation = 0.0f;   // how far the foot floats ABOVE the lift
        float maxAbsVelY = 0.0f;
        for (int i = 0; i < 300; i++)
        {
            applyInput(reg, player, idle);
            qengine::stepSimulation(reg, jolt, level, dt);

            if (reg.get<Mover>(lift).state == MoverState::Moving)
            {
                float footY    = reg.get<Position>(player).value.y - playerHalfY;
                float liftTopY = reg.get<Position>(lift).value.y + liftHalfY;
                maxSeparation  = std::max(maxSeparation, footY - liftTopY);
                maxAbsVelY     = std::max(maxAbsVelY, std::abs(playerVelY(reg, player)));
            }
        }

        char buf[160];
        std::snprintf(buf, sizeof(buf), "max separation above lift during ascent=%.4f (1 tick of travel = 0.033), maxAbsVelY=%.4f", maxSeparation, maxAbsVelY);
        // Carried correctly = never floats more than ~one tick above the lift.
        return report("ride_lift_up", maxSeparation < 0.05f, buf);
    }

    // The reported complaint: WALK onto the lift platform from the floor.
    // The player steps up the floor->lift seam (~0.3) onto the platform. We
    // watch for a launch/pop (large upward velocity), getting stuck at the
    // seam (no forward progress), or failing to end up on the platform.
    bool scenario_walk_onto_lift(entt::registry& reg, JoltWorld& jolt, const Level& level, float dt)
    {
        entt::entity player = findPlayer(reg);
        entt::entity lift   = findLift(reg);
        if (lift == entt::null) return report("walk_onto_lift", false, "no lift mover found");

        const float playerHalfY = reg.get<AABBCollider>(player).halfExtents.y;
        const float liftHalfY   = reg.get<AABBCollider>(lift).halfExtents.y;
        const glm::vec3 liftStart = reg.get<Mover>(lift).startPos;  // (10,0.2,25)
        const float liftTop = liftStart.y + liftHalfY;             // ~0.30

        // Stand on the floor ~4 units south of the lift centre.
        teleportPlayer(reg, player, glm::vec3(liftStart.x, playerHalfY, liftStart.z - 4.0f));
        Input idle;
        for (int i = 0; i < 60; i++) { applyInput(reg, player, idle); qengine::stepSimulation(reg, jolt, level, dt); }

        // Walk north (+z) until over the lift centre, then stop. (Total time
        // stays under the lift's 2s start-delay so it doesn't move underfoot.)
        Input walk; walk.wishDir = glm::vec3(0, 0, 1); walk.lookDir = glm::vec3(0, 0, 1);
        float maxUpVelY = 0.0f;
        int   stuckSeamTicks = 0;
        for (int i = 0; i < 90 && reg.get<Position>(player).value.z < liftStart.z; i++)
        {
            float zBefore = reg.get<Position>(player).value.z;
            applyInput(reg, player, walk);
            qengine::stepSimulation(reg, jolt, level, dt);
            maxUpVelY = std::max(maxUpVelY, playerVelY(reg, player));            // launch detector
            float advance = reg.get<Position>(player).value.z - zBefore;
            if (i > 10 && advance < 0.01f) stuckSeamTicks++;                     // stalled at the seam
        }
        // Stop and settle on the platform.
        for (int i = 0; i < 30; i++) { applyInput(reg, player, idle); qengine::stepSimulation(reg, jolt, level, dt); }

        glm::vec3 p = reg.get<Position>(player).value;
        float footY = p.y - playerHalfY;
        bool onPlatform = std::abs(p.x - liftStart.x) < 1.5f && std::abs(p.z - liftStart.z) < 1.5f
                          && footY > liftTop - 0.12f;
        char buf[200];
        std::snprintf(buf, sizeof(buf),
            "onPlatform=%d footY=%.3f (liftTop=%.3f) maxUpVelY=%.3f stuckSeamTicks=%d",
            (int)onPlatform, footY, liftTop, maxUpVelY, stuckSeamTicks);
        // Smooth boarding = steps up onto the platform, no launch, no seam stall.
        return report("walk_onto_lift", onPlatform && maxUpVelY < 2.0f && stuckSeamTicks < 5, buf);
    }

    // Walk in a straight line across the multi-body floor. Each level surface
    // is a separate static box, so seams between them can snag the capsule
    // (eval 05 sec.4). A clean floor walk shows no vertical disturbance.
    bool scenario_walk_floor_seams(entt::registry& reg, JoltWorld& jolt, const Level& level, float dt)
    {
        entt::entity player = findPlayer(reg);
        const float halfY = reg.get<AABBCollider>(player).halfExtents.y;

        teleportPlayer(reg, player, glm::vec3(10.0f, halfY, 8.0f));
        Input idle;
        for (int i = 0; i < 60; i++) { applyInput(reg, player, idle); qengine::stepSimulation(reg, jolt, level, dt); }

        Input walk; walk.wishDir = glm::vec3(0, 0, 1); walk.lookDir = glm::vec3(0, 0, 1);
        float maxAbsVelY = 0.0f, minFwdSpeed = 1e9f, startZ = reg.get<Position>(player).value.z, prevZ = startZ;
        for (int i = 0; i < 120; i++)
        {
            applyInput(reg, player, walk);
            qengine::stepSimulation(reg, jolt, level, dt);
            float z = reg.get<Position>(player).value.z;
            maxAbsVelY = std::max(maxAbsVelY, std::abs(playerVelY(reg, player)));
            if (i > 30) minFwdSpeed = std::min(minFwdSpeed, (z - prevZ) / dt);  // after accel ramp
            prevZ = z;
        }
        float travelled = reg.get<Position>(player).value.z - startZ;
        char buf[200];
        std::snprintf(buf, sizeof(buf),
            "travelled=%.2f maxAbsVelY=%.4f minFwdSpeed=%.3f (no seam stalls/pops if velY~0 & speed steady)",
            travelled, maxAbsVelY, minFwdSpeed);
        // Flat floor: negligible vertical disturbance, no forward stall.
        return report("walk_floor_seams", maxAbsVelY < 0.5f && minFwdSpeed > 3.0f, buf);
    }

    // Fire a rocket straight down at the floor. The projectile must detonate
    // at the floor, not pass through it — level geometry isn't an ECS entity,
    // so projectile-vs-level collision must be handled explicitly (eval 07).
    bool scenario_rocket_vs_floor(entt::registry& reg, JoltWorld& jolt, const Level& level, float dt)
    {
        entt::entity player = findPlayer(reg);

        // Put the player on open floor and let it settle.
        teleportPlayer(reg, player, glm::vec3(15.0f, 0.85f, 15.0f));
        Input idle;
        for (int i = 0; i < 60; i++) { applyInput(reg, player, idle); qengine::stepSimulation(reg, jolt, level, dt); }

        // One tick: switch to the rocket launcher (slot 1) and fire straight down.
        Input fire;
        fire.weaponSwitch = 1;
        fire.fire = true;
        fire.lookDir = glm::vec3(0.0f, -1.0f, 0.0f);
        applyInput(reg, player, fire);
        qengine::stepSimulation(reg, jolt, level, dt);

        // Track the projectile until it's destroyed or it has clearly tunnelled.
        float minProjY = 1e9f;
        bool stillAlive = true;
        for (int i = 0; i < 120; i++)
        {
            applyInput(reg, player, idle);   // stop firing
            qengine::stepSimulation(reg, jolt, level, dt);

            stillAlive = false;
            for (auto [e, p, proj] : reg.view<Position, Projectile>().each())
            {
                stillAlive = true;
                minProjY = std::min(minProjY, p.value.y);
            }
            if (!stillAlive) break;
        }

        char buf[200];
        std::snprintf(buf, sizeof(buf),
            "projectile minY=%.3f stillAlive=%d (should detonate near floor y=0, not tunnel below)",
            (minProjY > 1e8f ? 0.0f : minProjY), (int)stillAlive);
        // Pass = the rocket stopped at/above the floor and didn't sink through.
        return report("rocket_vs_floor", minProjY > -0.5f && minProjY < 1e8f, buf);
    }

    // Walk into the teleporter trigger; the player should jump to the
    // destination and stay there.
    bool scenario_teleporter(entt::registry& reg, JoltWorld& jolt, const Level& level, float dt)
    {
        entt::entity player = findPlayer(reg);

        // Teleporter trigger sits at (5,0.5,5); destination is (25,1,25).
        teleportPlayer(reg, player, glm::vec3(5.0f, 1.5f, 5.0f));

        Input idle;
        for (int i = 0; i < 20; i++) { applyInput(reg, player, idle); qengine::stepSimulation(reg, jolt, level, dt); }

        glm::vec3 p = reg.get<Position>(player).value;
        float distToDest = glm::length(p - glm::vec3(25.0f, 1.0f, 25.0f));
        char buf[160];
        std::snprintf(buf, sizeof(buf), "player ended at (%.2f,%.2f,%.2f), dist to destination=%.2f", p.x, p.y, p.z, distToDest);
        return report("teleporter", distToDest < 2.0f, buf);
    }
}

int main(int argc, char** argv)
{
    std::string scenario = (argc > 1) ? argv[1] : "ride_lift_up";

    // Fully headless: no window, no GL context. loadResources caches GL-free
    // stubs and buildWorld skips render-mesh building, so this runs on a box
    // with no GPU/driver (CI-friendly). Physics is unaffected.
    ResourceManager resources;
    qengine::loadResources(resources, /*headless=*/true);

    entt::registry registry;
    auto& cfg = registry.ctx().emplace<PhysicsConfig>();
    auto& jolt = registry.ctx().emplace<JoltWorld>();
    jolt.init(/*singleThreaded=*/true, cfg.gravity);   // deterministic

    Level level = qengine::buildWorld(registry, resources, jolt, /*headless=*/true);
    float dt = cfg.fixedDeltaTime;

    std::cout << "── headless scenario: " << scenario << " ──" << std::endl;

    bool pass;
    if      (scenario == "rest_no_jitter")   pass = scenario_rest_no_jitter(registry, jolt, level, dt);
    else if (scenario == "ride_lift_up")     pass = scenario_ride_lift_up(registry, jolt, level, dt);
    else if (scenario == "walk_onto_lift")   pass = scenario_walk_onto_lift(registry, jolt, level, dt);
    else if (scenario == "walk_floor_seams") pass = scenario_walk_floor_seams(registry, jolt, level, dt);
    else if (scenario == "rocket_vs_floor")  pass = scenario_rocket_vs_floor(registry, jolt, level, dt);
    else if (scenario == "teleporter")       pass = scenario_teleporter(registry, jolt, level, dt);
    else { std::cerr << "unknown scenario: " << scenario << std::endl; pass = false; }

    jolt.shutdown();
    resources.clear();
    return pass ? 0 : 1;
}
