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

	// ─── Gather player health + armour ───────────────────────
	float health = 0.0f;
	float maxHealth = 0.0f;
	for (auto [entity, hp] : registry.view<Health, TagPlayer>().each())
	{
		health = hp.current;
		maxHealth = hp.max;
	}

	float armor = 0.0f;
	float maxArmor = 0.0f;
	for (auto [entity, ap] : registry.view<Armor, TagPlayer>().each())
	{
		armor = ap.current;
		maxArmor = ap.max;
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
	const glm::vec3 panelColor(0.0f);
	const float panelAlpha = 0.45f;

	// FPS (top-left, white) on a legibility panel
	char fpsText[64];
	snprintf(fpsText, sizeof(fpsText), "FPS: %.0f", fps);
	drawPanel(2.0f, 2.0f, 120.0f, 22.0f, shader, ortho, panelColor, panelAlpha);
	drawText(5.0f, 5.0f, fpsText, shader, ortho, textScale, glm::vec3(1.0f));

	// Bottom-left status cluster: armour bar (top), health bar (below), ammo (right).
	float barX = 10.0f;
	float barWidth = 200.0f;
	float barHeight = 16.0f;
	float barGap = 6.0f;
	float barY = (float)windowHeight - 30.0f;             // health row
	float armorY = barY - (barHeight + barGap);            // armour row above it

	// One panel behind the whole cluster.
	drawPanel(barX - 6.0f, armorY - 6.0f, barWidth + 200.0f, (barY + barHeight) - armorY + 12.0f,
		shader, ortho, panelColor, panelAlpha);

	// Armour bar (blue) + value text
	float armorPercent = (maxArmor > 0.0f) ? armor / maxArmor : 0.0f;
	drawBar(barX, armorY, barWidth, barHeight, armorPercent, shader, ortho,
		glm::vec3(0.2f, 0.2f, 0.2f), glm::vec3(0.2f, 0.5f, 1.0f));
	char armorText[64];
	snprintf(armorText, sizeof(armorText), "AP: %.0f /%.0f", armor, maxArmor);
	drawText(barX + 4.0f, armorY + 2.0f, armorText, shader, ortho, textScale, glm::vec3(1.0f));

	// Health bar + value text
	float healthPercent = (maxHealth > 0.0f) ? health / maxHealth : 0.0f;
	drawBar(barX, barY, barWidth, barHeight, healthPercent, shader, ortho,
		glm::vec3(0.2f, 0.2f, 0.2f), healthBarColor(healthPercent));
	char healthText[64];
	snprintf(healthText, sizeof(healthText), "HP: %.0f /%.0f", health, maxHealth);
	drawText(barX + 4.0f, barY + 2.0f, healthText, shader, ortho, textScale, glm::vec3(0.0f));

	// Ammo (right of the health bar)
	drawAmmo(registry, barX + barWidth + 20.0f, barY + 2.0f, shader, ortho, textScale);

	// Pickup toast (upper-centre, fades out). Ticked here like the damage flash.
	{
		float dt = registry.ctx().get<PhysicsConfig>().fixedDeltaTime;
		for (auto [entity, msg] : registry.view<PickupMessage, TagPlayer>().each())
		{
			if (msg.timer <= 0.0f) continue;

			float msgScale = 2.5f;
			// stb_easy_font glyphs are ~6px wide before scaling; enough to centre.
			float textWidth = msg.text.size() * 6.0f * msgScale;
			float x = windowWidth * 0.5f - textWidth * 0.5f;
			float y = windowHeight * 0.30f;
			drawPanel(x - 10.0f, y - 5.0f, textWidth + 20.0f, 8.0f * msgScale + 10.0f,
				shader, ortho, glm::vec3(0.0f), panelAlpha);
			drawText(x, y, msg.text.c_str(), shader, ortho, msgScale, glm::vec3(1.0f, 0.9f, 0.4f));

			msg.timer -= dt;
			if (msg.timer < 0.0f) msg.timer = 0.0f;
		}
	}

	// Crosshair (screen centre)
	drawCrosshair(windowWidth * 0.5f, windowHeight * 0.5f, shader, ortho, glm::vec3(1.0f));

	// Damage flash overlay
	drawFlashOverlay(windowWidth, windowHeight, shader, ortho, flashAlpha);

	// Restore 3D render state for the next frame.
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
}
