#include "engine/ecs/scene_setup.h"
#include "engine/ecs/components.h"

void setupScene
(
	entt::registry& registry,
	const ResourceManager& resources
)
{
	auto litShader = resources.getShader("lit");
	auto wallTexture = resources.getTexture("wall");
	auto cubeMesh = resources.getMesh("cube");

	// create a cube (lit — far from torch, mostly sun-lit)
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

	// create a wall cube (lit — close to torch, gets warm orange tint)
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
};