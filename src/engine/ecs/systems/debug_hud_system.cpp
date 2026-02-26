#include "engine/ecs/systems/debug_hud_system.h"
#include "engine/ecs/components.h"
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <stb_easy_font.h>
#include <cstdio>
#include <vector>

// ─── Internal: draw a string at screen position (x, y) ──────────
// stb_easy_font outputs quads as 4 vertices each, with a stride of
// 16 bytes per vertex (x, y, z, colour as 4 floats).
// Core profile doesn't support GL_QUADS, so we convert to triangles
// using an index buffer: each quad becomes 2 triangles (6 indices).

static void drawText
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

	glDrawElements(GL_TRIANGLES,(int)indices.size(), GL_UNSIGNED_INT, 0);

	glBindVertexArray(0);
	glDeleteBuffers(1, &ebo);
	glDeleteBuffers(1, &vbo);
	glDeleteVertexArrays(1, &vao);
}


void debugHudSystem
(
	entt::registry& registry,
	int windowWidth,
	int windowHeight,
	float fps
)
{
	auto* hudConfig = registry.ctx().find<HudConfig>();
	if (!hudConfig || hudConfig->shaderId == 0) return;

    // ─── Set up orthographic projection for 2D rendering ─────
    // Origin at top-left, Y increases downward (screen space)
	glm::mat4 ortho = glm::ortho
	(
		0.0f, (float)windowWidth, (float)windowHeight,
		0.0f, -1.0f, 1.0f
	);

	// disable depth testing and face culling for HUD overlay
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);

	// ─── Gather debug info ───────────────────────────────────
	float health = 0.0f;
	float maxHealth = 0.0f;

	auto playerView = registry.view<Health, TagPlayer>();
	for (auto [entity, hp] : playerView.each())
	{
		health = hp.current;
		maxHealth = hp.max;
	}

	// ─── Build and draw text strings ─────────────────────────
	float textScale = 2.0f; // stb_easy_font is tiny — scale it up
	unsigned int shader = hudConfig->shaderId;

	// FPS — always white
	char fpsText[64];
	snprintf(fpsText, sizeof(fpsText), "FPS: %.0f", fps);
	drawText
	(
		5.0f, 5.0f, fpsText,
		shader, ortho, textScale, glm::vec3(1.0f)
	);

	// health - yellow when low, red when critical
	char healthText[64];
	snprintf
	(
		healthText, sizeof(healthText),
		"HP: %.0f / %.0f",
		health, maxHealth
	);

	glm::vec3 healthColor(1.0f); // white
	if (health <= 0.0f)
		healthColor = glm::vec3(1.0f, 0.0f, 0.0f); // red — dead
	else if (health < 30.0f)
		healthColor = glm::vec3(1.0f, 1.0f, 0.0f); // yellow - danger

	drawText
	(
		5.0f, 20.0f, healthText, shader,
		ortho, textScale, healthColor
	);

	// Re-enable depth testing and face culling for the next frame's 3D rendering
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
}