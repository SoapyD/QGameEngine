#pragma once
// Coordinate/scale conversion between TrenchBroom `.map` space and engine space,
// kept in ONE place (per the note in types/map_data.h). Every brush vertex,
// entity origin, and vector-valued property crosses this boundary exactly once.
//
//   TrenchBroom / Quake : X right, Y forward, Z up   (Z-up), integer Quake units
//   QEngine             : X right, Y up,      Z back  (Y-up), small float units
//
// Axis map preserves right-handedness: engine = { m.x, m.z, -m.y } / scale.

#include <glm/glm.hpp>

#include <cmath>

namespace qmap
{
    // One engine unit spans this many Quake/TB units. The showcase room is ~30
    // engine units; smoke.map's ~256-unit room becomes ~8 engine units at 32.
    // Single knob — bump down (e.g. 16) if maps feel cramped for the player scale.
    inline constexpr float kMapUnitsPerEngineUnit = 32.0f;

    // A world POSITION: axis-swap + scale.
    inline glm::vec3 mapPointToEngine(glm::vec3 m)
    {
        return glm::vec3(m.x, m.z, -m.y) / kMapUnitsPerEngineUnit;
    }

    // A DIRECTION (sun vector, etc.): axis-swap only, no scale; caller normalises.
    inline glm::vec3 mapDirToEngine(glm::vec3 d)
    {
        return glm::vec3(d.x, d.z, -d.y);
    }

    // Full EXTENTS (a size, not a point): axis-swap the magnitudes, scale, keep
    // positive. Used for brush-entity collider sizes.
    inline glm::vec3 mapExtentToEngine(glm::vec3 e)
    {
        return glm::abs(glm::vec3(e.x, e.z, e.y)) / kMapUnitsPerEngineUnit;
    }

    // Quake yaw (degrees about Z-up, 0=+X, 90=+Y) → engine yaw about Y-up.
    // NOTE: the current spawnPlayer ignores angle, so this only matters once a
    // factory consumes facing; provided for completeness.
    inline float mapAngleToEngineYaw(float degrees)
    {
        return -degrees;
    }
}
