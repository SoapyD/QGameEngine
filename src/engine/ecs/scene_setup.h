#pragma once

#include <entt/entt.hpp>
#include "engine/core/mesh_factory.h"
#include "engine/renderer/shader.h"
#include "engine/renderer/texture.h"

#include <memory>

// set up the initial scene entities
// this replaces the inline entity creation that was in main
void setupScene
(
	entt::registry& registry,
	const MeshData& triangleMesh,
	const MeshData& quadMesh,
	std::shared_ptr<Shader> basicShader,
	std::shared_ptr<Shader> texturedShader,
	std::shared_ptr<Texture> walltexture
);
