#include "engine/level/map_to_level.h"

#include "engine/level/map_transform.h"

#include <algorithm>
#include <limits>
#include <string>
#include <unordered_map>

namespace
{
    // The texture used by most of a brush's faces. For single-texture box brushes
    // (the smoke.map case) this is exact; for mixed-texture brushes the AABB
    // representation can only carry one, so majority wins.
    std::string majorityTexture(const qmap::MapBrush& brush)
    {
        std::unordered_map<std::string, int> counts;
        for (const auto& f : brush.faces) counts[f.texture]++;

        std::string best;
        int bestN = -1;
        for (const auto& [tex, n] : counts)
            if (n > bestN) { bestN = n; best = tex; }
        return best;
    }

    // Push the 6 axis-aligned faces of the box [mn,mx] into the sector, each wound
    // CCW as seen from outside (GL_CCW front-face + GL_BACK cull) with an outward
    // normal. The interior-facing face of a wall slab is what the player sees.
    void addBoxSurfaces(Sector& sector, glm::vec3 mn, glm::vec3 mx, const std::string& tex)
    {
        const float x0 = mn.x, y0 = mn.y, z0 = mn.z;
        const float x1 = mx.x, y1 = mx.y, z1 = mx.z;

        auto push = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, glm::vec3 n)
        {
            Surface s;
            s.vertices[0] = a; s.vertices[1] = b; s.vertices[2] = c; s.vertices[3] = d;
            s.normal = n;
            s.textureName = tex;
            sector.surfaces.push_back(s);
        };

        push({x1,y0,z1}, {x1,y0,z0}, {x1,y1,z0}, {x1,y1,z1}, { 1, 0, 0}); // +X
        push({x0,y0,z0}, {x0,y0,z1}, {x0,y1,z1}, {x0,y1,z0}, {-1, 0, 0}); // -X
        push({x0,y1,z1}, {x1,y1,z1}, {x1,y1,z0}, {x0,y1,z0}, { 0, 1, 0}); // +Y
        push({x0,y0,z0}, {x1,y0,z0}, {x1,y0,z1}, {x0,y0,z1}, { 0,-1, 0}); // -Y
        push({x0,y0,z1}, {x1,y0,z1}, {x1,y1,z1}, {x0,y1,z1}, { 0, 0, 1}); // +Z
        push({x1,y0,z0}, {x0,y0,z0}, {x0,y1,z0}, {x1,y1,z0}, { 0, 0,-1}); // -Z
    }
}

Level mapWorldspawnToLevel(const qmap::MapData& map)
{
    Level level;

    Sector world;
    world.id = 0;

    glm::vec3 lvlMin(std::numeric_limits<float>::max());
    glm::vec3 lvlMax(std::numeric_limits<float>::lowest());

    for (const auto& entity : map.entities)
    {
        if (entity.classname() != "worldspawn") continue;

        for (const auto& brush : entity.brushes)
        {
            // Brush AABB in ENGINE space (convert each face point, then min/max —
            // the axis swap makes this cleaner than converting a map-space AABB).
            glm::vec3 mn(std::numeric_limits<float>::max());
            glm::vec3 mx(std::numeric_limits<float>::lowest());
            for (const auto& face : brush.faces)
                for (const auto& p : face.points)
                {
                    glm::vec3 e = qmap::mapPointToEngine(p);
                    mn = glm::min(mn, e);
                    mx = glm::max(mx, e);
                }

            if (mn.x > mx.x) continue;  // brush with no faces — skip

            addBoxSurfaces(world, mn, mx, majorityTexture(brush));
            lvlMin = glm::min(lvlMin, mn);
            lvlMax = glm::max(lvlMax, mx);
        }
    }

    world.boundsMin = world.surfaces.empty() ? glm::vec3(0.0f) : lvlMin;
    world.boundsMax = world.surfaces.empty() ? glm::vec3(0.0f) : lvlMax;

    level.sectors.push_back(std::move(world));
    return level;
}
