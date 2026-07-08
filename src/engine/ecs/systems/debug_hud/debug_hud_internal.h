#pragma once
// Internal drawing primitives shared across the debug HUD's split .cpp files.
// NOT the public API (that is debug_hud_system.h). Each is defined in its own file.

#include <entt/entt.hpp>
#include <glm/glm.hpp>

// Draw a scaled string at screen position (x, y) using stb_easy_font.
void drawText(float x, float y, const char* text, unsigned int shaderId,
              const glm::mat4& projection, float scale, const glm::vec3& color);

// Draw a centred crosshair (two lines gapped by `gap` px each side of centre).
void drawCrosshair(float centreX, float centreY, float gap, unsigned int shaderId,
                   const glm::mat4& projection, const glm::vec3& color);

// Draw a small diagonal "X" hit-marker over the crosshair (white = hit, red = kill).
void drawHitMarker(float centreX, float centreY, unsigned int shaderId,
                   const glm::mat4& projection, const glm::vec3& color);

// Draw a damage-direction chevron at `screenAngle` radians around the crosshair
// (0 = straight ahead/up, +x = to the right), fading with `alpha`.
void drawDamageArc(float centreX, float centreY, float screenAngle, float alpha,
                   unsigned int shaderId, const glm::mat4& projection);

// Draw a background + partial-fill bar (health bar).
void drawBar(float x, float y, float width, float height, float fillPercent,
             unsigned int shaderId, const glm::mat4& projection,
             const glm::vec3& bgColor, const glm::vec3& fgColor);

// Draw a semi-transparent filled quad (legibility backing behind text).
void drawPanel(float x, float y, float width, float height,
               unsigned int shaderId, const glm::mat4& projection,
               const glm::vec3& color, float alpha);

// Green/yellow/red based on fill percent.
glm::vec3 healthBarColor(float percent);

// Draw the current weapon's name + ammo count for the player.
void drawAmmo(entt::registry& registry, float x, float y, unsigned int shaderId,
              const glm::mat4& projection, float scale);

// Draw the top-of-screen weapon bar: slots 1-7, coloured when owned (current
// highlighted), greyed out when not yet collected.
void drawWeaponBar(entt::registry& registry, int windowWidth, unsigned int shaderId,
                   const glm::mat4& projection, float scale);

// Draw the full-screen red damage-flash overlay (no-op if flashAlpha <= 0).
void drawFlashOverlay(int windowWidth, int windowHeight, unsigned int shaderId,
                      const glm::mat4& projection, float flashAlpha);
