#include "engine/ai/find_path.h"
#include "engine/ai/types/nav_grid.h"

#include <algorithm>
#include <cmath>
#include <queue>
#include <utility>
#include <vector>

namespace
{
    constexpr float kDiag = 1.41421356f;
    constexpr int   kMaxExpanded = 4000;   // node budget — bail on pathological maps

    // Nudge a blocked cell to the nearest open one within a few rings.
    void snapToOpen(const NavGrid& g, int& cx, int& cz)
    {
        if (!g.isBlocked(cx, cz)) return;
        for (int r = 1; r <= 4; ++r)
            for (int dz = -r; dz <= r; ++dz)
                for (int dx = -r; dx <= r; ++dx)
                {
                    if (std::abs(dx) != r && std::abs(dz) != r) continue;  // ring only
                    if (!g.isBlocked(cx + dx, cz + dz)) { cx += dx; cz += dz; return; }
                }
    }
}

std::vector<glm::vec3> findPath(const NavGrid& grid, const glm::vec3& from, const glm::vec3& to)
{
    if (grid.cols <= 0 || grid.rows <= 0) return {};

    int sx, sz, gx, gz;
    grid.cellOf(from, sx, sz);
    grid.cellOf(to,   gx, gz);
    snapToOpen(grid, sx, sz);
    snapToOpen(grid, gx, gz);
    if (grid.isBlocked(sx, sz) || grid.isBlocked(gx, gz)) return {};
    if (sx == gx && sz == gz) return {};

    const int   n     = grid.cols * grid.rows;
    const int   start = grid.index(sx, sz);
    const int   goal  = grid.index(gx, gz);

    std::vector<float>   gScore(n, 1e18f);
    std::vector<int>     cameFrom(n, -1);
    std::vector<uint8_t> closed(n, 0);

    auto heuristic = [&](int x, int z)
    {
        float dx = (float)std::abs(x - gx), dz = (float)std::abs(z - gz);
        return (dx + dz) + (kDiag - 2.0f) * std::min(dx, dz);   // octile distance
    };

    using PQItem = std::pair<float, int>;   // (fScore, cellIndex)
    std::priority_queue<PQItem, std::vector<PQItem>, std::greater<PQItem>> open;
    gScore[start] = 0.0f;
    open.push({ heuristic(sx, sz), start });

    static const int dxs[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
    static const int dzs[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };

    int expanded = 0;
    bool found = false;
    while (!open.empty() && expanded < kMaxExpanded)
    {
        int cur = open.top().second; open.pop();
        if (closed[cur]) continue;
        closed[cur] = 1;
        ++expanded;
        if (cur == goal) { found = true; break; }

        int cx = cur % grid.cols, cz = cur / grid.cols;
        for (int i = 0; i < 8; ++i)
        {
            int nx = cx + dxs[i], nz = cz + dzs[i];
            if (grid.isBlocked(nx, nz)) continue;
            bool diag = dxs[i] != 0 && dzs[i] != 0;
            if (diag && (grid.isBlocked(cx + dxs[i], cz) || grid.isBlocked(cx, cz + dzs[i])))
                continue;   // don't cut blocked corners
            int ni = grid.index(nx, nz);
            float ng = gScore[cur] + (diag ? kDiag : 1.0f);
            if (ng < gScore[ni])
            {
                gScore[ni] = ng;
                cameFrom[ni] = cur;
                open.push({ ng + heuristic(nx, nz), ni });
            }
        }
    }

    if (!found) return {};

    std::vector<glm::vec3> path;
    for (int c = goal; c != -1; c = cameFrom[c])
        path.push_back(grid.center(c % grid.cols, c / grid.cols));
    std::reverse(path.begin(), path.end());
    if (!path.empty()) path.erase(path.begin());   // drop the start cell
    return path;
}
