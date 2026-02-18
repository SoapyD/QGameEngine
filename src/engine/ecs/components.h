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