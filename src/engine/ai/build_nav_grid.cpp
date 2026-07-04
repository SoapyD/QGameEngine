#include "engine/ai/build_nav_grid.h"
#include "engine/ai/types/nav_grid.h"

#include "engine/ecs/components.h"
#include "engine/level/level.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr float kCell      = 1.0f;
    constexpr float kClearance = 0.5f;   // enemy half-width margin around obstacles
    constexpr float kWallMinY  = 1.0f;   // a surface taller than this blocks (wall, not floor)

    // Mark every cell whose centre lies within the XZ box [mn, mx] inflated by
    // `pad` as blocked.
    void blockBox(NavGrid& g, glm::vec3 mn, glm::vec3 mx, float pad)
    {
        int x0, z0, x1, z1;
        g.cellOf({ mn.x - pad, 0.0f, mn.z - pad }, x0, z0);
        g.cellOf({ mx.x + pad, 0.0f, mx.z + pad }, x1, z1);
        for (int cz = z0; cz <= z1; ++cz)
            for (int cx = x0; cx <= x1; ++cx)
                if (g.inBounds(cx, cz)) g.blocked[g.index(cx, cz)] = 1;
    }
}

NavGrid buildNavGrid(entt::registry& registry, const Level& level)
{
    // XZ bounds from every level surface vertex.
    glm::vec3 mn(1e9f), mx(-1e9f);
    for (const auto& sector : level.sectors)
        for (const auto& s : sector.surfaces)
            for (const auto& v : s.vertices) { mn = glm::min(mn, v); mx = glm::max(mx, v); }
    if (mn.x > mx.x) { mn = glm::vec3(0.0f); mx = glm::vec3(30.0f, 6.0f, 30.0f); }  // fallback

    NavGrid g;
    g.cell   = kCell;
    g.origin = glm::vec3(mn.x, 0.95f, mn.z);   // walk height ~ grunt centre
    g.cols   = std::max(1, (int)std::ceil((mx.x - mn.x) / kCell));
    g.rows   = std::max(1, (int)std::ceil((mx.z - mn.z) / kCell));
    g.blocked.assign((size_t)g.cols * g.rows, 0);

    // Walls (tall surfaces) block their footprint; floors/ceilings don't.
    for (const auto& sector : level.sectors)
        for (const auto& s : sector.surfaces)
        {
            glm::vec3 smn(1e9f), smx(-1e9f);
            for (const auto& v : s.vertices) { smn = glm::min(smn, v); smx = glm::max(smx, v); }
            if (smx.y - smn.y < kWallMinY) continue;   // floor/ceiling
            blockBox(g, smn, smx, kClearance);
        }

    // Solid props/movers block. Skip triggers, the player, enemies, demo cubes.
    for (auto [e, pos, col] : registry.view<Position, AABBCollider>().each())
    {
        if (col.isTrigger) continue;
        if (registry.any_of<TagPlayer, AIState, DemoReset>(e)) continue;
        blockBox(g, pos.value - col.halfExtents, pos.value + col.halfExtents, kClearance);
    }

    return g;
}
