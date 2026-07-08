#pragma once

#include "engine/level/types/map_data.h"
#include "engine/level/types/brush_geometry.h"

// Resolve a convex brush (the intersection of its face half-spaces) into explicit
// geometry via plane∩plane∩plane — the "general geometry" upgrade over the MVP's
// brush→AABB→6-quads approximation. Handles arbitrary angled brushes (ramps,
// wedges) losslessly. Output is in ENGINE space (mapPointToEngine applied).

namespace qmap
{
    // Build the geometry for one brush. Returns empty faces/vertices for a
    // degenerate or unbounded brush (fewer than 4 faces, or no valid corners) —
    // the caller should skip such a brush.
    BrushGeometry buildBrushGeometry(const MapBrush& brush);
}
