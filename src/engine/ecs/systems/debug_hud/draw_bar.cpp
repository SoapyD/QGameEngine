#include "engine/ecs/systems/debug_hud/debug_hud_internal.h"

#include <glad/glad.h>

glm::vec3 healthBarColor(float percent)
{
	if (percent > 0.5f) return glm::vec3(0.0f, 0.8f, 0.0f);
	if (percent > 0.25f) return glm::vec3(0.9f, 0.9f, 0.0f);
	return glm::vec3(0.9f, 0.1f, 0.1f);
}

void drawBar
(
	float x, float y, float width, float height,
	float fillPercent,
	unsigned int shaderId, const glm::mat4& projection,
	const glm::vec3& bgColor, const glm::vec3& fgColor
)
{
	// background quad (full width)
	float bgVertices[] =
	{
		x,         y,          0.0f,
		x + width, y,          0.0f,
		x + width, y + height, 0.0f,
		x,         y,          0.0f,
		x + width, y + height, 0.0f,
		x,         y + height, 0.0f
	};

	// Foreground quad (partial width based on fillPercent)
	float fw = width * fillPercent;
	float fgVertices[] =
	{
		x,      y,          0.0f,
		x + fw, y,          0.0f,
		x + fw, y + height, 0.0f,
		x,      y,          0.0f,
		x + fw, y + height, 0.0f,
		x,      y + height, 0.0f
	};

	unsigned int vao, vbo;
	glGenVertexArrays(1, &vao);
	glGenBuffers(1, &vbo);
	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer
	(
		0, 3, GL_FLOAT, GL_FALSE,
		3 * sizeof(float), (void*)0
	);

	glUseProgram(shaderId);
	GLint loc = glGetUniformLocation(shaderId, "projection");
	glUniformMatrix4fv(loc, 1, GL_FALSE, &projection[0][0]);

	// draw background
	glBufferData(GL_ARRAY_BUFFER, sizeof(bgVertices), bgVertices, GL_DYNAMIC_DRAW);
	loc = glGetUniformLocation(shaderId, "textColor");
	glUniform3fv(loc, 1, &bgColor[0]);
	GLint alphaLoc = glGetUniformLocation(shaderId, "alpha");
	glUniform1f(alphaLoc, 1.0f);
	glDrawArrays(GL_TRIANGLES, 0, 6);

	// draw foreground
	glBufferData(GL_ARRAY_BUFFER, sizeof(fgVertices), fgVertices, GL_DYNAMIC_DRAW);
	glUniform3fv(loc, 1, &fgColor[0]);
	glDrawArrays(GL_TRIANGLES, 0, 6);

	glBindVertexArray(0);
	glDeleteBuffers(1, &vbo);
	glDeleteVertexArrays(1, &vao);
}
