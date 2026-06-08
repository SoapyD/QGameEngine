#pragma once

#include <entt/entt.hpp>
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
    // Requires a current GL context (real or hidden window).
    void loadResources(ResourceManager& resources);

    // Build the full world: showcase scene entities, Jolt static/kinematic/
    // sensor bodies, the player CharacterVirtual, and broad-phase optimisation.
    // `joltWorld` must already be init()'d and PhysicsConfig must be in the
    // registry context.
    Level buildWorld(entt::registry& registry, ResourceManager& resources, JoltWorld& joltWorld);

    // Run ONE fixed-timestep tick: the full ordered system pipeline.
    // The caller is responsible for having written PlayerInput and the camera
    // direction (ctx glm::vec3) for this tick before calling.
    void stepSimulation(entt::registry& registry, JoltWorld& joltWorld, const Level& level, float dt);
}
