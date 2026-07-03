#pragma once
// Item/weapon pickup classnames for the dispatch table. Split out of
// classname_factory.cpp so the growing pickup roster (one entry per item and
// weapon type) doesn't bloat the core dispatch file.

#include <string>
#include <unordered_map>

#include "engine/level/types/spawn_params.h"   // SpawnFn

namespace factories
{
    // Add the item_* / weapon_* pickup classnames to `table`.
    void registerItemClassnames(std::unordered_map<std::string, SpawnFn>& table);
}
