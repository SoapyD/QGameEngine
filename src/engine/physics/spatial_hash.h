#pragma once

// ─── LEGACY / DEAD CODE — not compiled, kept for tutorial reference ──────────
// `spatial_hash.cpp` is NOT in the CMake build and this header has no live
// includer (only the archived collision_system used it). Jolt owns broad-phase
// now. See docs/processes/physics.md → "Legacy & retained code".
// ────────────────────────────────────────────────────────────────────────────

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>

class SpatialHash
{
	public:
		SpatialHash(float cellSize = 4.0f);

		// clear all entries (call at start of each frame)
		void clear();

		// insert an entity at a position
		void insert
		(
			entt::entity entity,
			const glm::vec3& position,
			const glm::vec3& halfExtents
		);

		// query get all entities that might overlap with this AABB
		std::vector<entt::entity> query
		(
			const glm::vec3& position,
			const glm::vec3& halfExtents
		) const;

	private:
		float m_cellSize;

		// hash a 3D cell coordinate to a single integer
		struct CellHash
		{
			size_t operator()
			(
				const glm::ivec3& cell
			) const
			{
				size_t h = std::hash<int>()(cell.x);
				h ^= std::hash<int>()(cell.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
				h ^= std::hash<int>()(cell.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
				return h;
			}
		};

		struct CellEqual
		{
			bool operator()
			(
				const glm::ivec3 a,
				const glm::ivec3& b
			) const
			{
				return a.x == b.x &&
				a.y == b.y &&
				a.z == b.z;
			}
		};

		std::unordered_map<glm::ivec3, std::vector<entt::entity>,
							CellHash, CellEqual> m_cells;

		glm::ivec3 toCell(const glm::vec3& position) const;
};