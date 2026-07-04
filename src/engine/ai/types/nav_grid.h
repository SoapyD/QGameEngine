#pragma once

#include <glm/glm.hpp>

#include <cmath>
#include <cstdint>
#include <vector>

// A 2D walkability grid over the level's XZ bounds, used by enemy pathfinding.
// Cell (cx, cz) covers [origin.x + cx*cell, +cell) × [origin.z + cz*cell, +cell);
// `blocked[cz*cols + cx]` is 1 where a wall/prop (inflated by enemy clearance)
// sits. Built once in buildWorld (rebuild when the level changes).
struct NavGrid
{
    glm::vec3 origin{ 0.0f };          // min-corner world position (origin.y = walk height)
    float cell = 1.0f;
    int cols = 0;                      // cells along +X
    int rows = 0;                      // cells along +Z
    std::vector<uint8_t> blocked;      // cols*rows, 1 = blocked

    bool inBounds(int cx, int cz) const { return cx >= 0 && cz >= 0 && cx < cols && cz < rows; }
    int  index(int cx, int cz)   const { return cz * cols + cx; }

    // Out-of-bounds counts as blocked (so search + snap stay inside the grid).
    bool isBlocked(int cx, int cz) const
    {
        return !inBounds(cx, cz) || blocked[index(cx, cz)] != 0;
    }

    void cellOf(const glm::vec3& p, int& cx, int& cz) const
    {
        cx = (int)std::floor((p.x - origin.x) / cell);
        cz = (int)std::floor((p.z - origin.z) / cell);
    }

    glm::vec3 center(int cx, int cz) const
    {
        return { origin.x + (cx + 0.5f) * cell, origin.y, origin.z + (cz + 0.5f) * cell };
    }
};
