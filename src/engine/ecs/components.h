#pragma once

#include <glm/glm.hpp>

// ─── Spatial Components ──────────────────────────────────────────

struct Position {
	glm::vec3 value = glm::vec3(0.0f);
};

struct Rotation {
	glm::vec3 euler = glm::vec3(0.0f); // pitch, yaw, roll in degrees
};

struct Scale {
	glm::vec3 value = glm::vec3(1.0f);
};

struct Velocity {
	glm::vec3 value = glm::vec3(0.0f);
};

struct Vertex {
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 normal = glm::vec3(0.0f);      // For lighting (Chapter 7)
    glm::vec2 texCoords = glm::vec2(0.0f);
};

// ─── Lighting Components ─────────────────────────────────────────

struct DirectionalLight {
	glm::vec3 direction = glm::vec3(-0.2f, -1.0f, -0.3f);
	glm::vec3 color = glm::vec3(1.0f);
	float ambienStrength = 0.1f;
};

struct PointLight {
	glm::vec3 color = glm::vec3(1.0f);
	float ambienStrength = 0.05f;
	float linear = 0.09f;
	float quadratic = 0.032f;	
};

// ─── Rendering Components ────────────────────────────────────────

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

// ─── Tags ────────────────────────────────────────────────────────
// Tags are empty structs — they mark entities without adding data

struct TagPlayer {};