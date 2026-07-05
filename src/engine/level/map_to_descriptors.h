#pragma once

#include <vector>

#include "engine/level/types/map_data.h"
#include "engine/level/types/spawn_params.h"

// Translate a parsed .map's entities into the SpawnParams the existing entity
// factories consume. worldspawn is skipped (it becomes level geometry). Point
// entities read `origin`; brush entities (doors/lifts/triggers) derive origin +
// size from their brush AABB. Spatial props read back by factories (`endpos`,
// `velocity`, `direction`) are converted into engine space here; the result is
// fed straight to factories::spawnScene for classname dispatch + target linking.
std::vector<factories::SpawnParams> mapEntitiesToDescriptors(const qmap::MapData& map);
