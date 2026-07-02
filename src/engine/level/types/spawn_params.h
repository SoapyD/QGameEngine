#pragma once
// Data-driven spawn descriptor + render context for the entity-factory dispatch
// layer. SpawnParams is the shape the .map loader will emit (and the in-code
// showcase descriptor mimics); SpawnContext carries the render/asset handles map
// data doesn't include. See docs/plans/2026-07-02-entity-factory-classname-dispatch.md.

#include <glm/glm.hpp>
#include <functional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>

#include "engine/ecs/types/mesh_assets.h"

namespace factories
{
    // One spawnable entity, described the way a TrenchBroom entity is: a string
    // classname, a geometric origin/size, optional target links, and a bag of
    // string key/values. Typed getters parse `props` on demand.
    struct SpawnParams
    {
        std::string classname;
        glm::vec3   origin{0.0f};   // entity origin / position
        glm::vec3   size{1.0f};     // full render extents; box collider half = size * 0.5
        std::string targetname;     // this entity's name (for target linking; may be empty)
        std::string target;         // name this entity links to (may be empty)
        std::unordered_map<std::string, std::string> props;

        bool has(const std::string& key) const { return props.find(key) != props.end(); }

        std::string getString(const std::string& key, std::string fallback = {}) const
        {
            auto it = props.find(key);
            return it == props.end() ? std::move(fallback) : it->second;
        }

        float getFloat(const std::string& key, float fallback = 0.0f) const
        {
            auto it = props.find(key);
            if (it == props.end()) return fallback;
            try { return std::stof(it->second); } catch (...) { return fallback; }
        }

        int getInt(const std::string& key, int fallback = 0) const
        {
            auto it = props.find(key);
            if (it == props.end()) return fallback;
            try { return std::stoi(it->second); } catch (...) { return fallback; }
        }

        // Parses "x y z" (whitespace-separated). Missing key / malformed → fallback.
        glm::vec3 getVec3(const std::string& key, glm::vec3 fallback = glm::vec3(0.0f)) const
        {
            auto it = props.find(key);
            if (it == props.end()) return fallback;
            std::istringstream ss(it->second);
            glm::vec3 out;
            if (!(ss >> out.x >> out.y >> out.z)) return fallback;
            return out;
        }
    };

    // Render/asset side that map data can't carry: the shared cube handles plus a
    // texture-name → GL id resolver (backed by ResourceManager at the call site).
    struct SpawnContext
    {
        MeshAssets assets;
        std::function<unsigned int(std::string_view)> texture;
    };
}
