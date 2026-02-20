#include "engine/ecs/scene_setup.h"
#include "engine/ecs/components.h"
#include "engine/level/level.h"
#include "engine/level/level_loader.h"

// ─── Helper: build a test level programmatically ─────────────────
static Level createTestLevel()
{
	Level level;

	// room 1: a simple box room
	Sector room1;
	room1.id = 0;
	room1.boundsMin = glm::vec3(-5.0f, 0.0f, -5.0f);
	room1.boundsMax = glm::vec3(5.0f, 4.0f, 5.0f);

    // Floor
    room1.surfaces.push_back({
        {glm::vec3(-5,0,5), glm::vec3(5,0,5), glm::vec3(5,0,-5), glm::vec3(-5,0,-5)},
        glm::vec3(0, 1, 0),  // normal: up
        "floor_stone.png", 0
    });

    // Ceiling
    room1.surfaces.push_back({
        {glm::vec3(-5,4,-5), glm::vec3(5,4,-5), glm::vec3(5,4,5), glm::vec3(-5,4,5)},
        glm::vec3(0, -1, 0),  // normal: down
        "ceil_dark.png", 0
    });

    // Four walls — vertices wound CCW when viewed from INSIDE the room
    // so that front-face culling keeps the inward-facing side visible.

    // Front wall (z = 5, normal points -z into the room)
    room1.surfaces.push_back({
        {glm::vec3(-5,4,5), glm::vec3(5,4,5), glm::vec3(5,0,5), glm::vec3(-5,0,5)},
        glm::vec3(0, 0, -1),
        "wall_brick.png", 0
    });

    // Back wall (z = -5, normal points +z into the room)
    room1.surfaces.push_back({
        {glm::vec3(5,4,-5), glm::vec3(-5,4,-5), glm::vec3(-5,0,-5), glm::vec3(5,0,-5)},
        glm::vec3(0, 0, 1),
        "wall_brick.png", 0
    });

    // Left wall (x = -5, normal points +x into the room)
    room1.surfaces.push_back({
        {glm::vec3(-5,4,-5), glm::vec3(-5,4,5), glm::vec3(-5,0,5), glm::vec3(-5,0,-5)},
        glm::vec3(1, 0, 0),
        "wall_brick.png", 0
    });

    // Right wall (x = 5, normal points -x into the room)
    room1.surfaces.push_back({
        {glm::vec3(5,4,5), glm::vec3(5,4,-5), glm::vec3(5,0,-5), glm::vec3(5,0,5)},
        glm::vec3(-1, 0, 0),
        "wall_brick.png", 0
    });

    level.sectors.push_back(std::move(room1));

    // Build GPU meshes for all sectors
    buildSectorMeshes(level);

    return level;
}

Level setupScene
(
	entt::registry& registry,
	const ResourceManager& resources
)
{
	auto litShader = resources.getShader("lit");
	auto wallTexture = resources.getTexture("wall");
	auto cubeMesh = resources.getMesh("cube");

	// ─── Create the level geometry ───────────────────────────────
	Level level = createTestLevel();

	// Create an ECS entity for each sector so the render system draws it.
	// Position is (0,0,0) because sector vertices are already in world space.
	for (const auto& sector : level.sectors)
	{
		if (!sector.mesh) continue;

		auto sectorEntity = registry.create();
		registry.emplace<Position>(sectorEntity, glm::vec3(0.0f));
		registry.emplace<MeshRenderer>
		(
			sectorEntity,
			sector.mesh->getVAO(),
			0u,
			litShader->getId(),
			wallTexture->getId(),
			true,
			sector.mesh->getIndexCount()
		);
	}

	// Keep the test cubes inside the room as visual references
	auto cube = registry.create();
	registry.emplace<Position>(cube, glm::vec3(-3.0f, 0.0f, -3.0f));
	registry.emplace<MeshRenderer>
	(
		cube,
		cubeMesh->getVAO(),
		0u,
		litShader->getId(),
		wallTexture->getId(),
		true,
		cubeMesh->getIndexCount()
	);

	auto wall = registry.create();
	registry.emplace<Position>(wall, glm::vec3(2.0f, 0.0f, -3.0f));
	registry.emplace<MeshRenderer>
	(
		wall,
		cubeMesh->getVAO(),
		0u,
		litShader->getId(),
		wallTexture->getId(),
		true,
		cubeMesh->getIndexCount()
	);

	// Sun light
	auto sun = registry.create();
	registry.emplace<DirectionalLight>
	(
		sun, glm::vec3(-0.2f, -1.0f, -0.3f), // direction (angled down)
		glm::vec3(1.0f, 0.95f, 0.8f), // warm white colour
		0.1f // ambient strength
	);

	// A torch in the level (close to the wall cube)
	auto torch = registry.create();
	registry.emplace<Position>
	(
		torch, glm::vec3(3.0f, 2.0f, -1.0f)
	);
	registry.emplace<PointLight>
	(
		torch, glm::vec3(2.0f, 1.4f, 0.6f), // bright warm orange
		0.15f, 0.045f, 0.0075f // ambient, linear, quadratic (~65 unit range)
	);

	return level;
};