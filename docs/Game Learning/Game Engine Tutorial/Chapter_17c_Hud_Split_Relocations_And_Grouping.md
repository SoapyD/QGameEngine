# Chapter 17c: HUD Split, Relocations & Domain Grouping

## What You'll Learn
- Splitting `debug_hud_system` into one-draw-function-per-file with a private `_internal.h`
- The "folder = domain" rule: why a file's directory should name what the file *is*
- Relocating `jolt_body_helpers` out of `ecs/` and into `physics/bodies/`
- Splitting `physics/raycast.cpp` into one-free-function-per-file behind `raycast.h`
- Moving `scene_setup` to `app/` and `factories` / `showcase_level` / `build_sector_meshes` to `level/`
- Grouping *every* system into a domain folder under `systems/` (no loose files left)
- The two flavours of domain folder: one-system-split vs independent-systems-grouped
- The mechanical edit each move needs: self-include, `simulation.cpp`, `main.cpp`, and `CMakeLists.txt`

---

Previously (17b) we extracted types and split the combat system. Now we finish the structural work. The combat split proved a recipe — folder, private `*_internal.h`, one free function per file, thin entry point. This chapter applies that same recipe to the debug HUD, then turns to the other half of the cleanup: putting files where their *domain* lives, and grouping every remaining system into a domain folder so that `systems/` has no loose files left at all.

None of this changes behaviour. Every step is pure relocation and re-splitting — the build stays clean and all 6 headless scenarios pass identically before and after. That is the whole point: structure-only changes should be *invisible* at runtime.

---

## Step 1: Split the Debug HUD

### Why the same recipe as combat?

`debug_hud_system.cpp` was 433 lines: a single `debugHudSystem` orchestrator plus a pile of static drawing helpers — `drawText`, `drawBar`, `drawCrosshair`, `drawAmmo`, `healthBarColor`, `drawFlashOverlay`. Those helpers are *free functions*, not a class's methods. Per CODING_STANDARD §1 the "one per file" rule targets exactly this: a collection of free functions in one big file. Combat was the same shape (524 lines of free functions), and we split it cleanly. The HUD gets the identical treatment.

The recipe:

1. A `debug_hud/` folder.
2. A **private** `debug_hud_internal.h` declaring the drawing primitives shared between the split files. This is *not* the public API.
3. One draw-function per `.cpp`: `draw_text.cpp`, `draw_bar.cpp`, `draw_crosshair.cpp`, `draw_ammo.cpp`, `draw_flash_overlay.cpp`.
4. A thin `debug_hud_system.cpp` that only *orchestrates* — it calls the primitives in order and owns nothing else.
5. The unchanged public `debug_hud_system.h` stays the single entry point the rest of the engine sees.

### The private header — `debug_hud_internal.h`

Create `src/engine/ecs/systems/debug_hud/debug_hud_internal.h`:

```cpp
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
```

The functions that were `static` inside the old monolith become ordinary (external-linkage) functions declared here. The `_internal.h` name is the convention signal: anything outside `debug_hud/` must include `debug_hud_system.h`, never this file.

### The public header — `debug_hud_system.h`

This is unchanged from before the split. It is the only thing the rest of the engine includes:

```cpp
#pragma once

#include <entt/entt.hpp>

// Public entry point for the debug HUD overlay (FPS, health bar, ammo,
// crosshair, damage flash). Implementation split across systems/debug_hud/*.cpp;
// see debug_hud_internal.h for the drawing primitives.
void debugHudSystem(entt::registry& registry, int windowWidth, int windowHeight, float fps);
```

### `draw_text.cpp`

Create `src/engine/ecs/systems/debug_hud/draw_text.cpp`:

```cpp
#include "engine/ecs/systems/debug_hud/debug_hud_internal.h"

#include <glad/glad.h>
#include <stb_easy_font.h>
#include <vector>

// ─── Draw a string at screen position (x, y) ─────────────────────
// stb_easy_font outputs quads as 4 vertices each, with a stride of
// 16 bytes per vertex (x, y, z, colour as 4 floats).
// Core profile doesn't support GL_QUADS, so we convert to triangles
// using an index buffer: each quad becomes 2 triangles (6 indices).
void drawText
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
```

Note the include style: each split file includes **only** the private `debug_hud_internal.h`, plus whatever third-party headers it personally needs (`glad`, `stb_easy_font`, `vector`). It does *not* include the public `debug_hud_system.h` — that header is for callers, not for siblings.

### `draw_bar.cpp`

Create `src/engine/ecs/systems/debug_hud/draw_bar.cpp`. `healthBarColor` travels with `drawBar` because the bar is its only caller:

```cpp
#include "engine/ecs/systems/debug_hud/debug_hud_internal.h"

#include <glad/glad.h>

glm::vec3 healthBarColor(float percent)
{
	if (percent > 0.5f) return glm::vec3(0.0f, 0.8f, 0.0f);
	if (percent > 0.25f) return glm::vec3(0.9f, 0.9f, 0.0f);
	return glm::vec3(0.9f, 0.1f, 0.1f);
}

void drawBar
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
```

### `draw_crosshair.cpp`

Create `src/engine/ecs/systems/debug_hud/draw_crosshair.cpp`:

```cpp
#include "engine/ecs/systems/debug_hud/debug_hud_internal.h"

#include <glad/glad.h>

void drawCrosshair
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
```

### `draw_ammo.cpp`

Create `src/engine/ecs/systems/debug_hud/draw_ammo.cpp`. This one needs the ECS components and calls `drawText`, so it includes `components.h`:

```cpp
#include "engine/ecs/systems/debug_hud/debug_hud_internal.h"

#include "engine/ecs/components.h"

#include <cstdio>

// Draw the player's current weapon name + ammo count at (x, y).
void drawAmmo
(
	entt::registry& registry,
	float x, float y,
	unsigned int shaderId, const glm::mat4& projection, float scale
)
{
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
		drawText(x, y, ammoText, shaderId, projection, scale, glm::vec3(0.0f));
	}
}
```

### `draw_flash_overlay.cpp`

Create `src/engine/ecs/systems/debug_hud/draw_flash_overlay.cpp`:

```cpp
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
```

### The thin entry point — `debug_hud_system.cpp`

Now the orchestrator. It includes **both** the public header (it defines the public symbol) and the private header (it calls the primitives), gathers the per-frame state, then issues the draw calls in order. No drawing code lives here any more:

```cpp
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
```

Read top-to-bottom this file is now a *table of contents* for the HUD: gather state, then FPS, bar, ammo, crosshair, flash. Every "how" lives in its own sibling file. That is the deliverable of a one-function-per-file split — the orchestrator becomes legible.

---

## Step 2: Put Files Where Their Domain Lives

### Why "folder = domain"?

A file's directory should answer the question *"what kind of thing is this?"* When the answer the folder gives is wrong, every reader is misled and every `#include` lies about the architecture. Three files were sitting in the wrong domain:

- `ecs/jolt_body_helpers.*` — these create **Jolt** bodies. They are ECS-*aware* (they read `Position`/`AABBCollider` from the registry) but they are not ECS-*owned*; they are physics-domain glue. They belong in `physics/`.
- `ecs/scene_setup.*` — this orchestrates *application* startup (what entities exist when the game boots). That is an `app/` concern.
- `ecs/factories.*`, `ecs/showcase_level.*`, `ecs/build_sector_meshes.*` — these construct **level** content (entities and geometry that make up a level). They belong in `level/`.

"ECS-aware but not ECS-owned" is the key distinction. Touching the registry does not make something an ECS *system*. The fix is to move each file to the domain that *names* it, and update its include paths to match.

### Relocation table

| File | From | To | Why |
|------|------|----|-----|
| `jolt_body_helpers.h` | `ecs/` | `physics/jolt_bodies.h` | Physics glue, not ECS-owned |
| body-creation functions | (in `jolt_body_helpers.cpp`) | `physics/bodies/*.cpp` (one per fn) | One free function per file |
| `raycast.cpp` | `physics/` | `physics/raycast/ray_intersect_*.cpp` | Two free functions, one per file |
| `scene_setup.{h,cpp}` | `ecs/` | `app/` | Application startup orchestration |
| `factories.{h,cpp}` | `ecs/` | `level/` | Level-entity construction |
| `showcase_level.{h,cpp}` | `ecs/` | `level/` | Level geometry |
| `build_sector_meshes.{h,cpp}` | `ecs/` | `level/` | Level render-mesh construction |

### `jolt_body_helpers` → `physics/jolt_bodies.h` + `physics/bodies/`

The header is renamed `jolt_bodies.h`, moved into `physics/`, and its banner records the move. The five body-creation functions each split into their own file under `physics/bodies/`.

Create `src/engine/physics/jolt_bodies.h`:

```cpp
#pragma once
// Jolt body creation helpers. Each function is defined in its own file under
// physics/bodies/. (Relocated from ecs/jolt_body_helpers.h — these are
// physics-domain glue, ECS-aware but not ECS-owned. CODING_STANDARD §5.)

#include <entt/entt.hpp>
#include "engine/level/level.h"

void createLevelBodies(entt::registry& registry, const Level& level);
void createDynamicBody(entt::registry& registry, entt::entity entity);
void createKinematicBody(entt::registry& registry, entt::entity entity);
void createStaticBody(entt::registry& registry, entt::entity entity);
void createSensorBody(entt::registry& registry, entt::entity entity);
```

Now the five implementation files. Each includes `engine/physics/jolt_bodies.h` (the new path), not the old `ecs/` one.

Create `src/engine/physics/bodies/create_level_bodies.cpp`:

```cpp
#include "engine/physics/jolt_bodies.h"

#include "engine/ecs/components.h"
#include "engine/physics/jolt_world.h"

// Static Jolt bodies for the level surfaces (walls/floors/ceilings). Built from
// surface geometry, not the render mesh.
void createLevelBodies(entt::registry& registry, const Level& level)
{
    auto& jolt = registry.ctx().get<JoltWorld>();
    auto& bodyInterface = jolt.getBodyInterface();

    for (const auto& sector : level.sectors)
    {
        for (const auto& surface : sector.surfaces)
        {
            // Compute AABB from the surface vertices
            glm::vec3 surfMin = glm::min(
                glm::min(surface.vertices[0], surface.vertices[1]),
                glm::min(surface.vertices[2], surface.vertices[3])
            );
            glm::vec3 surfMax = glm::max(
                glm::max(surface.vertices[0], surface.vertices[1]),
                glm::max(surface.vertices[2], surface.vertices[3])
            );

            // Fatten thin dimensions (same as old collision system)
            for (int i = 0; i < 3; i++)
            {
                if (surfMax[i] - surfMin[i] < 0.01f)
                {
                    surfMin[i] -= 0.1f;
                    surfMax[i] += 0.1f;
                }
            }

            // Jolt box shape takes half-extents
            glm::vec3 halfExtents = (surfMax - surfMin) * 0.5f;
            glm::vec3 centre = (surfMin + surfMax) * 0.5f;

            JPH::BoxShapeSettings shapeSettings(
                JPH::Vec3(halfExtents.x, halfExtents.y, halfExtents.z)
            );
            shapeSettings.SetEmbedded();

            auto shapeResult = shapeSettings.Create();
            if (!shapeResult.IsValid()) continue;

            JPH::BodyCreationSettings bodySettings(
                shapeResult.Get(),
                JPH::RVec3(centre.x, centre.y, centre.z),
                JPH::Quat::sIdentity(),
                JPH::EMotionType::Static,
                Layers::NON_MOVING
            );

            bodyInterface.CreateAndAddBody(bodySettings, JPH::EActivation::DontActivate);
        }
    }
}
```

Create `src/engine/physics/bodies/create_dynamic_body.cpp`:

```cpp
#include "engine/physics/jolt_bodies.h"

#include "engine/ecs/components.h"
#include "engine/physics/jolt_world.h"

void createDynamicBody(entt::registry& registry, entt::entity entity)
{
    auto& jolt = registry.ctx().get<JoltWorld>();
    auto& bodyInterface = jolt.getBodyInterface();
    auto& pos = registry.get<Position>(entity);
    auto& col = registry.get<AABBCollider>(entity);

    JPH::BoxShapeSettings shapeSettings(
        JPH::Vec3(col.halfExtents.x, col.halfExtents.y, col.halfExtents.z)
    );
    shapeSettings.SetEmbedded();

    auto shapeResult = shapeSettings.Create();
    if (!shapeResult.IsValid()) return;  // half-extent below convex radius → skip rather than crash

    JPH::BodyCreationSettings bodySettings(
        shapeResult.Get(),
        JPH::RVec3(pos.value.x, pos.value.y, pos.value.z),
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Dynamic,
        Layers::MOVING
    );

    // Match our gravity strength
    bodySettings.mGravityFactor = 1.0f;

    // Set initial velocity if the entity has one
    if (registry.all_of<Velocity>(entity))
    {
        auto& vel = registry.get<Velocity>(entity);
        bodySettings.mLinearVelocity = JPH::Vec3(vel.value.x, vel.value.y, vel.value.z);
    }

    JPH::BodyID bodyId = bodyInterface.CreateAndAddBody(
        bodySettings, JPH::EActivation::Activate
    );

    registry.emplace<JoltBody>(entity, bodyId);
}
```

Create `src/engine/physics/bodies/create_kinematic_body.cpp`:

```cpp
#include "engine/physics/jolt_bodies.h"

#include "engine/ecs/components.h"
#include "engine/physics/jolt_world.h"

void createKinematicBody(entt::registry& registry, entt::entity entity)
{
    auto& jolt = registry.ctx().get<JoltWorld>();
    auto& bodyInterface = jolt.getBodyInterface();
    auto& pos = registry.get<Position>(entity);
    auto& col = registry.get<AABBCollider>(entity);

    JPH::BoxShapeSettings shapeSettings(
        JPH::Vec3(col.halfExtents.x, col.halfExtents.y, col.halfExtents.z)
    );
    shapeSettings.SetEmbedded();

    auto shapeResult = shapeSettings.Create();
    if (!shapeResult.IsValid()) return;  // half-extent below convex radius → skip rather than crash

    JPH::BodyCreationSettings bodySettings(
        shapeResult.Get(),
        JPH::RVec3(pos.value.x, pos.value.y, pos.value.z),
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Kinematic,
        Layers::MOVING
    );

    JPH::BodyID bodyId = bodyInterface.CreateAndAddBody(
        bodySettings, JPH::EActivation::Activate
    );

    registry.emplace<JoltBody>(entity, bodyId);
}
```

Create `src/engine/physics/bodies/create_static_body.cpp`:

```cpp
#include "engine/physics/jolt_bodies.h"

#include "engine/ecs/components.h"
#include "engine/physics/jolt_world.h"

void createStaticBody(entt::registry& registry, entt::entity entity)
{
    auto& jolt = registry.ctx().get<JoltWorld>();
    auto& bodyInterface = jolt.getBodyInterface();
    auto& pos = registry.get<Position>(entity);
    auto& col = registry.get<AABBCollider>(entity);

    JPH::BoxShapeSettings shapeSettings(
        JPH::Vec3(col.halfExtents.x, col.halfExtents.y, col.halfExtents.z)
    );
    shapeSettings.SetEmbedded();

    auto shapeResult = shapeSettings.Create();
    if (!shapeResult.IsValid()) return;  // half-extent below convex radius → skip rather than crash

    JPH::BodyCreationSettings bodySettings(
        shapeResult.Get(),
        JPH::RVec3(pos.value.x, pos.value.y, pos.value.z),
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Static,
        Layers::NON_MOVING
    );

    JPH::BodyID bodyId = bodyInterface.CreateAndAddBody(
        bodySettings, JPH::EActivation::DontActivate
    );

    registry.emplace<JoltBody>(entity, bodyId);
}
```

Create `src/engine/physics/bodies/create_sensor_body.cpp`:

```cpp
#include "engine/physics/jolt_bodies.h"

#include "engine/ecs/components.h"
#include "engine/physics/jolt_world.h"

void createSensorBody(entt::registry& registry, entt::entity entity)
{
    auto& jolt = registry.ctx().get<JoltWorld>();
    auto& bodyInterface = jolt.getBodyInterface();
    auto& pos = registry.get<Position>(entity);
    auto& col = registry.get<AABBCollider>(entity);

    JPH::BoxShapeSettings shapeSettings(
        JPH::Vec3(col.halfExtents.x, col.halfExtents.y, col.halfExtents.z)
    );
    shapeSettings.SetEmbedded();

    auto shapeResult = shapeSettings.Create();
    if (!shapeResult.IsValid()) return;  // half-extent below convex radius → skip rather than crash

    JPH::BodyCreationSettings bodySettings(
        shapeResult.Get(),
        JPH::RVec3(pos.value.x, pos.value.y, pos.value.z),
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Static,
        Layers::SENSOR
    );
    bodySettings.mIsSensor = true;

    JPH::BodyID bodyId = bodyInterface.CreateAndAddBody(
        bodySettings, JPH::EActivation::DontActivate
    );

    registry.emplace<JoltBody>(entity, bodyId);
}
```

Anything that previously wrote `#include "engine/ecs/jolt_body_helpers.h"` now writes `#include "engine/physics/jolt_bodies.h"`. We will see exactly this edit in `factories.cpp` and `simulation.cpp` below.

### `physics/raycast.cpp` → `physics/raycast/`

`raycast.cpp` held two independent free functions: `rayIntersectionsAABB` and `rayIntersectsTriangle`. Same rule, same split — one function per file under a `raycast/` folder, with `raycast.h` keeping the declarations as the public face.

`raycast.h` is unchanged in content (only its siblings move):

```cpp
#pragma once

#include "engine/physics/types/aabb.h"
#include "engine/physics/types/ray.h"
#include "glm/glm.hpp"
#include <optional>

std::optional<float> rayIntersectionsAABB
(
	const Ray& ray,
	const AABB& box
);

std::optional<float> rayIntersectsTriangle
(
	const Ray& ray,
	const glm::vec3 v0,
	const glm::vec3 v1,
	const glm::vec3 v2
);
```

Create `src/engine/physics/raycast/ray_intersect_aabb.cpp`:

```cpp
#include "engine/physics/raycast.h"

#include <algorithm>
#include <cmath>
#include <limits>

std::optional<float> rayIntersectionsAABB
(
	// slab method, fidn the overlap of ray intervals on each axis
	const Ray& ray,
	const AABB& box
)
{
	float tmin = 0.0f;
	float tmax = std::numeric_limits<float>::max();

	for (int i = 0; i < 3; i++)
	{
		float origin = ray.origin[i];
		float dir = ray.direction[i];
		float bmin = box.min[i];
		float bmax = box.max[i];

		if (std::abs(dir) < 1e-8f)
		{
			// ray is parallell to this axis
			if (origin < bmin || origin > bmax)
			{
				return std::nullopt; // ray misses entirely
			}
		}
		else
		{
			float t1 = (bmin - origin) / dir;
			float t2 = (bmax - origin) / dir;

			if (t1 > t2) std::swap(t1, t2);

			tmin = std::max(tmin, t1);
			tmax = std::min(tmax, t2);

			if (tmin > tmax)
			{
				return std::nullopt; // no overlap
			}
		}
	}

	if (tmin < 0.0f) return std::nullopt; // hit is behind the ray

	return tmin;
}
```

Create `src/engine/physics/raycast/ray_intersect_triangle.cpp`:

```cpp
#include "engine/physics/raycast.h"

#include <cmath>

std::optional<float> rayIntersectsTriangle
(
	const Ray& ray,
	const glm::vec3 v0,
	const glm::vec3 v1,
	const glm::vec3 v2
)
{
	const float EPSILON = 1e-7f;

	glm::vec3 edge1 = v1 - v0;
	glm::vec3 edge2 = v2 - v0;
	glm::vec3 h = glm::cross(ray.direction, edge2);
	float a = glm::dot(edge1, h);

	// ray is parallell to triangle
	if (std::abs(a) < EPSILON) return std::nullopt;

	float f = 1.0f / a;
	glm::vec3 s = ray.origin - v0;
	float u = f * glm::dot(s, h);

	if (u < 0.0f || u > 1.0f) return std::nullopt;

	glm::vec3 q = glm::cross(s, edge1);
	float v = f * glm::dot(ray.direction, q);

	if ( v < 0.0f || u + v > 1.0f) return std::nullopt;

	float t = f * glm::dot(edge2, q);

	if (t > EPSILON)
	{
		return t;
	}

	return std::nullopt; // behind the ray
}
```

Both files include `engine/physics/raycast.h` — the header stays put while its implementation fans out.

### `scene_setup` → `app/`, level files → `level/`

`scene_setup.{h,cpp}` move to `app/`; `factories.{h,cpp}`, `showcase_level.{h,cpp}` and `build_sector_meshes.{h,cpp}` move to `level/`. For the small ones we show the full file; for the large bodies (`factories.cpp`, `showcase_level.cpp`, `scene_setup.cpp`) we show only the changed include lines and signatures — the logic inside is untouched by a relocation, but the include *paths* change.

`level/factories.h` is unchanged in content; only its location and the include paths of files that reference it move. Its banner already explains its role as the `.map` loader's eventual `classname → factory` layer:

```cpp
#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include "engine/ecs/components/rendering.h"  // MeshRenderer
#include "engine/ecs/components/gameplay.h"   // TriggerAction
#include "engine/ecs/types/mesh_assets.h"

// ─────────────────────────────────────────────────────────────────────
// Entity factories
// ─────────────────────────────────────────────────────────────────────
// One function per spawnable entity type. Each builds the entity's full
// component set and returns its handle. This is the layer the TrenchBroom
// `.map` loader will map `classname` → factory onto (Phase 5), and it keeps
// scene_setup.cpp declarative (what is in the scene) rather than mechanical
// (how each entity is assembled).
//
// Physics note: factories that need a Jolt *static* or *dynamic* body create it
// here (the body can exist immediately). Kinematic bodies for movers are created
// later in buildWorld(), after the broad-phase is first optimised, so spawnMover
// only attaches the Mover/render components.

namespace factories
{
    // MeshAssets lives in ecs/types/mesh_assets.h (included above).

    // A MeshRenderer for the shared cube mesh with the given texture.
    MeshRenderer cubeRenderer(const MeshAssets& a, unsigned int textureId);

    // Player: collider, movement, health, spawn point, weapons and ammo.
    entt::entity spawnPlayer(entt::registry& reg, glm::vec3 pos);

    entt::entity spawnDirectionalLight(entt::registry& reg, glm::vec3 direction,
                                       glm::vec3 color, float ambientStrength);

    // Point light plus its small debug cube marker (same position).
    entt::entity spawnPointLight(entt::registry& reg, const MeshAssets& a, glm::vec3 pos,
                                 glm::vec3 color, float ambientStrength, float linear,
                                 float quadratic, unsigned int markerTexture);

    // Static box: renders and gets a Jolt static body.
    entt::entity spawnStaticBox(entt::registry& reg, const MeshAssets& a, glm::vec3 pos,
                                glm::vec3 scale, glm::vec3 halfExtents, unsigned int textureId);

    // Dynamic physics cube that periodically resets to its start (demo prop).
    entt::entity spawnDemoCube(entt::registry& reg, const MeshAssets& a, glm::vec3 startPos,
                               glm::vec3 startVel, float resetInterval, unsigned int textureId);

    // Kinematic mover (door / lift). Renders and gets a Mover; its Jolt
    // kinematic body is created later in buildWorld().
    entt::entity spawnMover(entt::registry& reg, const MeshAssets& a, glm::vec3 startPos,
                            glm::vec3 endPos, glm::vec3 scale, glm::vec3 halfExtents,
                            float speed, float waitTime, float startDelay, unsigned int textureId);

    // Trigger volume. `target` is the entity it activates (movers); pass
    // entt::null for teleport/damage/heal. `destination` is used by Teleport,
    // `value` by Damage/Heal.
    entt::entity spawnTrigger(entt::registry& reg, glm::vec3 pos, glm::vec3 halfExtents,
                              TriggerAction action, entt::entity target, glm::vec3 destination,
                              float value, float cooldown);

    // Green-by-default wireframe box visualising a trigger volume.
    entt::entity spawnDebugWireframe(entt::registry& reg, const MeshAssets& a, glm::vec3 pos,
                                     glm::vec3 scale, unsigned int textureId);

    // Decorative box with no physics (poles, markers, lava surface).
    entt::entity spawnDecorBox(entt::registry& reg, const MeshAssets& a, glm::vec3 pos,
                               glm::vec3 scale, unsigned int textureId);
}
```

`factories.cpp` is large — the bodies of the spawn functions are unchanged. What changes is the **self-include** (it now lives in `level/`) and the helper include (the body helper was relocated to `physics/`). The top of `level/factories.cpp` reads:

```cpp
#include "engine/level/factories.h"

#include "engine/ecs/components.h"   // spawns touch every component group
#include "engine/physics/jolt_bodies.h"
#include "engine/ecs/weapon_definitions.h"

#include <utility>
```

Two lines moved: `#include "engine/level/factories.h"` (self-include now points at `level/`, was `ecs/`), and `#include "engine/physics/jolt_bodies.h"` (was `engine/ecs/jolt_body_helpers.h`). The function bodies — `spawnPlayer`, `spawnStaticBox` calling `createStaticBody`, `spawnDemoCube` calling `createDynamicBody`, and the rest — are byte-for-byte identical.

`level/build_sector_meshes.h` is small — shown in full. It only includes the level type:

```cpp
#pragma once

#include "engine/level/level.h"

// Build the GPU render mesh for every sector in a level (one mesh per sector,
// quads → triangles). LIVE — called by createShowcaseLevel(). Extracted from
// the (legacy) level_loader so it no longer sits beside dead .qlvl code.
void buildSectorMeshes(Level& level);
```

`level/build_sector_meshes.cpp` in full — its self-include now points at `level/`:

```cpp
#include "engine/level/build_sector_meshes.h"

#include "engine/ecs/components/core.h"   // Vertex
#include "engine/renderer/mesh.h"

#include <vector>

void buildSectorMeshes(Level& level)
{
	for (auto& sector : level.sectors)
	{
		std::vector<Vertex> vertices;
		std::vector<unsigned int> indices;

		for(const auto& surface : sector.surfaces)
		{
			unsigned int baseIndex = static_cast<unsigned int>(vertices.size());

            // Calculate UV coordinates based on surface dimensions
            // Simple planar projection for now
			float uScale = glm::length(surface.vertices[1] - surface.vertices[0]);
			float vScale = glm::length(surface.vertices[3] - surface.vertices[0]);

			// four vertices for the quad
			vertices.push_back({surface.vertices[0], surface.normal, {0.0f, 0.0f}});
			vertices.push_back({surface.vertices[1], surface.normal, {uScale, 0.0f}});
			vertices.push_back({surface.vertices[2], surface.normal, {uScale, vScale}});
			vertices.push_back({surface.vertices[3], surface.normal, {0.0f, vScale}});

			// two trianges for the quad
			indices.push_back(baseIndex + 0);
			indices.push_back(baseIndex + 1);
			indices.push_back(baseIndex + 2);
			indices.push_back(baseIndex + 0);
			indices.push_back(baseIndex + 2);
			indices.push_back(baseIndex + 3);
		}

		if (!vertices.empty())
		{
			sector.mesh = std::make_unique<Mesh>(vertices, indices);
		}
	}
}
```

`showcase_level.{h,cpp}` move to `level/` too. The header signature is unchanged:

```cpp
#pragma once

#include "engine/level/level.h"

// Builds the showcase room geometry. When `headless` is true the per-sector GL
// render meshes are NOT built (buildSectorMeshes is skipped) — physics bodies
// come from the surface geometry, not the meshes, so the simulation is
// unaffected and no GL context is required.
Level createShowcaseLevel(bool headless = false);
```

In the large `showcase_level.cpp`, only the includes at the top change — the room-geometry construction is unchanged. The new header block reads:

```cpp
#include "engine/level/showcase_level.h"
#include "engine/level/level.h"
#include "engine/level/build_sector_meshes.h"
```

All three are `level/` paths now. Previously the self-include and the `build_sector_meshes` include pointed at `ecs/`.

Finally `scene_setup.{h,cpp}` move to `app/`. The header:

```cpp
#pragma once

#include <entt/entt.hpp>
#include "engine/core/resource_manager.h"
#include "engine/level/level.h"

#include <memory>

// set up the initial scene entities
// this replaces the inline entity creation that was in main.
// `headless` skips building GL render meshes for the level (no GL context).
Level setupScene
(
	entt::registry& registry,
	const ResourceManager& resources,
	bool headless = false
);
```

In `app/scene_setup.cpp`, the changed include block shows the new homes of everything it pulls in — `scene_setup.h` from `app/`, and `factories.h` / `showcase_level.h` from `level/`:

```cpp
#include "engine/app/scene_setup.h"
#include "engine/level/factories.h"
#include "engine/level/showcase_level.h"
#include "engine/ecs/components.h"
#include "engine/level/level.h"

using factories::MeshAssets;
```

The body of `setupScene` (resolving shaders/textures into `MeshAssets`, then calling the factory functions) is unchanged. The move is entirely in the include paths.

---

## Step 3: Group Systems into Domain Folders

### Why no loose files under `systems/`?

After the relocations, `systems/` still mixed split-up domain folders (`combat/`, `debug_hud/`) with loose single-file systems (`render_system.cpp`, `trigger_system.cpp`, …). That is the inconsistency this step removes. The chosen layout — borrowed from the wyrdwars `core/<domain>/` pattern — is the "aggressive" option: **every** system lives in a domain folder, so there are no loose files at all.

### The folder → systems mapping

| Folder | Systems moved in |
|--------|------------------|
| `systems/player/` | `player_character_system`, `init_player_character`, `player_input_system`, `player_death_system`, `camera_follow_system` |
| `systems/mover/` | `mover_system` |
| `systems/sync/` | `mover_sync_system`, `jolt_sync_system` (both ECS↔Jolt sync) |
| `systems/combat/` | + `weapon_switch_system` (joins the existing combat split) |
| `systems/render/` | `render_system` |
| `systems/trigger/` | `trigger_system` |
| `systems/lifetime/` | `lifetime_system` |
| `systems/demo/` | `demo_reset_system` |

### Two flavours of domain folder

These folders are not all the same kind of thing, and it is worth being precise:

1. **One system split into parts.** `combat/` and `debug_hud/` each hold *one* public system whose implementation fans out across many files behind a private `*_internal.h`. The split files include the private header; only the public `*_system.h` is visible outside.
2. **Independent systems grouped by domain.** `player/`, `sync/` hold *several distinct public systems* that share a domain. There is no private internal header — each system keeps its own public `*_system.h`. They sit together because they are *about the same thing*, not because they are one thing.

Both express the same idea ("group by domain"), and both match the wyrdwars convention. `render/`, `trigger/`, `lifetime/`, `demo/` are the degenerate case of flavour 2 — a single-file folder with no siblings yet — kept that way purely for consistency, so every system path looks the same.

> **Why not split engine classes the same way?** `Shader`, `Mesh`, `Texture`, `Camera`, `Window`, `InputManager`, `ResourceManager`, `FixedTimestep` are each kept as one whole `.h`/`.cpp`. The "one per file" rule targets *free functions*; a class's methods stay together. The big things we split (`combat_system`, `debug_hud_system`) were collections of free functions, never classes.

### The include edits each moved file needs

Moving a system to `systems/<domain>/` is mechanical but touches three kinds of include site. For example, moving `render_system` into `render/`:

1. **The self-include** inside `render_system.cpp` changes from `engine/ecs/systems/render_system.h` to `engine/ecs/systems/render/render_system.h`.
2. **`simulation.cpp`** — which drives the fixed-timestep tick — updates every system include to its new domain path. After grouping, its system includes read:

```cpp
#include "engine/ecs/systems/combat/combat_system.h"
#include "engine/ecs/systems/demo/demo_reset_system.h"
#include "engine/ecs/systems/sync/jolt_sync_system.h"
#include "engine/ecs/systems/lifetime/lifetime_system.h"
#include "engine/ecs/systems/player/player_character_system.h"
#include "engine/ecs/systems/player/player_death_system.h"
#include "engine/ecs/systems/sync/mover_sync_system.h"
#include "engine/ecs/systems/mover/mover_system.h"
#include "engine/ecs/systems/trigger/trigger_system.h"
#include "engine/ecs/systems/combat/weapon_switch_system.h"
```

3. **`main.cpp`** — the windowed entry point — updates the handful of system headers it includes directly (for the per-frame render/input/HUD path):

```cpp
#include "engine/ecs/systems/debug_hud/debug_hud_system.h"
#include "engine/ecs/systems/render/render_system.h"
#include "engine/ecs/systems/player/player_input_system.h"
#include "engine/ecs/systems/player/camera_follow_system.h"
```

The rule of thumb: when a file moves, fix *its own* self-include first, then grep `src/` for any include of the old path and repoint each one. No old-path include may remain — that is what 17d's enforcement will verify mechanically.

### The CMakeLists source-path updates

Every relocation also has to be reflected in the `add_library(qengine_lib STATIC …)` source list — CMake lists physical paths, so a moved `.cpp` is a build error until its entry is updated. After all the moves in this chapter, the source list reads:

```cmake
add_library(qengine_lib STATIC
	src/engine/app/simulation.cpp
	src/engine/core/input_manager.cpp
	src/engine/core/resource_manager.cpp
	src/engine/core/window.cpp
	src/engine/level/factories.cpp
	src/engine/physics/bodies/create_dynamic_body.cpp
	src/engine/physics/bodies/create_kinematic_body.cpp
	src/engine/physics/bodies/create_level_bodies.cpp
	src/engine/physics/bodies/create_sensor_body.cpp
	src/engine/physics/bodies/create_static_body.cpp
	src/engine/app/scene_setup.cpp
	src/engine/level/showcase_level.cpp
	src/engine/ecs/systems/player/camera_follow_system.cpp
	src/engine/ecs/systems/combat/apply_spread.cpp
	src/engine/ecs/systems/combat/box_hits_level.cpp
	src/engine/ecs/systems/combat/combat_system.cpp
	src/engine/ecs/systems/combat/fire_hitscan.cpp
	src/engine/ecs/systems/combat/fire_projectile.cpp
	src/engine/ecs/systems/combat/raycast_entities.cpp
	src/engine/ecs/systems/combat/spawn_tracer.cpp
	src/engine/ecs/systems/combat/splash_damage.cpp
	src/engine/ecs/systems/combat/update_projectiles.cpp
	src/engine/ecs/systems/debug_hud/debug_hud_system.cpp
	src/engine/ecs/systems/debug_hud/draw_ammo.cpp
	src/engine/ecs/systems/debug_hud/draw_bar.cpp
	src/engine/ecs/systems/debug_hud/draw_crosshair.cpp
	src/engine/ecs/systems/debug_hud/draw_flash_overlay.cpp
	src/engine/ecs/systems/debug_hud/draw_text.cpp
	src/engine/ecs/systems/player/player_input_system.cpp
	src/engine/ecs/systems/demo/demo_reset_system.cpp
	src/engine/ecs/systems/lifetime/lifetime_system.cpp
	src/engine/ecs/systems/sync/jolt_sync_system.cpp
	src/engine/ecs/systems/player/init_player_character.cpp
	src/engine/ecs/systems/player/player_character_system.cpp
	src/engine/ecs/systems/player/player_death_system.cpp
	src/engine/ecs/systems/sync/mover_sync_system.cpp
	src/engine/ecs/systems/mover/mover_system.cpp
	src/engine/ecs/systems/render/render_system.cpp
	src/engine/ecs/systems/trigger/trigger_system.cpp
	src/engine/level/build_sector_meshes.cpp
	src/engine/physics/jolt_world.cpp
	src/engine/physics/raycast/ray_intersect_aabb.cpp
	src/engine/physics/raycast/ray_intersect_triangle.cpp
	src/engine/renderer/camera.cpp
	src/engine/renderer/mesh.cpp
	src/engine/renderer/obj_loader.cpp
	src/engine/renderer/shader.cpp
	src/engine/renderer/stb_image_impl.cpp
	src/engine/renderer/texture.cpp
)
```

Note the debug-HUD split (six entries where there was one), the five `physics/bodies/*.cpp`, the two `physics/raycast/*.cpp`, and `factories.cpp` / `scene_setup.cpp` / `showcase_level.cpp` / `build_sector_meshes.cpp` now under their proper domains.

> **Build gotcha worth remembering.** A parallel `cmake --build` intermittently corrupts `libqengine_lib.a` (`ranlib: file truncated`) right after a large source-list change like this one. Building serially (`--parallel 1`) avoids the race, and a clean `build/CMakeFiles/qengine_lib.dir` clears an already-corrupt archive.

---

## Summary

| Area | Change |
|------|--------|
| `debug_hud/` | Split `debug_hud_system.cpp` (433 lines) into one-draw-function-per-file (`draw_text`, `draw_bar`, `draw_crosshair`, `draw_ammo`, `draw_flash_overlay`) behind a private `debug_hud_internal.h`; `debug_hud_system.cpp` becomes a thin orchestrator. |
| `physics/jolt_bodies.h` + `physics/bodies/` | Relocated `jolt_body_helpers` out of `ecs/`; split the 5 body-creation functions one-per-file. |
| `physics/raycast/` | Split `raycast.cpp` into `ray_intersect_aabb.cpp` + `ray_intersect_triangle.cpp`; `raycast.h` keeps the declarations. |
| `app/scene_setup.*` | Moved from `ecs/` — application startup orchestration. |
| `level/factories.*`, `level/showcase_level.*`, `level/build_sector_meshes.*` | Moved from `ecs/` — level-entity and geometry construction. |
| `systems/<domain>/` | Every system grouped into a domain folder (`player`, `mover`, `sync`, `combat`, `render`, `trigger`, `lifetime`, `demo`); no loose files left under `systems/`. |
| Include sites | Self-includes, `simulation.cpp`, and `main.cpp` repointed to the new paths; no old-path include remains. |
| `CMakeLists.txt` | Source list updated to the new physical locations of every moved/split `.cpp`. |

The single discipline running through all of it: **a file's folder names its domain, and the implementation of any free-function collection fans out one-per-file.** Behaviour is unchanged — the build is clean and all 6 headless scenarios pass exactly as before.

## What's Next

The structure is now correct, but nothing *enforces* it. A future commit could drop a 500-line file back into the wrong folder and the build would happily accept it. In **Chapter 17d — Enforcement & Verification** (`Chapter_17d_Enforcement_And_Verification.md`) we wire the coding-standard checker into pre-commit and CI, make the convention violations fail the build, and run the full verification pass (build + 6 scenarios + `run_all.py --strict` → 0 findings) that proves the whole 17a–17d restructuring landed cleanly.
