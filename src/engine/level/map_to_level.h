#pragma once

#include "engine/level/level.h"
#include "engine/level/types/map_data.h"

// Build a Level (one sector of axis-aligned quad Surfaces) from a parsed .map's
// worldspawn brushes at MVP fidelity: each brush is represented by its
// axis-aligned bounding box in engine space. Lossless for axis-aligned box
// brushes (angled brushes collapse to their AABB — general brush geometry is a
// documented follow-up). Coordinate/scale conversion goes through map_transform.h.
//
// Surfaces (not meshes) are produced here: they feed collision (createLevelBodies)
// and the nav grid directly, and drive the render-mesh build. GL meshes are built
// separately (build_textured_meshes), only when a GL context exists.
Level mapWorldspawnToLevel(const qmap::MapData& map);
