#pragma once
// TrenchBroom `.map` parser — step 2.1 of the engine-loader plan
// (docs/plans/2026-07-03_trenchbroom_engine-loader.md). Turns Standard-format
// `.map` text into a qmap::MapData (entities → brushes → faces). This is the
// text→struct front end ONLY: it does no geometry building, no coordinate
// conversion, and no entity spawning — those are steps 2.2–2.4.

#include "engine/level/types/map_data.h"

#include <string>

namespace qmap
{
    // Parse `.map` text already in memory. On a syntax error, returns an empty
    // MapData and, if `error` is non-null, writes a human-readable reason (with a
    // 1-based line number). Comments (`//` to end of line) and whitespace are
    // ignored. Valve-220 face format (extra texture axes) is NOT parsed — the
    // QEngine game config emits Standard format.
    MapData parseMapString(const std::string& text, std::string* error = nullptr);

    // Read + parse a `.map` file from disk. Returns empty (and sets `error`) if
    // the file can't be opened or fails to parse.
    MapData loadMapFile(const std::string& path, std::string* error = nullptr);
}
