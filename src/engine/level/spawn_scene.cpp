#include "engine/level/spawn_scene.h"

#include "engine/level/classname_factory.h"
#include "engine/ecs/components.h"   // TriggerVolume, TriggerAction, Position

#include <cstddef>
#include <iostream>
#include <string>
#include <unordered_map>

namespace factories
{
    std::vector<entt::entity> spawnScene(entt::registry& reg, const SpawnContext& ctx,
                                         const std::vector<SpawnParams>& descriptors)
    {
        std::vector<entt::entity> spawned;
        spawned.reserve(descriptors.size());
        std::unordered_map<std::string, entt::entity> byName;

        // Pass 1 — spawn everything, index the named entities.
        for (const auto& d : descriptors)
        {
            entt::entity e = spawnByClassname(reg, ctx, d);
            spawned.push_back(e);
            if (e != entt::null && !d.targetname.empty())
                byName[d.targetname] = e;
        }

        // Pass 2 — resolve target links now that every name is known.
        for (std::size_t i = 0; i < descriptors.size(); ++i)
        {
            const SpawnParams& d = descriptors[i];
            entt::entity e = spawned[i];
            if (e == entt::null || d.target.empty()) continue;

            auto it = byName.find(d.target);
            if (it == byName.end())
            {
                std::cerr << "[spawnScene] '" << d.classname << "' targets unknown name '"
                          << d.target << "'\n";
                continue;
            }
            entt::entity target = it->second;

            // Only triggers carry links today.
            TriggerVolume* tv = reg.try_get<TriggerVolume>(e);
            if (!tv) continue;

            if (tv->action == TriggerAction::Teleport)
            {
                if (const Position* pos = reg.try_get<Position>(target))
                    tv->destination = pos->value;
            }
            else
            {
                tv->target = target;  // ActivateMover (door/lift), etc.
            }
        }

        return spawned;
    }
}
