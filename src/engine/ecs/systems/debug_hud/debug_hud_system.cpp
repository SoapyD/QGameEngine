#include "engine/ecs/systems/debug_hud/debug_hud_system.h"
#include "engine/ecs/systems/debug_hud/debug_hud_internal.h"

#include "engine/ecs/components.h"
#include "engine/physics/physics_config.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cstdio>

// ─── Debug HUD overlay ──────────────────────────────────────────
// Orchestrates the 2D overlay: FPS, health bar, ammo, crosshair, damage flash.
// The drawing primitives live in the sibling files (draw_text, draw_bar, ...).
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
	unsigned int shader = hudConfig->shaderId;

    // Orthographic projection: origin top-left, Y increases downward.
	glm::mat4 ortho = glm::ortho
	(
		0.0f, (float)windowWidth, (float)windowHeight,
		0.0f, -1.0f, 1.0f
	);

	// HUD overlay: no depth test / face culling.
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);

	// ─── Gather player health ────────────────────────────────
	float health = 0.0f;
	float maxHealth = 0.0f;
	for (auto [entity, hp] : registry.view<Health, TagPlayer>().each())
	{
		health = hp.current;
		maxHealth = hp.max;
	}

	// ─── Tick damage flash ───────────────────────────────────
	float flashAlpha = 0.0f;
	{
		float dt = registry.ctx().get<PhysicsConfig>().fixedDeltaTime;
		for (auto [entity, flash] : registry.view<DamageFlash, TagPlayer>().each())
		{
			if (flash.timer > 0.0f)
			{
				flashAlpha = (flash.timer / flash.duration) * 0.4f;
				flash.timer -= dt;
				if (flash.timer < 0.0f) flash.timer = 0.0f;
			}
		}
	}

	float textScale = 2.0f; // stb_easy_font is tiny — scale it up

	// FPS (top-left, white)
	char fpsText[64];
	snprintf(fpsText, sizeof(fpsText), "FPS: %.0f", fps);
	drawText(5.0f, 5.0f, fpsText, shader, ortho, textScale, glm::vec3(1.0f));

	// Health bar (bottom-left) + value text
	float healthPercent = (maxHealth > 0.0f) ? health / maxHealth : 0.0f;
	float barX = 10.0f;
	float barY = (float)windowHeight - 30.0f;
	float barWidth = 200.0f;
	float barHeight = 16.0f;
	drawBar(barX, barY, barWidth, barHeight, healthPercent, shader, ortho,
		glm::vec3(0.2f, 0.2f, 0.2f), healthBarColor(healthPercent));

	char healthText[64];
	snprintf(healthText, sizeof(healthText), "HP: %.0f /%.0f", health, maxHealth);
	drawText(barX + 4.0f, barY + 2.0f, healthText, shader, ortho, textScale, glm::vec3(0.0f));

	// Ammo (right of the health bar)
	drawAmmo(registry, barX + barWidth + 20.0f, barY + 2.0f, shader, ortho, textScale);

	// Crosshair (screen centre)
	drawCrosshair(windowWidth * 0.5f, windowHeight * 0.5f, shader, ortho, glm::vec3(1.0f));

	// Damage flash overlay
	drawFlashOverlay(windowWidth, windowHeight, shader, ortho, flashAlpha);

	// Restore 3D render state for the next frame.
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
}
