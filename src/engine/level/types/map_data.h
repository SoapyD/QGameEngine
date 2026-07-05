#pragma once
// Intermediate representation of a parsed TrenchBroom `.map` file (Standard
// format). This is the raw text→struct output of step 2.1 (the parser); it holds
// geometry and properties EXACTLY as written in the file — no coordinate/scale
// conversion is applied here. Z-up→Y-up axis swap and the Quake-unit→engine-unit
// scale happen downstream when brushes become meshes/colliders (2.2/2.4) and
// entities become SpawnParams (2.3), so the conversion is defined in one place.
//
// Grammar this mirrors (Standard .map):
//   map    := entity*
//   entity := '{' (keyval | brush)* '}'
//   keyval := '"' key '"' '"' value '"'
//   brush  := '{' face+ '}'
//   face   := '(' x y z ')' '(' x y z ')' '(' x y z ')' TEX offX offY rot sclX sclY

#include <glm/glm.hpp>

#include <string>
#include <unordered_map>
#include <vector>

namespace qmap
{
    // One brush face: a plane given as three points (winding defines the outward
    // normal, computed downstream) plus its texture name and UV placement.
    struct MapFace
    {
        glm::vec3   points[3]{glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(0.0f)};
        std::string texture;          // texture name (no path/extension)
        glm::vec2   offset{0.0f};     // texture offset  (u, v)
        float       rotation = 0.0f;  // texture rotation (degrees)
        glm::vec2   scale{1.0f};      // texture scale    (u, v)
    };

    // A convex brush = the intersection of its face half-spaces. Solid world
    // geometry and brush entities (doors/lifts/triggers) are made of these.
    struct MapBrush
    {
        std::vector<MapFace> faces;
    };

    // One `.map` entity: a bag of string key/values plus any brushes it owns.
    // Point entities (lights, spawns, items) carry no brushes; brush entities
    // (worldspawn, func_door, trigger_*) carry one or more.
    struct MapEntity
    {
        std::unordered_map<std::string, std::string> props;
        std::vector<MapBrush>                        brushes;

        bool has(const std::string& key) const { return props.find(key) != props.end(); }

        std::string getString(const std::string& key, std::string fallback = {}) const
        {
            auto it = props.find(key);
            return it == props.end() ? std::move(fallback) : it->second;
        }

        // Convenience: an entity's classname (empty if unset — a malformed entity).
        std::string classname() const { return getString("classname"); }

        bool isPointEntity() const { return brushes.empty(); }
    };

    // A whole parsed map file.
    struct MapData
    {
        std::vector<MapEntity> entities;
    };
}
