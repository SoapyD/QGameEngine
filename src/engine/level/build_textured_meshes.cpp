#include "engine/level/build_textured_meshes.h"

#include "engine/level/level.h"
#include "engine/ecs/components/core.h"   // Vertex
#include "engine/renderer/mesh.h"

#include <glm/glm.hpp>

#include <unordered_map>

std::vector<std::pair<std::string, std::unique_ptr<Mesh>>>
buildTexturedMeshes(const Sector& sector)
{
    struct Buffer
    {
        std::vector<Vertex>       vertices;
        std::vector<unsigned int> indices;
    };
    std::unordered_map<std::string, Buffer> groups;

    for (const auto& s : sector.surfaces)
    {
        Buffer& b = groups[s.textureName];
        unsigned int base = static_cast<unsigned int>(b.vertices.size());

        // Planar UVs from edge lengths (same as build_sector_meshes).
        float uScale = glm::length(s.vertices[1] - s.vertices[0]);
        float vScale = glm::length(s.vertices[3] - s.vertices[0]);

        b.vertices.push_back({s.vertices[0], s.normal, {0.0f,   0.0f}});
        b.vertices.push_back({s.vertices[1], s.normal, {uScale, 0.0f}});
        b.vertices.push_back({s.vertices[2], s.normal, {uScale, vScale}});
        b.vertices.push_back({s.vertices[3], s.normal, {0.0f,   vScale}});

        b.indices.push_back(base + 0);
        b.indices.push_back(base + 1);
        b.indices.push_back(base + 2);
        b.indices.push_back(base + 0);
        b.indices.push_back(base + 2);
        b.indices.push_back(base + 3);
    }

    std::vector<std::pair<std::string, std::unique_ptr<Mesh>>> out;
    out.reserve(groups.size());
    for (auto& [tex, b] : groups)
        if (!b.vertices.empty())
            out.emplace_back(tex, std::make_unique<Mesh>(b.vertices, b.indices));
    return out;
}
