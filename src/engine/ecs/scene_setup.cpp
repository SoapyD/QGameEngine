#include "engine/ecs/scene_setup.h"
#include "engine/ecs/components.h"

void setupScene
(
	entt::registry& registry,
	const ResourceManager& resources
)
{
	auto basicShader = resources.getShader("basic");
	auto litShader = resources.getShader("lit");
	auto wallTexture = resources.getTexture("wall");
	auto cubeMesh = resources.getMesh("cube");

	// create a cube entity
	auto cube = registry.create();
	registry.emplace<Position>(cube, glm::vec3(0.0f, 0.0f, -3.0f));
	registry.emplace<MeshRenderer>
	(
		cube, 
		cubeMesh->getVAO(), 
		0u, 
		basicShader->getId(),
		0u,
		true,
		cubeMesh->getIndexCount()
	);

	// create a textured wall
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

	// SUN LIGHT
	auto sun = registry.create();
	registry.emplace<DirectionalLight>
	(
		sun, glm::vec3(-0.2f, -1.0f, -0.3f), // direction (angled down)
		glm::vec3(1.0f, 0.95f, 0.8f), // warm white colour
		0.1f // ambient strength
	);

	// a torch in the level
	auto torch = registry.create();
	registry.emplace<Position>
	(
		torch, glm::vec3(3.0f, 2.0f, -10.f)
	);
	registry.emplace<PointLight>
	(
		torch, glm::vec3(1.0f, 0.7f, 0.3f), // warm orange
		0.05f, 0.09f, 0.032f // ambient, linear, quadratic
	);
};