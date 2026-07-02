#pragma once
// The hard-coded showcase expressed as spawn descriptors — the same shape a
// parsed .map will emit. Feeding this through factories::spawnScene must
// reproduce the C++-built showcase exactly (regression-guarded by the harness).
// It stands in for parser output until the .map loader (Plan 03) lands.

#include <vector>

#include "engine/level/types/spawn_params.h"

std::vector<factories::SpawnParams> showcaseDescriptors();
