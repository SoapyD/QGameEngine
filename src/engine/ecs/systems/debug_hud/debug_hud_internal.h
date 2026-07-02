#pragma once
// Internal drawing primitives shared across the debug HUD's split .cpp files.
// NOT the public API (that is debug_hud_system.h). Each is defined in its own file.

#include <entt/entt.hpp>
#include <glm/glm.hpp>

// Draw a scaled string at screen position (x, y) using stb_easy_font.
void drawText(float x, float y, const char* text, unsigned int shaderId,
              const glm::mat4& projection, float scale, const glm::vec3& color);

// Draw a centred crosshair (two gapped lines).
void drawCrosshair(float centreX, float centreY, unsigned int shaderId,
                   const glm::mat4& projection, const glm::vec3& color);

// Draw a background + partial-fill bar (health bar).
void drawBar(float x, float y, float width, float height, float fillPercent,
             unsigned int shaderId, const glm::mat4& projection,
             const glm::vec3& bgColor, const glm::vec3& fgColor);

// Green/yellow/red based on fill percent.
glm::vec3 healthBarColor(float percent);

// Draw the current weapon's name + ammo count for the player.
void drawAmmo(entt::registry& registry, float x, float y, unsigned int shaderId,
              const glm::mat4& projection, float scale);

// Draw the full-screen red damage-flash overlay (no-op if flashAlpha <= 0).
void drawFlashOverlay(int windowWidth, int windowHeight, unsigned int shaderId,
                      const glm::mat4& projection, float flashAlpha);
