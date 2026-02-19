#include "engine/ecs/scene_setup.h"
#include "engine/ecs/components.h"

void setupScene
(
	entt::registry& registry,
	const ResourceManager& resources
)
{
	auto basicShader = resources.getShader("basic");
	auto texturedShader = resources.getShader("textured");
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
		texturedShader->getId(),
		wallTexture->getId(), 
		true, 
		cubeMesh->getIndexCount()
	);
};