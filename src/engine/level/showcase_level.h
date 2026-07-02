#pragma once

#include "engine/level/level.h"

// Builds the showcase room geometry. When `headless` is true the per-sector GL
// render meshes are NOT built (buildSectorMeshes is skipped) — physics bodies
// come from the surface geometry, not the meshes, so the simulation is
// unaffected and no GL context is required.
Level createShowcaseLevel(bool headless = false);