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
#include "engine/level/showcase_descriptor.h"
#include "harness/map_scenarios.h"
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

    // Hard-teleport an enemy (its CharacterVirtual + ECS mirror). SetPosition also
    // moves the character's inner body, so the player still collides against it.
    void teleportEnemy(entt::registry& reg, entt::entity e, glm::vec3 p)
    {
        auto& ch = reg.get<JoltCharacter>(e).character;
        ch->SetPosition(JPH::RVec3(p.x, p.y, p.z));
        ch->SetLinearVelocity(JPH::Vec3::sZero());
        reg.get<Position>(e).value = p;
    }

    // Remove all enemies (entity + its CharacterVirtual, whose destructor drops the
    // inner body). Pure player-physics scenarios call this so aggroed grunts can't
    // wander in and disturb the measurement.
    void clearEnemies(JoltWorld&, entt::registry& reg)
    {
        std::vector<entt::entity> es;
        for (auto e : reg.view<AIState>()) es.push_back(e);
        for (auto e : es) reg.destroy(e);
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
        clearEnemies(jolt, reg);   // isolate the floor-physics measurement from AI
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

        // One tick: switch to the rocket launcher (its WeaponType slot) and fire down.
        Input fire;
        fire.weaponSwitch = static_cast<int>(WeaponType::RocketLauncher);
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

    // The showcase must build identically through the descriptor → classname
    // dispatch path. Tally the descriptor classnames and assert the world's
    // component archetypes match — a data-path regression (an unhandled
    // classname, a mis-wired factory) shows up here as a count mismatch. Pure
    // static check on the built world; runs no ticks.
    bool scenario_spawn_counts(entt::registry& reg, JoltWorld&, const Level&, float)
    {
        // Expected tallies from the source-of-truth descriptors.
        int dPlayer = 0, dMover = 0, dTrigger = 0, dPoint = 0, dDir = 0, dDemo = 0;
        for (const auto& p : showcaseDescriptors())
        {
            if      (p.classname == "info_player_start")                       dPlayer++;
            else if (p.classname == "func_door" || p.classname == "func_plat") dMover++;
            else if (p.classname.rfind("trigger_", 0) == 0)                    dTrigger++;
            else if (p.classname == "light")                                   dPoint++;
            else if (p.classname == "light_environment")                       dDir++;
            else if (p.classname == "prop_dynamic")                            dDemo++;
        }

        // What actually got built.
        auto count = [](auto view) { int n = 0; for (auto e : view) { (void)e; ++n; } return n; };
        int wPlayer  = count(reg.view<TagPlayer>());
        int wMover   = count(reg.view<Mover>());
        int wTrigger = count(reg.view<TriggerVolume>());
        int wPoint   = count(reg.view<PointLight>());
        int wDir     = count(reg.view<DirectionalLight>());
        int wDemo    = count(reg.view<DemoReset>());

        bool match = wPlayer == dPlayer && wMover == dMover && wTrigger == dTrigger
                  && wPoint == dPoint && wDir == dDir && wDemo == dDemo;
        // Sanity floor: the known showcase shape (1 player, 2 movers, 4 triggers,
        // 5 point lights, 1 sun, 3 demo cubes) — catches an empty/half-built scene.
        bool shape = wPlayer == 1 && wMover == 2 && wTrigger == 4
                  && wPoint == 5 && wDir == 1 && wDemo == 3;

        char buf[200];
        std::snprintf(buf, sizeof(buf),
            "player %d/%d mover %d/%d trigger %d/%d point %d/%d dir %d/%d demo %d/%d (world/descriptor)",
            wPlayer, dPlayer, wMover, dMover, wTrigger, dTrigger,
            wPoint, dPoint, wDir, dDir, wDemo, dDemo);
        return report("spawn_counts", match && shape, buf);
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

    // Wound the player, then stand them on the showcase health pickup at
    // (16,1,23). pickupSystem should heal them and consume the pickup entity.
    bool scenario_pickup_health(entt::registry& reg, JoltWorld& jolt, const Level& level, float dt)
    {
        entt::entity player = findPlayer(reg);
        const float halfY = reg.get<AABBCollider>(player).halfExtents.y;

        auto countPickups = [&]() { int n = 0; for (auto e : reg.view<Pickup>()) { (void)e; ++n; } return n; };

        reg.get<Health>(player).current = 50.0f;   // wound so a heal is observable
        float hpBefore = reg.get<Health>(player).current;
        int   before   = countPickups();

        teleportPlayer(reg, player, glm::vec3(16.0f, halfY, 23.0f));  // onto item_health
        Input idle;
        for (int i = 0; i < 10; i++) { applyInput(reg, player, idle); qengine::stepSimulation(reg, jolt, level, dt); }

        float hpAfter = reg.get<Health>(player).current;
        int   after   = countPickups();

        // The HUD toast is set on collect (the HUD itself only renders/fades it).
        const auto& msg = reg.get<PickupMessage>(player);
        bool toastSet = !msg.text.empty() && msg.timer > 0.0f;

        char buf[220];
        std::snprintf(buf, sizeof(buf),
            "health %.0f→%.0f, pickups %d→%d, toast=\"%s\" (expect +25 health, one fewer pickup, a message)",
            hpBefore, hpAfter, before, after, msg.text.c_str());
        return report("pickup_health", hpAfter > hpBefore && after == before - 1 && toastSet, buf);
    }

    // Fire the shotgun once (should spend a shell), then walk onto the shells
    // pickup at (17,1,9) (should refill +10). Proves ammo is consumed and
    // replenished.
    bool scenario_ammo_shells(entt::registry& reg, JoltWorld& jolt, const Level& level, float dt)
    {
        entt::entity player = findPlayer(reg);
        const float halfY = reg.get<AABBCollider>(player).halfExtents.y;

        int start = reg.get<Ammo>(player).shells;   // 25; shotgun is slot 0

        // One shot forward.
        Input fire; fire.fire = true; fire.lookDir = glm::vec3(0.0f, 0.0f, -1.0f);
        applyInput(reg, player, fire);
        qengine::stepSimulation(reg, jolt, level, dt);
        int afterFire = reg.get<Ammo>(player).shells;

        // Walk onto the shells pickup (+10).
        Input idle;
        teleportPlayer(reg, player, glm::vec3(17.0f, halfY, 9.0f));
        for (int i = 0; i < 10; i++) { applyInput(reg, player, idle); qengine::stepSimulation(reg, jolt, level, dt); }
        int afterPickup = reg.get<Ammo>(player).shells;

        char buf[200];
        std::snprintf(buf, sizeof(buf),
            "shells %d→%d (fire -1) →%d (pickup +10)", start, afterFire, afterPickup);
        return report("ammo_shells", afterFire == start - 1 && afterPickup == afterFire + 10, buf);
    }

    // Pin the player in the lava with 10 armour. Armour should soak damage first,
    // then health takes the overflow — proving armour absorbs before health.
    bool scenario_armor_absorb(entt::registry& reg, JoltWorld& jolt, const Level& level, float dt)
    {
        entt::entity player = findPlayer(reg);
        const float halfY = reg.get<AABBCollider>(player).halfExtents.y;

        reg.get<Armor>(player).current = 10.0f;
        reg.get<Health>(player).current = 100.0f;

        // Lava trigger is at (20,0.5,25), dmg 25/sec. Re-pin each tick so the
        // upward lava knockback can't carry the player out of the volume.
        Input idle;
        for (int i = 0; i < 60; i++)   // ~1s → ~25 total damage
        {
            teleportPlayer(reg, player, glm::vec3(20.0f, halfY, 25.0f));
            applyInput(reg, player, idle);
            qengine::stepSimulation(reg, jolt, level, dt);
        }

        float armor  = reg.get<Armor>(player).current;
        float health = reg.get<Health>(player).current;

        // ~25 damage: armour (10) drains to 0, health takes the remaining ~15 → ~85.
        char buf[200];
        std::snprintf(buf, sizeof(buf),
            "after ~25 lava dmg: armor=%.1f (expect 0), health=%.1f (expect ~85)", armor, health);
        return report("armor_absorb", armor < 0.5f && health > 82.0f && health < 88.0f, buf);
    }

    // Walk onto the Nailgun weapon pickup. It must add the weapon + its own ammo
    // (nails) WITHOUT touching shotgun ammo (shells) or auto-switching weapons.
    bool scenario_weapon_pickup(entt::registry& reg, JoltWorld& jolt, const Level& level, float dt)
    {
        entt::entity player = findPlayer(reg);
        const float halfY = reg.get<AABBCollider>(player).halfExtents.y;

        auto ownedCount = [](const WeaponInventory& v)
        { int n = 0; for (bool b : v.owned) if (b) n++; return n; };

        int shellsBefore  = reg.get<Ammo>(player).shells;                 // 25
        int nailsBefore   = reg.get<Ammo>(player).nails;                  // 0
        int ownedBefore   = ownedCount(reg.get<WeaponInventory>(player)); // 2 (shotgun + RL)
        int currentBefore = reg.get<WeaponInventory>(player).currentWeapon;       // 0

        teleportPlayer(reg, player, glm::vec3(5.0f, halfY, 15.0f));  // weapon_nailgun
        Input idle;
        for (int i = 0; i < 10; i++) { applyInput(reg, player, idle); qengine::stepSimulation(reg, jolt, level, dt); }

        const auto& inv = reg.get<WeaponInventory>(player);
        const auto& ammo = reg.get<Ammo>(player);
        bool hasNailgun = inv.owned[static_cast<int>(WeaponType::Nailgun)];

        bool pass = ammo.shells == shellsBefore          // shotgun ammo untouched
                 && ammo.nails > nailsBefore             // nails granted
                 && ownedCount(inv) == ownedBefore + 1   // exactly one new weapon owned
                 && hasNailgun
                 && inv.currentWeapon == currentBefore;  // no auto-switch

        char buf[220];
        std::snprintf(buf, sizeof(buf),
            "shells %d→%d (unchanged), nails %d→%d, owned %d→%d, current=%d",
            shellsBefore, ammo.shells, nailsBefore, ammo.nails,
            ownedBefore, ownedCount(inv), inv.currentWeapon);
        return report("weapon_pickup", pass, buf);
    }

    // A monster_grunt from the showcase: while alive it blocks the player, and it
    // can be shot dead (health drops, then the entity is removed).
    bool scenario_monster_grunt(entt::registry& reg, JoltWorld& jolt, const Level& level, float dt)
    {
        entt::entity player = findPlayer(reg);

        entt::entity grunt = entt::null;
        for (auto e : reg.view<AIState, Health, Position, AABBCollider>())
            { if (reg.any_of<RangedAttack>(e)) continue; grunt = e; break; }   // melee grunt only
        if (grunt == entt::null) return report("monster_grunt", false, "no monster_grunt spawned");

        glm::vec3 gpos = reg.get<Position>(grunt).value;
        float halfY = reg.get<AABBCollider>(player).halfExtents.y;

        // ── Collision: stand in front (+Z), push toward the grunt, expect blocked.
        glm::vec3 front = gpos + glm::vec3(0.0f, 0.0f, 1.4f); front.y = halfY + 0.05f;
        teleportPlayer(reg, player, front);
        Input push; push.wishDir = glm::vec3(0.0f, 0.0f, -1.0f);
        for (int i = 0; i < 90; i++) { applyInput(reg, player, push); qengine::stepSimulation(reg, jolt, level, dt); }
        float blockedZ = reg.get<Position>(player).value.z;
        // grunt front face ~ gpos.z + 0.4, player radius ~0.3 → stop near gpos.z+0.7;
        // must not tunnel to the far side (z < gpos.z).
        bool blocked = blockedZ > gpos.z + 0.5f;

        // ── Damage + death: stand back, aim, fire the shotgun until it dies.
        glm::vec3 stand = gpos + glm::vec3(0.0f, 0.0f, 3.0f); stand.y = halfY + 0.05f;
        teleportPlayer(reg, player, stand);
        glm::vec3 aim = glm::normalize(gpos - (stand + glm::vec3(0.0f, halfY * 0.7f, 0.0f)));

        Input idle; idle.lookDir = aim;
        for (int i = 0; i < 5; i++) { applyInput(reg, player, idle); qengine::stepSimulation(reg, jolt, level, dt); }

        float hpBefore = reg.get<Health>(grunt).current;
        float hpAfter1 = hpBefore;
        bool died = false;
        for (int shot = 0; shot < 12 && !died; ++shot)
        {
            Input fire; fire.lookDir = aim; fire.fire = true;
            applyInput(reg, player, fire);
            qengine::stepSimulation(reg, jolt, level, dt);
            if (shot == 0 && reg.valid(grunt)) hpAfter1 = reg.get<Health>(grunt).current;
            for (int i = 0; i < 40 && reg.valid(grunt); i++)
            { applyInput(reg, player, idle); qengine::stepSimulation(reg, jolt, level, dt); }
            died = !reg.valid(grunt);
        }

        bool damaged = hpAfter1 < hpBefore;
        char buf[220];
        std::snprintf(buf, sizeof(buf),
            "blockedZ=%.2f (grunt z=%.2f) blocked=%d; hp %.0f->%.0f died=%d",
            blockedZ, gpos.z, blocked ? 1 : 0, hpBefore, hpAfter1, died ? 1 : 0);
        return report("monster_grunt", blocked && damaged && died, buf);
    }

    // Enemy behaviour: no line-of-sight → stays put; open LoS → chases and
    // attacks (the player takes damage).
    bool scenario_monster_ai(entt::registry& reg, JoltWorld& jolt, const Level& level, float dt)
    {
        entt::entity player = findPlayer(reg);

        // The front grunt (lowest z) is at a known spawn (13, .95, 8).
        entt::entity grunt = entt::null; float bestZ = 1e9f;
        for (auto [e, ai, pos] : reg.view<AIState, Position>().each())
            if (!reg.any_of<RangedAttack>(e) && pos.value.z < bestZ) { bestZ = pos.value.z; grunt = e; }  // melee grunt only
        if (grunt == entt::null) return report("monster_ai", false, "no grunt");

        glm::vec3 gpos = reg.get<Position>(grunt).value;
        float halfY = reg.get<AABBCollider>(player).halfExtents.y;

        // ── Blocked LoS: hide behind the shelf; grunt must stay Idle + not move.
        teleportPlayer(reg, player, glm::vec3(24.0f, halfY + 0.05f, 5.0f));
        for (int i = 0; i < 60; i++) { applyInput(reg, player, Input{}); qengine::stepSimulation(reg, jolt, level, dt); }
        bool idleHidden = reg.get<AIState>(grunt).state == AIStateKind::Idle;
        float movedHidden = glm::length(reg.get<Position>(grunt).value - gpos);

        // ── Open LoS: stand ~10 units in front; grunt should close in.
        glm::vec3 seen = gpos + glm::vec3(0.0f, 0.0f, 10.0f); seen.y = halfY + 0.05f;
        teleportPlayer(reg, player, seen);
        float distStart = glm::length(reg.get<Position>(grunt).value - reg.get<Position>(player).value);
        bool sawChase = false;
        for (int i = 0; i < 150; i++)
        {
            applyInput(reg, player, Input{});
            qengine::stepSimulation(reg, jolt, level, dt);
            if (reg.valid(grunt) && reg.get<AIState>(grunt).state == AIStateKind::Chase) sawChase = true;
        }
        float distChase = glm::length(reg.get<Position>(grunt).value - reg.get<Position>(player).value);
        bool chased = sawChase && distChase < distStart - 1.0f;

        // ── Attack: keep still; the player should take damage.
        float hpStart = reg.get<Health>(player).current;
        for (int i = 0; i < 240; i++) { applyInput(reg, player, Input{}); qengine::stepSimulation(reg, jolt, level, dt); }
        bool attacked = reg.get<Health>(player).current < hpStart;

        char buf[240];
        std::snprintf(buf, sizeof(buf),
            "hidden idle=%d moved=%.2f; dist %.1f->%.1f chase=%d; attack=%d",
            idleHidden ? 1 : 0, movedHidden, distStart, distChase, chased ? 1 : 0, attacked ? 1 : 0);
        return report("monster_ai", idleHidden && movedHidden < 0.3f && chased && attacked, buf);
    }

    // Pathfinding: an aggroed grunt with the shelf between it and the player must
    // route AROUND the shelf (never through it) and reach attack range.
    bool scenario_monster_path(entt::registry& reg, JoltWorld& jolt, const Level& level, float dt)
    {
        entt::entity player = findPlayer(reg);
        entt::entity grunt = entt::null; float bestZ = 1e9f;
        for (auto [e, ai, pos] : reg.view<AIState, Position>().each())
            if (!reg.any_of<RangedAttack>(e) && pos.value.z < bestZ) { bestZ = pos.value.z; grunt = e; }  // melee grunt only
        if (grunt == entt::null) return report("monster_path", false, "no grunt");

        float halfY = reg.get<AABBCollider>(player).halfExtents.y;

        // Grunt just west of the shelf (shelf spans x[18,22] z[3,7]).
        teleportEnemy(reg, grunt, glm::vec3(16.0f, 0.95f, 5.0f));

        // Aggro it with an open line of sight to the north.
        teleportPlayer(reg, player, glm::vec3(16.0f, halfY + 0.05f, 10.0f));
        for (int i = 0; i < 40; i++) { applyInput(reg, player, Input{}); qengine::stepSimulation(reg, jolt, level, dt); }
        bool aggroed = reg.get<AIState>(grunt).target != entt::null;

        // Now hide the player on the far side of the shelf. The grunt is aggroed,
        // so it must path AROUND the shelf to reach the player.
        teleportPlayer(reg, player, glm::vec3(26.0f, halfY + 0.05f, 5.0f));

        bool enteredShelf = false;
        for (int i = 0; i < 600; i++)
        {
            applyInput(reg, player, Input{});
            qengine::stepSimulation(reg, jolt, level, dt);
            if (!reg.valid(grunt)) break;
            glm::vec3 gp = reg.get<Position>(grunt).value;
            if (gp.x > 18.0f && gp.x < 22.0f && gp.z > 3.0f && gp.z < 7.0f) enteredShelf = true;
        }

        float finalDist = reg.valid(grunt)
            ? glm::length(reg.get<Position>(grunt).value - reg.get<Position>(player).value) : 1e9f;
        bool reached = finalDist < 3.5f;

        char buf[220];
        std::snprintf(buf, sizeof(buf),
            "aggroed=%d; routed around shelf (no-clip=%d); final dist=%.1f reached=%d",
            aggroed ? 1 : 0, enteredShelf ? 0 : 1, finalDist, reached ? 1 : 0);
        return report("monster_path", aggroed && !enteredShelf && reached, buf);
    }

    // The player's own horizontal speed is clamped: injecting an absurd velocity
    // and holding a movement key must settle back under the cap (no runaway).
    bool scenario_speed_cap(entt::registry& reg, JoltWorld& jolt, const Level& level, float dt)
    {
        entt::entity player = findPlayer(reg);
        clearEnemies(jolt, reg);
        const float halfY = reg.get<AABBCollider>(player).halfExtents.y;

        teleportPlayer(reg, player, glm::vec3(8.0f, halfY, 15.0f));
        for (int i = 0; i < 30; i++) { applyInput(reg, player, Input{}); qengine::stepSimulation(reg, jolt, level, dt); }

        auto& ch = reg.get<JoltCharacter>(player).character;
        ch->SetLinearVelocity(JPH::Vec3(60.0f, 0.0f, 0.0f));   // absurd shove
        Input fwd; fwd.wishDir = glm::vec3(1, 0, 0); fwd.lookDir = glm::vec3(1, 0, 0);

        float peak = 0.0f;
        for (int i = 0; i < 30; i++)
        {
            applyInput(reg, player, fwd);   // hold forward (no friction path)
            qengine::stepSimulation(reg, jolt, level, dt);
            JPH::Vec3 v = ch->GetLinearVelocity();
            peak = std::max(peak, std::sqrt(v.GetX() * v.GetX() + v.GetZ() * v.GetZ()));
        }

        float cap = reg.get<CharacterPhysics>(player).maxHorizontalSpeed;
        char buf[180];
        std::snprintf(buf, sizeof(buf),
            "injected 60 u/s + held forward: peak horiz speed=%.1f (cap %.1f)", peak, cap);
        return report("speed_cap", peak <= cap + 1.0f, buf);
    }

    // Ranged enemy: aggros at distance, HOLDS standoff (doesn't rush to melee),
    // telegraphs, then fires dodgeable bolts that damage the player.
    bool scenario_monster_ranged(entt::registry& reg, JoltWorld& jolt, const Level& level, float dt)
    {
        entt::entity player = findPlayer(reg);

        // Isolate: drop the melee grunts so only the ranged enemy acts / blocks LoS.
        std::vector<entt::entity> melee;
        for (auto e : reg.view<AIState>()) if (!reg.any_of<RangedAttack>(e)) melee.push_back(e);
        for (auto e : melee) reg.destroy(e);

        entt::entity ranged = entt::null;
        for (auto e : reg.view<AIState, RangedAttack, Position>()) { ranged = e; break; }
        if (ranged == entt::null) return report("monster_ranged", false, "no ranged enemy");

        float halfY = reg.get<AABBCollider>(player).halfExtents.y;

        // Face-off in the clear x=16 lane: enemy south, player 10 units north — inside
        // range (16) but beyond standoffMin (7), so it should hold and shoot.
        teleportEnemy(reg, ranged, glm::vec3(16.0f, 0.95f, 8.0f));
        teleportPlayer(reg, player, glm::vec3(16.0f, halfY + 0.05f, 18.0f));

        float hpStart = reg.get<Health>(player).current;
        for (int i = 0; i < 40; i++) { applyInput(reg, player, Input{}); qengine::stepSimulation(reg, jolt, level, dt); }
        bool aggroed = reg.valid(ranged) && reg.get<AIState>(ranged).target != entt::null;

        bool sawAttack = false;
        for (int i = 0; i < 400; i++)
        {
            applyInput(reg, player, Input{});
            qengine::stepSimulation(reg, jolt, level, dt);
            if (reg.valid(ranged) && reg.get<AIState>(ranged).state == AIStateKind::Attack) sawAttack = true;
        }
        float hpEnd    = reg.get<Health>(player).current;
        bool  damaged  = hpEnd < hpStart;

        float finalDist  = reg.valid(ranged)
            ? glm::length(reg.get<Position>(ranged).value - reg.get<Position>(player).value) : 0.0f;
        float standoffMin = reg.valid(ranged) ? reg.get<RangedAttack>(ranged).standoffMin : 0.0f;
        bool  held        = finalDist > standoffMin;   // never closed to melee

        char buf[240];
        std::snprintf(buf, sizeof(buf),
            "aggroed=%d attack=%d; player hp %.0f->%.0f dmg=%d; final dist=%.1f (standoffMin=%.1f) held=%d",
            aggroed?1:0, sawAttack?1:0, hpStart, hpEnd, damaged?1:0, finalDist, standoffMin, held?1:0);
        return report("monster_ranged", aggroed && sawAttack && damaged && held, buf);
    }

    // Friendly-fire guard: an Enemy-faction projectile must NOT damage an enemy,
    // but a Player-faction one must. Tests updateProjectiles' faction check directly
    // (independent of AI aim). Also: a Player projectile must not damage the player.
    bool scenario_friendly_fire(entt::registry& reg, JoltWorld& jolt, const Level& level, float dt)
    {
        entt::entity player = findPlayer(reg);
        teleportPlayer(reg, player, glm::vec3(10.0f, 0.85f, 15.0f));

        // An INERT enemy-faction target (AIState marks the side) with no JoltCharacter
        // /AIPath, so aiSystem never moves it — a still, deterministic dummy. Placed in
        // the x=10 lane (walk_floor_seams confirms it's open floor, no wall to eat the
        // test projectile before it reaches the target).
        auto dummy = reg.create();
        reg.emplace<Position>(dummy, glm::vec3(10.0f, 0.95f, 20.0f));
        reg.emplace<AABBCollider>(dummy, glm::vec3(0.4f, 0.9f, 0.4f), false);
        reg.emplace<Health>(dummy, 100.0f, 100.0f, 0.0f);
        reg.emplace<AIState>(dummy);

        // Spawn a projectile overlapping `target` and let it resolve.
        auto fireAt = [&](entt::entity target, Faction faction) {
            glm::vec3 tp = reg.get<Position>(target).value;
            auto p = reg.create();
            reg.emplace<Position>(p, tp - glm::vec3(0.0f, 0.0f, 0.3f));
            reg.emplace<Velocity>(p, glm::vec3(0.0f, 0.0f, 2.0f));   // into the target
            reg.emplace<AABBCollider>(p, glm::vec3(0.15f), false);
            reg.emplace<Projectile>(p, 50.0f, 0.0f, 0.0f, entt::null, faction);
            reg.emplace<Lifetime>(p, 1.0f);
            for (int i = 0; i < 4; i++) { applyInput(reg, player, Input{}); qengine::stepSimulation(reg, jolt, level, dt); }
        };

        float dEnemyBefore = reg.get<Health>(dummy).current;
        fireAt(dummy, Faction::Enemy);                       // same side → no damage
        float dAfterEnemy  = reg.valid(dummy) ? reg.get<Health>(dummy).current : -1.0f;

        fireAt(dummy, Faction::Player);                      // opposite side → damage
        float dAfterPlayer = reg.valid(dummy) ? reg.get<Health>(dummy).current : -1.0f;

        float pBefore = reg.get<Health>(player).current;
        fireAt(player, Faction::Player);                     // own side → no self-damage
        float pAfter  = reg.get<Health>(player).current;

        bool enemyBoltSpared  = dAfterEnemy  == dEnemyBefore;   // enemy bolt didn't hurt enemy
        bool playerBoltHit    = dAfterPlayer <  dAfterEnemy;    // player bolt hurt enemy
        bool playerSelfSpared = pAfter       == pBefore;        // player bolt didn't hurt player

        char buf[240];
        std::snprintf(buf, sizeof(buf),
            "dummy hp %.0f -(enemy bolt)-> %.0f -(player bolt)-> %.0f; player %.0f->%.0f; spared=%d hit=%d selfSpared=%d",
            dEnemyBefore, dAfterEnemy, dAfterPlayer, pBefore, pAfter,
            enemyBoltSpared?1:0, playerBoltHit?1:0, playerSelfSpared?1:0);
        return report("friendly_fire", enemyBoltSpared && playerBoltHit && playerSelfSpared, buf);
    }

    // HUD signal state (ctx, testable headless): crosshair spread widens with
    // movement + firing, low-ammo flag, hit/kill markers on shooting an enemy, and
    // the damage-direction timer when the player is hit.
    bool scenario_hud_signals(entt::registry& reg, JoltWorld& jolt, const Level& level, float dt)
    {
        entt::entity player = findPlayer(reg);
        const HudSignals& hud = reg.ctx().get<HudSignals>();
        float halfY = reg.get<AABBCollider>(player).halfExtents.y;
        clearEnemies(jolt, reg);   // isolate crosshair/marker measurements

        auto idle = [&]{ applyInput(reg, player, Input{}); qengine::stepSimulation(reg, jolt, level, dt); };

        // Baseline gap (idle, settled) in the open x=10 lane.
        teleportPlayer(reg, player, glm::vec3(10.0f, halfY, 15.0f));
        for (int i = 0; i < 40; i++) idle();
        float restGap = hud.crosshairGap;

        // Movement widens the crosshair.
        Input walk; walk.wishDir = glm::vec3(0, 0, 1); walk.lookDir = glm::vec3(0, 0, 1);
        for (int i = 0; i < 25; i++) { applyInput(reg, player, walk); qengine::stepSimulation(reg, jolt, level, dt); }
        float moveGap = hud.crosshairGap;
        for (int i = 0; i < 40; i++) idle();   // settle

        // Firing widens it (recoil kick), then it decays back.
        Input fire; fire.fire = true; fire.lookDir = glm::vec3(0, 0, -1);
        applyInput(reg, player, fire); qengine::stepSimulation(reg, jolt, level, dt);
        float fireGap = hud.crosshairGap;
        for (int i = 0; i < 40; i++) idle();

        // Low-ammo flag flips at the threshold.
        reg.get<Ammo>(player).shells = 2;  idle();  bool lowSet   = hud.lowAmmo;
        reg.get<Ammo>(player).shells = 25; idle();  bool lowClear = !hud.lowAmmo;

        // Hit + kill markers: shoot a stationary dummy enemy dead.
        auto dummy = reg.create();
        reg.emplace<Position>(dummy, glm::vec3(10.0f, 0.95f, 20.0f));
        reg.emplace<AABBCollider>(dummy, glm::vec3(0.4f, 0.9f, 0.4f), false);
        reg.emplace<Health>(dummy, 30.0f, 30.0f, 0.0f);
        reg.emplace<AIState>(dummy);
        teleportPlayer(reg, player, glm::vec3(10.0f, halfY, 15.0f));
        glm::vec3 eye = glm::vec3(10.0f, halfY, 15.0f) + glm::vec3(0.0f, halfY * 0.7f, 0.0f);
        glm::vec3 aim = glm::normalize(glm::vec3(10.0f, 0.95f, 20.0f) - eye);
        bool sawHit = false, sawKill = false;
        for (int shot = 0; shot < 8 && !sawKill; ++shot)
        {
            Input f; f.lookDir = aim; f.fire = true;
            applyInput(reg, player, f); qengine::stepSimulation(reg, jolt, level, dt);
            if (hud.hitMarkerTimer  > 0.0f) sawHit  = true;
            if (hud.killMarkerTimer > 0.0f) sawKill = true;
            for (int i = 0; i < 40 && reg.valid(dummy); i++)
            { Input a; a.lookDir = aim; applyInput(reg, player, a); qengine::stepSimulation(reg, jolt, level, dt); }
        }

        // Damage-direction: an Enemy-faction bolt strikes the player.
        glm::vec3 pp = reg.get<Position>(player).value;
        auto bolt = reg.create();
        reg.emplace<Position>(bolt, pp - glm::vec3(0.0f, 0.0f, 0.3f));
        reg.emplace<Velocity>(bolt, glm::vec3(0.0f, 0.0f, 2.0f));
        reg.emplace<AABBCollider>(bolt, glm::vec3(0.15f), false);
        reg.emplace<Projectile>(bolt, 5.0f, 0.0f, 0.0f, entt::null, Faction::Enemy);
        reg.emplace<Lifetime>(bolt, 1.0f);
        for (int i = 0; i < 4; i++) idle();
        bool dmgDir = hud.damageDirTimer > 0.0f;

        bool pass = moveGap > restGap + 0.5f && fireGap > restGap + 1.0f
                 && lowSet && lowClear && sawHit && sawKill && dmgDir;
        char buf[260];
        std::snprintf(buf, sizeof(buf),
            "gap rest=%.1f move=%.1f fire=%.1f; low set=%d clear=%d; hit=%d kill=%d; dmgDir=%d",
            restGap, moveGap, fireGap, lowSet?1:0, lowClear?1:0, sawHit?1:0, sawKill?1:0, dmgDir?1:0);
        return report("hud_signals", pass, buf);
    }

    // Brush entities loaded from a `.map` (assets/maps/brush_entities.map, built
    // by main for this scenario). Proves the loader's brush-entity path end-to-
    // end: a func_door drawn as a brush gets a Mover + a kinematic collider body
    // (it blocks), the linked trigger_multiple opens it (it travels toward
    // endPos), and a trigger_hurt lava brush damages the player. The scenario
    // finds every entity by component, so it doesn't hard-code map coordinates.
    bool scenario_map_brush_entities(entt::registry& reg, JoltWorld& jolt, const Level& level, float dt)
    {
        entt::entity player = findPlayer(reg);
        if (player == entt::null) return report("map_brush_entities", false, "no player from map");

        // The door mover + the trigger that activates it, and the hurt volume.
        entt::entity door = entt::null, doorTrig = entt::null, hurtTrig = entt::null;
        for (auto [e, tv] : reg.view<TriggerVolume>().each())
        {
            if (tv.action == TriggerAction::ActivateMover && tv.target != entt::null)
                { doorTrig = e; door = tv.target; }
            if (tv.action == TriggerAction::Damage) hurtTrig = e;
        }
        if (door == entt::null || doorTrig == entt::null)
            return report("map_brush_entities", false, "no func_door + trigger link built from map");
        if (hurtTrig == entt::null)
            return report("map_brush_entities", false, "no trigger_hurt built from map");

        // (collider) the door has a kinematic Jolt body — it blocks the player.
        bool doorHasBody = reg.all_of<JoltBody>(door) && reg.all_of<Mover>(door);

        // (blocks) before triggering, the door sits closed at its start position.
        const Mover& m0 = reg.get<Mover>(door);
        const glm::vec3 startPos = m0.startPos, endPos = m0.endPos;
        const float distStartToEnd = glm::length(endPos - startPos);
        bool closedAtStart = m0.state == MoverState::Idle
            && glm::length(reg.get<Position>(door).value - startPos) < 0.01f;

        // (opens) pin the player inside the door trigger; the mover should leave
        // Idle and travel toward endPos.
        const glm::vec3 trigCentre = reg.get<Position>(doorTrig).value;
        bool leftIdle = false;
        float bestProgress = 0.0f;
        for (int i = 0; i < 180; i++)
        {
            teleportPlayer(reg, player, trigCentre);   // hold inside the trigger AABB
            applyInput(reg, player, Input{});
            qengine::stepSimulation(reg, jolt, level, dt);
            const Mover& m = reg.get<Mover>(door);
            if (m.state != MoverState::Idle) leftIdle = true;
            bestProgress = std::max(bestProgress, m.progress);
        }
        const float distToEnd = glm::length(reg.get<Position>(door).value - endPos);
        bool opened = leftIdle && bestProgress > 0.5f
                   && distToEnd < distStartToEnd * 0.5f && distStartToEnd > 0.01f;

        // (hurt) pin the player in the lava volume; health must drop.
        const glm::vec3 hurtCentre = reg.get<Position>(hurtTrig).value;
        reg.get<Health>(player).current = 100.0f;
        const float hpBefore = reg.get<Health>(player).current;
        for (int i = 0; i < 60; i++)
        {
            teleportPlayer(reg, player, hurtCentre);
            applyInput(reg, player, Input{});
            qengine::stepSimulation(reg, jolt, level, dt);
        }
        const float hpAfter = reg.get<Health>(player).current;
        bool hurt = hpAfter < hpBefore;

        bool pass = doorHasBody && closedAtStart && opened && hurt;
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "door body=%d closed=%d; opened=%d (progress=%.2f distToEnd=%.2f/%.2f); hurt=%d (hp %.0f->%.0f)",
            doorHasBody?1:0, closedAtStart?1:0, opened?1:0, bestProgress, distToEnd, distStartToEnd,
            hurt?1:0, hpBefore, hpAfter);
        return report("map_brush_entities", pass, buf);
    }

}

int main(int argc, char** argv)
{
    std::string scenario = (argc > 1) ? argv[1] : "ride_lift_up";
    std::string mapArg = (argc > 2) ? argv[2] : "assets/maps/smoke.map";  // map_file path

    // Fully headless: no window, no GL context. loadResources caches GL-free
    // stubs and buildWorld skips render-mesh building, so this runs on a box
    // with no GPU/driver (CI-friendly). Physics is unaffected.
    ResourceManager resources;
    qengine::loadResources(resources, /*headless=*/true);

    entt::registry registry;
    auto& cfg = registry.ctx().emplace<PhysicsConfig>();
    auto& jolt = registry.ctx().emplace<JoltWorld>();
    jolt.init(/*singleThreaded=*/true, cfg.gravity);   // deterministic

    // Most scenarios run against the hard-coded showcase (empty path). The
    // map-driven behavioural scenario builds its world from a dedicated fixture
    // `.map` so it exercises the loader's brush-entity path, not the C++ scene.
    std::string worldMap = (scenario == "map_brush_entities")
        ? std::string("assets/maps/brush_entities.map") : std::string();
    Level level = qengine::buildWorld(registry, resources, jolt, /*headless=*/true, worldMap);
    float dt = cfg.fixedDeltaTime;

    std::cout << "── headless scenario: " << scenario << " ──" << std::endl;

    bool pass;
    if      (scenario == "rest_no_jitter")   pass = scenario_rest_no_jitter(registry, jolt, level, dt);
    else if (scenario == "ride_lift_up")     pass = scenario_ride_lift_up(registry, jolt, level, dt);
    else if (scenario == "walk_onto_lift")   pass = scenario_walk_onto_lift(registry, jolt, level, dt);
    else if (scenario == "walk_floor_seams") pass = scenario_walk_floor_seams(registry, jolt, level, dt);
    else if (scenario == "rocket_vs_floor")  pass = scenario_rocket_vs_floor(registry, jolt, level, dt);
    else if (scenario == "teleporter")       pass = scenario_teleporter(registry, jolt, level, dt);
    else if (scenario == "spawn_counts")     pass = scenario_spawn_counts(registry, jolt, level, dt);
    else if (scenario == "pickup_health")    pass = scenario_pickup_health(registry, jolt, level, dt);
    else if (scenario == "ammo_shells")      pass = scenario_ammo_shells(registry, jolt, level, dt);
    else if (scenario == "armor_absorb")     pass = scenario_armor_absorb(registry, jolt, level, dt);
    else if (scenario == "weapon_pickup")    pass = scenario_weapon_pickup(registry, jolt, level, dt);
    else if (scenario == "monster_grunt")    pass = scenario_monster_grunt(registry, jolt, level, dt);
    else if (scenario == "monster_ai")       pass = scenario_monster_ai(registry, jolt, level, dt);
    else if (scenario == "monster_path")     pass = scenario_monster_path(registry, jolt, level, dt);
    else if (scenario == "monster_ranged")   pass = scenario_monster_ranged(registry, jolt, level, dt);
    else if (scenario == "friendly_fire")    pass = scenario_friendly_fire(registry, jolt, level, dt);
    else if (scenario == "hud_signals")      pass = scenario_hud_signals(registry, jolt, level, dt);
    else if (scenario == "speed_cap")        pass = scenario_speed_cap(registry, jolt, level, dt);
    else if (scenario == "map_parse")        pass = mapscenarios::scenarioMapParse();
    else if (scenario == "map_file")         pass = mapscenarios::scenarioMapFile(mapArg);
    else if (scenario == "map_scene")        pass = mapscenarios::scenarioMapScene();
    else if (scenario == "map_ground")       pass = mapscenarios::scenarioMapGroundPlacement(mapArg);
    else if (scenario == "brush_geometry")   pass = mapscenarios::scenarioBrushGeometry();
    else if (scenario == "map_brush_entities") pass = scenario_map_brush_entities(registry, jolt, level, dt);
    else { std::cerr << "unknown scenario: " << scenario << std::endl; pass = false; }

    // Destroy entities (and their components) BEFORE the physics system: enemy
    // JoltCharacters remove their inner Jolt body in ~CharacterVirtual, which must
    // run while jolt is still alive (else a use-after-free segfault at exit).
    registry.clear();
    jolt.shutdown();
    resources.clear();
    return pass ? 0 : 1;
}
