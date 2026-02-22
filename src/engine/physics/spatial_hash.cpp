#include "engine/physics/spatial_hash.h"
#include <cmath>

SpatialHash::SpatialHash(float cellSize) : m_cellSize(cellSize) {}

void SpatialHash::clear()
{
	m_cells.clear();
}

glm::ivec3 SpatialHash::toCell
(
	const glm::vec3& position
) const
{
	return glm::ivec3
	(
		static_cast<int>(std::floor(position.x / m_cellSize)),
		static_cast<int>(std::floor(position.y / m_cellSize)),
		static_cast<int>(std::floor(position.z / m_cellSize))
	);
}


void SpatialHash::insert
(
	entt::entity entity,
	const glm::vec3& position,
	const glm::vec3& halfExtents
)
{
	// an entity might span multiple cells - insert into all of them
	glm::ivec3 minCell = toCell(position - halfExtents);
	glm::ivec3 maxCell = toCell(position + halfExtents);

	for (int x = minCell.x; x <= maxCell.x; x++)
	{
		for (int y = minCell.y; y <= maxCell.y; y++)
		{
			for (int z = minCell.z; z <= maxCell.z; z++)
			{
				m_cells[glm::ivec3(x, y, z)].push_back(entity);
			}	
		}	
	}
}

std::vector<entt::entity> SpatialHash::query
(
	const glm::vec3& position,
	const glm::vec3& halfExtents
) const
{
	std::vector<entt::entity> result;
	glm::ivec3 minCell = toCell(position - halfExtents);
	glm::ivec3 maxCell = toCell(position + halfExtents);

	for (int x = minCell.x; x <= maxCell.x; x++)
	{
		for (int y = minCell.y; y <= maxCell.y; y++)
		{
			for (int z = minCell.z; z <= maxCell.z; z++)
			{
				auto it = m_cells.find(glm::ivec3(x, y, z));
				if ( it != m_cells.end())
				{
					for (auto entity : it->second)
					{
						result.push_back(entity);
					}
				}
			}	
		}	
	}

	// notL result may contain duplicates if an entity spans multiple cells
	// the caller should handle this (e.g. skip self, check unique)
	return result;
}