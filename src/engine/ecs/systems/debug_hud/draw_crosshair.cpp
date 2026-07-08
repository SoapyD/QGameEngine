#include "engine/ecs/systems/debug_hud/debug_hud_internal.h"

#include <glad/glad.h>

void drawCrosshair
(
	float centreX, float centreY, float gap,
	unsigned int shaderId, const glm::mat4& projection,
	const glm::vec3& color
)
{
	// Crosshair: two lines gapped by `gap` px each side of centre. The gap grows
	// with weapon spread / movement / recoil (see hudSignalSystem).
	float arm = 10.0f;

	// 4 vertices: horizontal line (2) + vertical line (2)
	float vertices[] =
	{
		// horizontal line
		centreX - arm, centreY, 0.0f,
		centreX - gap, centreY, 0.0f,
		centreX + gap, centreY, 0.0f,
		centreX + arm, centreY, 0.0f,
		// vertical line
		centreX, centreY - arm, 0.0f,
		centreX, centreY - gap, 0.0f,
		centreX, centreY + gap, 0.0f,
		centreX, centreY + arm, 0.0f
	};

	unsigned int vao, vbo;
	glGenVertexArrays(1, &vao);
	glGenBuffers(1, &vbo);

	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer
	(
		0, 3, GL_FLOAT, GL_FALSE,
		3 * sizeof(float), (void*)0
	);

	glUseProgram(shaderId);

	GLint loc = glGetUniformLocation(shaderId, "projection");
	glUniformMatrix4fv(loc, 1, GL_FALSE, &projection[0][0]);

	loc = glGetUniformLocation(shaderId, "textColor");
	glUniform3fv(loc, 1, &color[0]);
	GLint alphaLoc = glGetUniformLocation(shaderId, "alpha");
	glUniform1f(alphaLoc, 1.0f);

	// Draw as 4 separate line segments (GL_LINES draws pairs)
	glDrawArrays(GL_LINES, 0, 8);

	glBindVertexArray(0);
	glDeleteBuffers(1, &vbo);
	glDeleteVertexArrays(1, &vao);
}
