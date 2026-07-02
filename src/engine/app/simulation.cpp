#include "engine/app/simulation.h"

#include "engine/core/resource_manager.h"
#include "engine/ecs/components.h"
#include "engine/physics/jolt_bodies.h"
#include "engine/app/scene_setup.h"
#include "engine/ecs/systems/combat/combat_system.h"
#include "engine/ecs/systems/demo/demo_reset_system.h"
#include "engine/ecs/systems/sync/jolt_sync_system.h"
#include "engine/ecs/systems/lifetime/lifetime_system.h"
#include "engine/ecs/systems/player/player_character_system.h"
#include "engine/ecs/systems/player/player_death_system.h"
#include "engine/ecs/systems/sync/mover_sync_system.h"
#include "engine/ecs/systems/mover/mover_system.h"
#include "engine/ecs/systems/trigger/trigger_system.h"
#include "engine/ecs/systems/combat/weapon_switch_system.h"
#include "engine/physics/jolt_world.h"

namespace qengine
{
    void loadResources(ResourceManager& resources, bool headless)
    {
        if (headless)
        {
            // GL-free stubs: scene_setup still fetches handles by name, but the
            // ids are 0 and nothing is ever drawn. No GL context required.
            for (const char* n : { "basic", "hud", "textured", "lit" })
                resources.storeShader(n, std::make_shared<Shader>(nullptr));
            for (const char* n : { "wall", "grid_grey", "grid_orange",
                                   "grid_blue", "grid_green", "grid_red" })
                resources.storeTexture(n, std::make_shared<Texture>(nullptr));
            resources.storeMesh("cube", std::make_shared<Mesh>(nullptr));
            return;
        }

        resources.getShader("basic",
            "assets/shaders/basic.vert", "assets/shaders/basic.frag");
        resources.getShader("hud",
            "assets/shaders/hud.vert", "assets/shaders/hud.frag");
        resources.getShader("textured",
            "assets/shaders/textured.vert", "assets/shaders/textured.frag");
        resources.getShader("lit",
            "assets/shaders/lit.vert", "assets/shaders/lit.frag");

        resources.getTexture("wall",       "assets/textures/wall.png");
        resources.getTexture("grid_grey",  "assets/textures/grid_grey.png");
        resources.getTexture("grid_orange","assets/textures/grid_orange.png");
        resources.getTexture("grid_blue",  "assets/textures/grid_blue.png");
        resources.getTexture("grid_green", "assets/textures/grid_green.png");
        resources.getTexture("grid_red",   "assets/textures/grid_red.png");

        resources.getMesh("cube", "assets/models/cube.obj");
    }

    Level buildWorld(entt::registry& registry, ResourceManager& resources,
                     JoltWorld& joltWorld, bool headless)
    {
        Level level = setupScene(registry, resources, headless);

        // Static bodies from level geometry
        createLevelBodies(registry, level);
        joltWorld.physicsSystem->OptimizeBroadPhase();

        // Kinematic bodies for movers (lifts, doors)
        auto moverView = registry.view<Position, AABBCollider, Mover>();
        for (auto [entity, pos, col, mover] : moverView.each())
        {
            createKinematicBody(registry, entity);
        }

        // NOTE: triggers use ECS AABB overlap in triggerSystem, not Jolt
        // sensor queries — so we deliberately do NOT create Jolt sensor bodies
        // here. They were inert (never queried) and would have been spuriously
        // hit by combat's AddImpulse sweep. (eval 05 §8 / 08 §8.5)

        // Player CharacterVirtual — must come after level bodies exist
        initPlayerCharacter(registry);

        joltWorld.physicsSystem->OptimizeBroadPhase();

        return level;
    }

    void stepSimulation(entt::registry& registry, JoltWorld& joltWorld, const Level& level, float dt)
    {
        // Tick order (eval 05 §2 / 08 §8.1 fix):
        // Movers animate and their kinematic bodies are swept by the physics
        // step BEFORE the player's CharacterVirtual ExtendedUpdate, so the
        // player resolves collision against the lift's CURRENT position rather
        // than its previous-tick position. This removes the boarding desync.
        weaponSwitchSystem(registry);
        moverSystem(registry);          // animate doors/lifts
        moverSyncSystem(registry);      // push mover targets to Jolt
        joltWorld.step(dt);             // sweep kinematic + dynamic bodies
        joltSyncSystem(registry);       // read body transforms back to ECS
        playerCharacterSystem(registry);// player resolves against moved world
        combatSystem(registry, level);
        lifetimeSystem(registry);
        triggerSystem(registry);
        playerDeathSystem(registry);
        demoResetSystem(registry);
    }
}
