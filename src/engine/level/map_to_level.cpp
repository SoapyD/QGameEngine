#include "engine/level/map_to_level.h"

#include "engine/level/brush_geometry.h"

#include <limits>

namespace
{
    // Emit a convex face polygon as engine Surfaces. Surface is a quad, so a 4-gon
    // maps 1:1 (box maps are unchanged — smoke.map still yields 6 surfaces/brush),
    // and an N-gon fans into (N-2) triangle-surfaces stored as degenerate quads
    // (v0, vt, vt+1, vt+1). The renderer/AABB paths treat the repeated corner
    // harmlessly.
    void emitPolygon(Sector& sector, const qmap::BrushFacePolygon& face)
    {
        const auto& v = face.vertices;
        if (v.size() < 3) return;

        auto push = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d)
        {
            Surface s;
            s.vertices[0] = a; s.vertices[1] = b; s.vertices[2] = c; s.vertices[3] = d;
            s.normal = face.normal;
            s.textureName = face.texture;
            sector.surfaces.push_back(s);
        };

        if (v.size() == 4)
        {
            push(v[0], v[1], v[2], v[3]);   // exact quad — the box-brush common case
            return;
        }
        for (size_t t = 1; t + 1 < v.size(); ++t)   // fan from v[0]
            push(v[0], v[t], v[t + 1], v[t + 1]);
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
            qmap::BrushGeometry geo = qmap::buildBrushGeometry(brush);
            if (geo.faces.empty()) continue;   // degenerate / unbounded — skip

            for (const auto& face : geo.faces)
            {
                emitPolygon(world, face);
                for (const auto& p : face.vertices) { lvlMin = glm::min(lvlMin, p); lvlMax = glm::max(lvlMax, p); }
            }

            // True convex hull for collision (angled brushes collide by their real
            // shape, not a fattened AABB).
            level.collisionHulls.push_back(std::move(geo.vertices));
        }
    }

    world.boundsMin = world.surfaces.empty() ? glm::vec3(0.0f) : lvlMin;
    world.boundsMax = world.surfaces.empty() ? glm::vec3(0.0f) : lvlMax;

    level.sectors.push_back(std::move(world));
    return level;
}
