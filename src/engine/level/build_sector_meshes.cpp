#include "engine/level/build_sector_meshes.h"

#include "engine/ecs/components/core.h"   // Vertex
#include "engine/renderer/mesh.h"

#include <vector>

void buildSectorMeshes(Level& level)
{
	for (auto& sector : level.sectors)
	{
		std::vector<Vertex> vertices;
		std::vector<unsigned int> indices;

		for(const auto& surface : sector.surfaces)
		{
			unsigned int baseIndex = static_cast<unsigned int>(vertices.size());

            // Calculate UV coordinates based on surface dimensions
            // Simple planar projection for now
			float uScale = glm::length(surface.vertices[1] - surface.vertices[0]);
			float vScale = glm::length(surface.vertices[3] - surface.vertices[0]);

			// four vertices for the quad
			vertices.push_back({surface.vertices[0], surface.normal, {0.0f, 0.0f}});
			vertices.push_back({surface.vertices[1], surface.normal, {uScale, 0.0f}});
			vertices.push_back({surface.vertices[2], surface.normal, {uScale, vScale}});
			vertices.push_back({surface.vertices[3], surface.normal, {0.0f, vScale}});

			// two trianges for the quad
			indices.push_back(baseIndex + 0);
			indices.push_back(baseIndex + 1);
			indices.push_back(baseIndex + 2);
			indices.push_back(baseIndex + 0);
			indices.push_back(baseIndex + 2);
			indices.push_back(baseIndex + 3);
		}

		if (!vertices.empty())
		{
			sector.mesh = std::make_unique<Mesh>(vertices, indices);
		}
	}
}
