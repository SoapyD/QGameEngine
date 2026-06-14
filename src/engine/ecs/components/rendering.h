#pragma once

#include <glm/glm.hpp>

// Lighting and rendering components.

struct DirectionalLight {
	glm::vec3 direction = glm::vec3(-0.2f, -1.0f, -0.3f);
	glm::vec3 color = glm::vec3(1.0f);
	float ambientStrength = 0.1f;
};

struct PointLight {
	glm::vec3 color = glm::vec3(1.0f);
	float ambientStrength = 0.05f;
	float linear = 0.09f;
	float quadratic = 0.032f;
};

struct MeshRenderer {
	unsigned int vao = 0; // Vertex Array Object handle
	unsigned int vertexCount = 0; // Number of vertices to draw
	unsigned int shaderId = 0; // Shader program to use
	unsigned int textureId = 0; // 0 means no texture
	bool useIndices = false; //for index drawing
	unsigned int indexCount = 0; // number of indices
};

struct Colour {
	glm::vec4 value = glm::vec4(1.0f); // RGBA
};
