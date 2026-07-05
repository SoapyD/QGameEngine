#pragma once

#include <entt/entt.hpp>
#include <string>
#include "engine/level/level.h"

// Shared simulation entry points used by BOTH the windowed game (main.cpp)
// and the headless harness (headless_main.cpp). Keeping world construction
// and the fixed tick in one place guarantees the harness exercises the exact
// same systems, in the exact same order, as the real game.

class ResourceManager;
struct JoltWorld;

namespace qengine
{
    // Load every shader/texture/mesh the showcase scene needs into the cache.
    // With headless=false this needs a current GL context (real or hidden
    // window). With headless=true it instead caches GL-free stubs (handles 0),
    // so the harness runs with no GL context and no GPU.
    void loadResources(ResourceManager& resources, bool headless = false);

    // Build the full world: showcase scene entities, Jolt static/kinematic/
    // sensor bodies, the player CharacterVirtual, and broad-phase optimisation.
    // `joltWorld` must already be init()'d and PhysicsConfig must be in the
    // registry context. `headless` skips building GL render meshes for the level.
    // `mapPath` empty → the hard-coded showcase; non-empty → load that .map.
    Level buildWorld(entt::registry& registry, ResourceManager& resources,
                     JoltWorld& joltWorld, bool headless = false,
                     const std::string& mapPath = "");

    // Run ONE fixed-timestep tick: the full ordered system pipeline.
    // The caller is responsible for having written PlayerInput and the camera
    // direction (ctx glm::vec3) for this tick before calling.
    void stepSimulation(entt::registry& registry, JoltWorld& joltWorld, const Level& level, float dt);
}
