#include "engine/level/map_to_descriptors.h"

#include "engine/level/map_transform.h"

#include <limits>
#include <sstream>
#include <string>
#include <utility>

namespace
{
    std::string vec3ToStr(glm::vec3 v)
    {
        std::ostringstream ss;
        ss << v.x << ' ' << v.y << ' ' << v.z;
        return ss.str();
    }

    // Engine-space AABB over all of an entity's brush face points. Returns false
    // for a point entity (no brushes).
    bool entityBrushAABB(const qmap::MapEntity& e, glm::vec3& mn, glm::vec3& mx)
    {
        mn = glm::vec3(std::numeric_limits<float>::max());
        mx = glm::vec3(std::numeric_limits<float>::lowest());
        bool any = false;
        for (const auto& b : e.brushes)
            for (const auto& f : b.faces)
                for (const auto& p : f.points)
                {
                    glm::vec3 v = qmap::mapPointToEngine(p);
                    mn = glm::min(mn, v);
                    mx = glm::max(mx, v);
                    any = true;
                }
        return any;
    }
}

std::vector<factories::SpawnParams> mapEntitiesToDescriptors(const qmap::MapData& map)
{
    std::vector<factories::SpawnParams> out;

    for (const auto& e : map.entities)
    {
        const std::string cls = e.classname();
        if (cls.empty() || cls == "worldspawn") continue;  // geometry, not an entity

        factories::SpawnParams p;
        p.classname  = cls;
        p.targetname = e.getString("targetname");
        p.target     = e.getString("target");
        p.props      = e.props;   // spatial keys overwritten (in engine space) below

        // Origin + size. Brush entities derive both from their AABB; point
        // entities read the `origin` key (size keeps its default extent).
        glm::vec3 mn, mx;
        if (entityBrushAABB(e, mn, mx))
        {
            p.origin = (mn + mx) * 0.5f;
            p.size   = mx - mn;
        }
        else if (e.has("origin"))
        {
            std::istringstream ss(e.getString("origin"));
            glm::vec3 m(0.0f);
            if (ss >> m.x >> m.y >> m.z)
                p.origin = qmap::mapPointToEngine(m);
        }

        // Vector-valued props factories read back by name must cross into engine
        // space here (props are otherwise raw map-space strings).
        auto convertPoint = [&](const char* key)
        {
            if (!e.has(key)) return;
            std::istringstream ss(e.getString(key));
            glm::vec3 m(0.0f);
            if (ss >> m.x >> m.y >> m.z)
                p.props[key] = vec3ToStr(qmap::mapPointToEngine(m));
        };
        auto convertDir = [&](const char* key)
        {
            if (!e.has(key)) return;
            std::istringstream ss(e.getString(key));
            glm::vec3 m(0.0f);
            if (ss >> m.x >> m.y >> m.z)
                p.props[key] = vec3ToStr(qmap::mapDirToEngine(m));
        };
        convertPoint("endpos");    // func_door/func_plat travel target (world pos)
        convertPoint("velocity");  // prop_dynamic initial velocity (linear)
        convertDir("direction");   // light_environment sun vector

        out.push_back(std::move(p));
    }

    return out;
}
