#pragma once
// Two-pass scene spawn: build every entity from its descriptor, then resolve
// target/targetname links (trigger → mover, teleport → destination) once all
// names exist. This is the load path the .map parser feeds SpawnParams into.

#include <vector>

#include <entt/entt.hpp>

#include "engine/level/types/spawn_params.h"

namespace factories
{
    // Spawn all `descriptors`, then link `target`→`targetname`. Returns the
    // spawned entities in descriptor order (entt::null where a classname was
    // unknown). Must run before buildWorld()'s mover view so movers still get
    // their kinematic bodies.
    std::vector<entt::entity> spawnScene(entt::registry& reg, const SpawnContext& ctx,
                                         const std::vector<SpawnParams>& descriptors);
}
