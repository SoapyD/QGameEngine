#include "engine/ecs/systems/debug_hud/debug_hud_internal.h"

#include <glad/glad.h>
#include <stb_easy_font.h>
#include <vector>

// ─── Draw a string at screen position (x, y) ─────────────────────
// stb_easy_font outputs quads as 4 vertices each, with a stride of
// 16 bytes per vertex (x, y, z, colour as 4 floats).
// Core profile doesn't support GL_QUADS, so we convert to triangles
// using an index buffer: each quad becomes 2 triangles (6 indices).
void drawText
(
	float x, float y, const char* text,
	unsigned int shaderId, const glm::mat4& projection,
	float scale, const glm::vec3& color
)
{
	static char vertexBuffer[4096 * 16];
	int numQuads = stb_easy_font_print
	(
		x, y, const_cast<char*>(text),
		nullptr, vertexBuffer, sizeof(vertexBuffer)
	);

	if (numQuads <= 0) return;

    // Build index buffer: convert quads to triangles
    // Quad vertices: 0,1,2,3 → triangles: (0,1,2) and (0,2,3)
	std::vector<unsigned int> indices;
	indices.reserve(numQuads * 6);
	for (int i = 0; i < numQuads; i++)
	{
		unsigned int base = i * 4;
		indices.push_back(base + 0);
		indices.push_back(base + 1);
		indices.push_back(base + 2);
		indices.push_back(base + 0);
		indices.push_back(base + 2);
		indices.push_back(base + 3);
	}

	unsigned int vao, vbo, ebo;
	glGenVertexArrays(1, &vao);
	glGenBuffers(1, &vbo);
	glGenBuffers(1, &ebo);

	glBindVertexArray(vao);

	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData
	(
		GL_ARRAY_BUFFER, numQuads * 64,
		vertexBuffer, GL_DYNAMIC_DRAW
	);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
	glBufferData
	(
		GL_ELEMENT_ARRAY_BUFFER,
		indices.size() * sizeof(unsigned int),
		indices.data(), GL_DYNAMIC_DRAW
	);

    // stb_easy_font vertex layout: x, y, z, colour (4 floats per vertex)
    // We only need x and y — stride is 16 bytes
	glEnableVertexAttribArray(0);
	glVertexAttribPointer
	(
		0, 2, GL_FLOAT,
		GL_FALSE, 16, (void*)0
	);

	// Set up shader
	glUseProgram(shaderId);

	GLint loc = glGetUniformLocation(shaderId, "projection");
	glUniformMatrix4fv(loc, 1, GL_FALSE, &projection[0][0]);

	loc = glGetUniformLocation(shaderId, "textColor");
	glUniform3fv(loc, 1, &color[0]);
	GLint alphaLoc = glGetUniformLocation(shaderId, "alpha");
	glUniform1f(alphaLoc, 1.0f);

	glDrawElements(GL_TRIANGLES,(int)indices.size(), GL_UNSIGNED_INT, 0);

	glBindVertexArray(0);
	glDeleteBuffers(1, &ebo);
	glDeleteBuffers(1, &vbo);
	glDeleteVertexArrays(1, &vao);
}
