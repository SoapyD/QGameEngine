#include "engine/ecs/scene_setup.h"
#include "engine/ecs/components.h"

void setupScene
(
	entt::registry& registry,
	const MeshData& triangleMesh,
	const MeshData& quadMesh,
	std::shared_ptr<Shader> basicShader,
	std::shared_ptr<Shader> texturedShader,
	std::shared_ptr<Texture> wallTexture
)
{
	// create a triangle entity
	auto triangle = registry.create();
	registry.emplace<Position>(triangle, glm::vec3(0.0f, 0.0f, 0.0f));
	registry.emplace<MeshRenderer>(triangle, triangleMesh.vao, triangleMesh.vertexCount, basicShader->getId());

	// create a second triangle off to the right
	auto triangle2 = registry.create();
	registry.emplace<Position>(triangle2, glm::vec3(2.0f, 0.0f, -1.0f));
	registry.emplace<Rotation>(triangle2, glm::vec3(0.0f, 45.0f, 0.0f));	
	registry.emplace<MeshRenderer>(triangle2, triangleMesh.vao, triangleMesh.vertexCount, basicShader->getId());

	// create a textured wall quad
	auto wall = registry.create();
	registry.emplace<Position>(wall, glm::vec3(0.0f, 0.0f, -2.0f));
	registry.emplace<MeshRenderer>(wall, quadMesh.vao, quadMesh.vertexCount, texturedShader->getId(),
								wallTexture->getId(), false, 0u);
};