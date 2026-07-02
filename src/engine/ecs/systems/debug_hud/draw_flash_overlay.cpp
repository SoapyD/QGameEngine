#include "engine/ecs/systems/debug_hud/debug_hud_internal.h"

#include <glad/glad.h>

// Full-screen red damage-flash overlay, alpha-blended. No-op when not flashing.
void drawFlashOverlay
(
	int windowWidth, int windowHeight,
	unsigned int shaderId, const glm::mat4& projection, float flashAlpha
)
{
	if (flashAlpha <= 0.0f) return;

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	float overlayVerts[] =
	{
		0.0f,                  0.0f,                   0.0f,
		(float)windowWidth,    0.0f,                   0.0f,
		(float)windowWidth,    (float)windowHeight,    0.0f,
		0.0f,                  0.0f,                   0.0f,
		(float)windowWidth,    (float)windowHeight,    0.0f,
		0.0f,                  (float)windowHeight,    0.0f
	};

	unsigned int oVao, oVbo;
	glGenVertexArrays(1, &oVao);
	glGenBuffers(1, &oVbo);
	glBindVertexArray(oVao);
	glBindBuffer(GL_ARRAY_BUFFER, oVbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(overlayVerts), overlayVerts, GL_DYNAMIC_DRAW);

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
	glm::vec3 red(1.0f, 0.0f, 0.0f);
	glUniform3fv(loc, 1, &red[0]);

	GLint alphaLoc = glGetUniformLocation(shaderId, "alpha");
	glUniform1f(alphaLoc, flashAlpha);

	glDrawArrays(GL_TRIANGLES, 0, 6);

	glBindVertexArray(0);
	glDeleteBuffers(1, &oVbo);
	glDeleteVertexArrays(1, &oVao);

	glDisable(GL_BLEND);
}
