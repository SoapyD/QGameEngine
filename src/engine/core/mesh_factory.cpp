#include "engine/core/mesh_factory.h"

namespace MeshFactory
{
	MeshData createTriangleMesh()
	{
		// ─── Vertex data: position (vec3) + colour (vec3) ────────
		float vertices[] =
		{
			// positions	// colours
			-0.5f, -0.5f, 0.0f,  1.0f, 0.0f, 0.0f,  // Bottom-left  (red)
			0.5f, -0.5f, 0.0f,  0.0f, 1.0f, 0.0f,  // Bottom-right (green)
			0.0f,  0.5f, 0.0f,  0.0f, 0.0f, 1.0f   // Top          (blue)
		};

		MeshData mesh;
		mesh.vertexCount = 3;

		glGenVertexArrays(1, &mesh.vao); // Generate 1 VAO
		glGenBuffers(1, &mesh.vbo); // Generate 1 VBO

		// bind the vao first = it will "record" subsequent VBO and attribute config
		glBindVertexArray(mesh.vao);

		// Bind the VBO and upload vetex data to GPU
		glBindBuffer(GL_ARRAY_BUFFER, mesh.vao);
		glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

		// tell openGL to interpret the vertex data:

		// attribute 0: Position (3 floats, starting at offset 0)
		glVertexAttribPointer(
			0,                    // Attribute index (matches layout(location = 0) in shader)
			3,                    // Number of components (vec3 = 3 floats)
			GL_FLOAT,             // Data type
			GL_FALSE,             // Normalise? No
			6 * sizeof(float),    // Stride: bytes between consecutive vertices (6 floats total)
			(void*)0              // Offset: where this attribute starts within each vertex
		);
		glEnableVertexAttribArray(0);

		// attribute 1: colour (3 floats, starting at offset 3 floats in)
		glVertexAttribPointer(
			1, 
			3, 
			GL_FLOAT, 
			GL_FALSE, 
			6 * sizeof(float),
			(void*)(3 * sizeof(float))
		);
		glEnableVertexAttribArray(1);

		// unbind (optional, for safety)
		glBindVertexArray(0);

		return mesh;
	}

	MeshData createQuadMesh()
	{
		// ─── Quad vertex data (textured) ─────────────────────────────
		float vertices[] = {
			// Positions          // UV coords
			-0.5f, -0.5f, 0.0f,  0.0f, 0.0f,  // Bottom-left
			0.5f, -0.5f, 0.0f,  1.0f, 0.0f,  // Bottom-right
			0.5f,  0.5f, 0.0f,  1.0f, 1.0f,  // Top-right

			-0.5f, -0.5f, 0.0f,  0.0f, 0.0f,  // Bottom-left
			0.5f,  0.5f, 0.0f,  1.0f, 1.0f,  // Top-right
			-0.5f,  0.5f, 0.0f,  0.0f, 1.0f   // Top-left
		};

		MeshData mesh;
		mesh.vertexCount = 6;

		glGenVertexArrays(1, &mesh.vao); // Generate 1 VAO
		glGenBuffers(1, &mesh.vbo); // Generate 1 VBO

		// bind the vao first = it will "record" subsequent VBO and attribute config
		glBindVertexArray(mesh.vao);

		// Bind the VBO and upload vetex data to GPU
		glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
		glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

		// tell openGL to interpret the vertex data:

		// attribute 0: Position (3 floats, starting at offset 0)
		glVertexAttribPointer(
			0,                    // Attribute index (matches layout(location = 0) in shader)
			3,                    // Number of components (vec3 = 3 floats)
			GL_FLOAT,             // Data type
			GL_FALSE,             // Normalise? No
			5 * sizeof(float),    // Stride: bytes between consecutive vertices (5 floats total)
			(void*)0              // Offset: where this attribute starts within each vertex
		);
		glEnableVertexAttribArray(0);

		// attribute 1: colour (3 floats, starting at offset 3 floats in)
		glVertexAttribPointer(
			1, 
			2, 
			GL_FLOAT, 
			GL_FALSE, 
			5 * sizeof(float),
			(void*)(3 * sizeof(float))
		);
		glEnableVertexAttribArray(1);

		// unbind (optional, for safety)
		glBindVertexArray(0);

		return mesh;
	}
}