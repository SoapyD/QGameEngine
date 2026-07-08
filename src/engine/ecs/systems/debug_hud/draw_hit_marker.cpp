#include "engine/ecs/systems/debug_hud/debug_hud_internal.h"

#include <glad/glad.h>

// A small diagonal "X" over the crosshair — the classic "your shot connected" cue.
// Drawn white for a hit, red for a kill (colour chosen by the caller).
void drawHitMarker
(
	float centreX, float centreY,
	unsigned int shaderId, const glm::mat4& projection,
	const glm::vec3& color
)
{
	float in  = 5.0f;   // inner gap from centre
	float out = 11.0f;  // outer reach

	float vertices[] =
	{
		centreX - out, centreY - out, 0.0f,  centreX - in, centreY - in, 0.0f,  // top-left
		centreX + in,  centreY - in,  0.0f,  centreX + out, centreY - out, 0.0f, // top-right
		centreX - out, centreY + out, 0.0f,  centreX - in, centreY + in, 0.0f,   // bottom-left
		centreX + in,  centreY + in,  0.0f,  centreX + out, centreY + out, 0.0f, // bottom-right
	};

	unsigned int vao, vbo;
	glGenVertexArrays(1, &vao);
	glGenBuffers(1, &vbo);
	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

	glUseProgram(shaderId);
	glUniformMatrix4fv(glGetUniformLocation(shaderId, "projection"), 1, GL_FALSE, &projection[0][0]);
	glUniform3fv(glGetUniformLocation(shaderId, "textColor"), 1, &color[0]);
	glUniform1f(glGetUniformLocation(shaderId, "alpha"), 1.0f);

	glDrawArrays(GL_LINES, 0, 8);

	glBindVertexArray(0);
	glDeleteBuffers(1, &vbo);
	glDeleteVertexArrays(1, &vao);
}
