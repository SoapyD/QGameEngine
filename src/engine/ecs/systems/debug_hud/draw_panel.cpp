#include "engine/ecs/systems/debug_hud/debug_hud_internal.h"

#include <glad/glad.h>

// Semi-transparent filled quad drawn behind HUD text for legibility.
void drawPanel
(
	float x, float y, float width, float height,
	unsigned int shaderId, const glm::mat4& projection,
	const glm::vec3& color, float alpha
)
{
	if (alpha <= 0.0f) return;

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	float verts[] =
	{
		x,         y,          0.0f,
		x + width, y,          0.0f,
		x + width, y + height, 0.0f,
		x,         y,          0.0f,
		x + width, y + height, 0.0f,
		x,         y + height, 0.0f
	};

	unsigned int vao, vbo;
	glGenVertexArrays(1, &vao);
	glGenBuffers(1, &vbo);
	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

	glUseProgram(shaderId);
	GLint loc = glGetUniformLocation(shaderId, "projection");
	glUniformMatrix4fv(loc, 1, GL_FALSE, &projection[0][0]);

	loc = glGetUniformLocation(shaderId, "textColor");
	glUniform3fv(loc, 1, &color[0]);

	GLint alphaLoc = glGetUniformLocation(shaderId, "alpha");
	glUniform1f(alphaLoc, alpha);

	glDrawArrays(GL_TRIANGLES, 0, 6);

	glBindVertexArray(0);
	glDeleteBuffers(1, &vbo);
	glDeleteVertexArrays(1, &vao);

	glDisable(GL_BLEND);
}
