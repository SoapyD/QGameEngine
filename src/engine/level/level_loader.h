#pragma once

#include "engine/level/level.h"
#include <string>
#include <unordered_map>

// ─── LEGACY / DEAD CODE — the `.qlvl` text-format loader ─────────────────────
// NOT compiled (removed from CMake) and never called — the running game builds
// its level procedurally via createShowcaseLevel(); the live mesh builder now
// lives in build_sector_meshes.{h,cpp}. This `.qlvl` path is a dead-end
// superseded by the Phase 5 TrenchBroom `.map` loader; kept for reference only.
// See docs/processes/physics.md → "Legacy & retained code".
// ────────────────────────────────────────────────────────────────────────────
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
