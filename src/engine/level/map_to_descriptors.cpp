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

    // Lift a floor-standing point actor from its authored feet-origin up to the
    // centre-origin the factories expect (value = the actor's collider half-Y).
    float groundHalfHeight(const std::string& cls)
    {
        if (cls == "monster_grunt" || cls == "monster_ranged") return 0.9f;  // grunt collider
        return 0.0f;
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

        // Brush entities derive origin+size from their AABB; points read `origin`.
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

        // Authored feet-origin → the body-centre origin the factories expect, so
        // a floor-placed grunt stands on the floor instead of sinking half-in.
        p.origin.y += groundHalfHeight(cls);

        // Vector props the factories read back by name must cross into engine
        // space here (positions scale + axis-swap; a direction only axis-swaps).
        auto convertKey = [&](const char* key, glm::vec3 (*xf)(glm::vec3))
        {
            if (!e.has(key)) return;
            std::istringstream ss(e.getString(key));
            glm::vec3 m(0.0f);
            if (ss >> m.x >> m.y >> m.z)
                p.props[key] = vec3ToStr(xf(m));
        };
        convertKey("endpos",    qmap::mapPointToEngine);  // door/plat travel target
        convertKey("velocity",  qmap::mapPointToEngine);  // prop_dynamic velocity
        convertKey("direction", qmap::mapDirToEngine);    // light_environment sun

        out.push_back(std::move(p));
    }

    return out;
}
