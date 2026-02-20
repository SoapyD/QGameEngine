#pragma once

#include "engine/level/level.h"
#include <string>
#include <unordered_map>

// Free function — builds GPU meshes for all sectors in a level.
// Used by both LevelLoader::load() and createTestLevel().
void buildSectorMeshes(Level& level);

class LevelLoader
{
	public:
		Level load
		(
			const std::string& path,
			const std::unordered_map<std::string,
			unsigned int>& textureMap
		);

	private:
		void parseSector(const std::string& line, Level& level);
		void parseSurface
		(
			const std::string& line,
			Level& level,
			const std::unordered_map<std::string, unsigned int>& textureMap
		);
		void parsePortal(const std::string& line, Level& level);
		void parseEntity(const std::string& line, Level& level);

		glm::vec3 computeNormal
		(
			const glm::vec3& v0,
			const glm::vec3& v1,
			const glm::vec3& v2
		);
};
