#include "engine/ecs/systems/debug_hud/debug_hud_internal.h"

#include <glad/glad.h>
#include <cmath>

// A red chevron placed around the crosshair at `screenAngle` (0 = straight ahead
// / up on screen, +x = to the right, growing clockwise), pointing outward toward
// where the last damage came from. Fades out via `alpha`.
void drawDamageArc
(
	float centreX, float centreY, float screenAngle, float alpha,
	unsigned int shaderId, const glm::mat4& projection
)
{
	const float radius = 46.0f;   // distance from crosshair centre
	const float depth  = 10.0f;   // chevron length (tip vs base)
	const float half   = 9.0f;    // chevron half-width

	// Screen basis at the angle: "out" points away from centre (up = -y at angle 0),
	// "side" is perpendicular.
	float s = std::sin(screenAngle), c = std::cos(screenAngle);
	float outX =  s, outY = -c;    // angle 0 → (0,-1) = up on screen
	float sideX = c, sideY =  s;

	float tipX  = centreX + outX * radius;
	float tipY  = centreY + outY * radius;
	float baseX = centreX + outX * (radius - depth);
	float baseY = centreY + outY * (radius - depth);

	float vertices[] =
	{
		baseX + sideX * half, baseY + sideY * half, 0.0f,  tipX, tipY, 0.0f,   // wing → tip
		tipX, tipY, 0.0f,  baseX - sideX * half, baseY - sideY * half, 0.0f,   // tip → wing
	};

	unsigned int vao, vbo;
	glGenVertexArrays(1, &vao);
	glGenBuffers(1, &vbo);
	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

	glm::vec3 red(1.0f, 0.2f, 0.15f);
	glUseProgram(shaderId);
	glUniformMatrix4fv(glGetUniformLocation(shaderId, "projection"), 1, GL_FALSE, &projection[0][0]);
	glUniform3fv(glGetUniformLocation(shaderId, "textColor"), 1, &red[0]);
	glUniform1f(glGetUniformLocation(shaderId, "alpha"), alpha);

	glDrawArrays(GL_LINES, 0, 4);

	glBindVertexArray(0);
	glDeleteBuffers(1, &vbo);
	glDeleteVertexArrays(1, &vao);
}
