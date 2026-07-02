#pragma once
// classname → factory dispatch. Looks up a SpawnParams' classname and builds the
// full entity by delegating to the typed factories in factories.h. This is the
// layer the .map loader maps FGD `classname`s onto (Plan 03 step 2.3).

#include <entt/entt.hpp>

#include "engine/level/types/spawn_params.h"

namespace factories
{
    // Build the entity for `p.classname`. Returns entt::null for an unknown
    // classname (logged to stderr) so the caller can keep spawning the rest.
    entt::entity spawnByClassname(entt::registry& reg, const SpawnContext& ctx,
                                  const SpawnParams& p);
}
