#pragma once

#include "engine/level/level.h"
#include <string>
#include <unordered_map>

// LIVE — builds GPU meshes for all sectors in a level. Called by
// createShowcaseLevel() (non-headless). Despite living beside the legacy
// LevelLoader below, this function is NOT dead. See docs/processes/physics.md.
void buildSectorMeshes(Level& level);

// ─── LEGACY / DEAD CODE — the `.qlvl` text-format loader ─────────────────────
// LevelLoader parses the custom `.qlvl` format (see assets/levels/test.qlvl),
// which the running game NEVER calls — the showcase is built procedurally by
// createShowcaseLevel(). This path is a dead-end superseded by the Phase 5
// TrenchBroom `.map` loader; do not invest in it. (Only buildSectorMeshes above
// is live.) See docs/processes/physics.md → "Legacy & retained code".
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
