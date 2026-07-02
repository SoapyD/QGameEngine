#pragma once

#include "engine/level/level.h"

// Build the GPU render mesh for every sector in a level (one mesh per sector,
// quads → triangles). LIVE — called by createShowcaseLevel(). Extracted from
// the (legacy) level_loader so it no longer sits beside dead .qlvl code.
void buildSectorMeshes(Level& level);
