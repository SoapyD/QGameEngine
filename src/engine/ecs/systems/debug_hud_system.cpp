#include "engine/ecs/systems/debug_hud_system.h"
#include "engine/ecs/components.h"
#include "engine/physics/physics_config.h"
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
	GLint alphaLoc = glGetUniformLocation(shaderId, "alpha");
	glUniform1f(alphaLoc, 1.0f);

	glDrawElements(GL_TRIANGLES,(int)indices.size(), GL_UNSIGNED_INT, 0);

	glBindVertexArray(0);
	glDeleteBuffers(1, &ebo);
	glDeleteBuffers(1, &vbo);
	glDeleteVertexArrays(1, &vao);
}

static void drawCrosshair
(
	float centreX, float centreY,
	unsigned int shaderId, const glm::mat4& projection,
	const glm::vec3& color	
)	
{
	// Crosshair: two lines with a 2px gap in the centre
	// Total width/height = 20px (10px each side, minus 2px gap)
	float gap = 2.0f;
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


static void drawBar
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

static glm::vec3 healthBarColor(float percent)
{
	if (percent > 0.5f) return glm::vec3(0.0f, 0.8f, 0.0f);
	if (percent > 0.25f) return glm::vec3(0.9f, 0.9f, 0.0f);
	return glm::vec3(0.9f, 0.1f, 0.1f);
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

	// ─── Tick damage flash ──────────────────────────────────
	float flashAlpha = 0.0f;
	{
		const auto& config = registry.ctx().get<PhysicsConfig>();
		float dt = config.fixedDeltaTime;

		auto flashView = registry.view<DamageFlash, TagPlayer>();
		for (auto [entity, flash] : flashView.each())
		{
			if (flash.timer > 0.0f)
			{
				flashAlpha = (flash.timer / flash.duration) * 0.4f;
				flash.timer -= dt;
				if (flash.timer < 0.0f) flash.timer = 0.0f;
			}
		}
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

	// ─── Health bar (bottom-left) ────────────────────────────
	float healthPercent = (maxHealth > 0.0f) ? health / maxHealth : 0.0f;
	float barX = 10.0f;
	float barY = (float)windowHeight - 30.0f;  // 30px from bottom
	float barWidth = 200.0f;
	float barHeight = 16.0f;

	drawBar
	(
		barX, barY, barWidth, barHeight,
		healthPercent, shader, ortho,
		glm::vec3(0.2f, 0.2f, 0.2f),           // dark grey background
		healthBarColor(healthPercent)             // colour based on %
	);

	// health text on top of the bar
	char healthText[64];
	snprintf
	(
		healthText, sizeof(healthText),
		"HP: %.0f /%.0f", health, maxHealth
	);
	drawText
	(
		barX + 4.0f, barY + 2.0f, healthText,
		shader, ortho, textScale, glm::vec3(0.0f)
	);

	// ─── Ammo counter (to the right of health bar) ──────────
	auto ammoView = registry.view<Ammo, WeaponInventory, TagPlayer>();
	for (auto [ entity, ammo, inv] : ammoView.each())
	{
		if (inv.weapons.empty()) continue;
		const Weapon& currentWeapon = inv.weapons[inv.currentWeapon];

		const char* weaponName = "Uknown";
		int ammoCount = 0;
		switch (currentWeapon.type)
		{
			case WeaponType::Shotgun:
			case WeaponType::SuperShotgun:
				weaponName = "Shotgun";
				ammoCount = ammo.shells;
			break;
			case WeaponType::Nailgun:
				weaponName = "Nailgun";
				ammoCount = ammo.nails;
			break;
			case WeaponType::RocketLauncher:
			case WeaponType::GrenadeLauncher:
				weaponName = "Rockets";
				ammoCount = ammo.rockets;
			break;
			case WeaponType::LighteningGun:
			case WeaponType::Railgun:
				weaponName = "Cells";
				ammoCount = ammo.cells;
			break;
		}

		char ammoText[64];
		snprintf
		(
			ammoText, sizeof(ammoText),
			"%s /%d", weaponName, ammoCount
		);
		drawText
		(
			barX + barWidth + 20.0f, barY + 2.0f, ammoText,
			shader, ortho, textScale, glm::vec3(0.0f)
		);
	}

	// ─── Crosshair at screen centre ─────────────────────────
	drawCrosshair
	(
		windowWidth * 0.5f, windowHeight * 0.5f,
		shader, ortho, glm::vec3(1.0f)  // white
	);

	// ─── Damage flash overlay ───────────────────────────────
	if (flashAlpha > 0.0f)
	{
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
	
		glUseProgram(shader);
		GLint loc = glGetUniformLocation(shader, "projection");
		glUniformMatrix4fv(loc, 1, GL_FALSE, &ortho[0][0]);

		loc = glGetUniformLocation(shader, "textColor");
		glm::vec3 red(1.0f, 0.0f, 0.0f);
		glUniform3fv(loc, 1, &red[0]);

		GLint alphaLoc = glGetUniformLocation(shader, "alpha");
		glUniform1f(alphaLoc, flashAlpha);
	
		glDrawArrays(GL_TRIANGLES, 0, 6);

		glBindVertexArray(0);
		glDeleteBuffers(1, &oVbo);
		glDeleteVertexArrays(1, &oVao);

		glDisable(GL_BLEND);
	}
	

	// Re-enable depth testing and face culling for the next frame's 3D rendering
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
}