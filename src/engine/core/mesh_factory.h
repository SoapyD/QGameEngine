#pragma once

#include <glad/glad.h>

// ─── MeshData ────────────────────────────────────────────────────
// Simple container for the GPU handles returned by mesh creation.
// Stores the VAO (for rendering), VBO (for cleanup), and vertex count.


struct MeshData
{
	unsigned int vao = 0;
	unsigned int vbo = 0;
	unsigned int vertexCount = 0;

	// cleanup GPU resources. Call this when the is no longer needed.

	void destroy()
	{
		if (vao) glDeleteVertexArrays(1, &vao);
		if (vbo) glDeleteBuffers(1, &vbo);
		vao = 0;
		vbo = 0;
	}
};

// ─── MeshFactory ─────────────────────────────────────────────────
// Free functions that create common mesh shapes.
// Each returns a MeshData with the GPU handles ready to use.

namespace MeshFactory
{
	// Create a coloured triangle (position + colour per vertex)
	// Layout: location 0 = vec3 position, location 1 = vec3 colour
	MeshData createTriangleMesh();

	// Create a textured quad (two triangles, position + UV per vertex)
	// Layout: location 0 = vec3 position, location 1 = vec2 texcoord
	MeshData createQuadMesh();
}
