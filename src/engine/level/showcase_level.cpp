#include "engine/level/showcase_level.h"
#include "engine/level/level.h"
#include "engine/level/build_sector_meshes.h"

// ─── Helper: build a single large test room ─────────────────────
Level createShowcaseLevel(bool headless)
{
    Level level;

    // ═══════════════════════════════════════════════════════════
    // Single room: 30 x 30 x 6   (origin 0,0,0 to 30,6,30)
    // All grey walls, floor and ceiling
    // ═══════════════════════════════════════════════════════════
    {
        Sector room;
        room.id = 0;
        room.boundsMin = glm::vec3(0.0f, 0.0f, 0.0f);
        room.boundsMax = glm::vec3(30.0f, 6.0f, 30.0f);

        // Floor
        room.surfaces.push_back({
            {glm::vec3(0,0,30), glm::vec3(30,0,30), glm::vec3(30,0,0), glm::vec3(0,0,0)},
            glm::vec3(0, 1, 0),
            "grid_grey.png", 0
        });

        // Ceiling
        room.surfaces.push_back({
            {glm::vec3(0,6,0), glm::vec3(30,6,0), glm::vec3(30,6,30), glm::vec3(0,6,30)},
            glm::vec3(0, -1, 0),
            "grid_grey.png", 0
        });

        // Back wall (z=0, normal +z)
        room.surfaces.push_back({
            {glm::vec3(30,6,0), glm::vec3(0,6,0), glm::vec3(0,0,0), glm::vec3(30,0,0)},
            glm::vec3(0, 0, 1),
            "grid_grey.png", 0
        });

        // Front wall (z=30, normal -z)
        room.surfaces.push_back({
            {glm::vec3(0,6,30), glm::vec3(30,6,30), glm::vec3(30,0,30), glm::vec3(0,0,30)},
            glm::vec3(0, 0, -1),
            "grid_grey.png", 0
        });

        // Left wall (x=0, normal +x)
        room.surfaces.push_back({
            {glm::vec3(0,6,0), glm::vec3(0,6,30), glm::vec3(0,0,30), glm::vec3(0,0,0)},
            glm::vec3(1, 0, 0),
            "grid_grey.png", 0
        });

        // Right wall (x=30, normal -x)
        room.surfaces.push_back({
            {glm::vec3(30,6,30), glm::vec3(30,6,0), glm::vec3(30,0,0), glm::vec3(30,0,30)},
            glm::vec3(-1, 0, 0),
            "grid_grey.png", 0
        });

        level.sectors.push_back(std::move(room));
    }

    // Render meshes are only needed for drawing; the headless harness has no
    // GL context, and physics bodies are built from the surface geometry above.
    if (!headless)
        buildSectorMeshes(level);
    return level;
}
