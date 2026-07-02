# Chapter 15a: HUD & Effects Cleanup

> **Prerequisites:** Chapter 15 (HUD & UI) completed. You should have a working HUD with a crosshair, health bar, ammo display, damage flash, bitmap font text rendering, and pickup messages.

---

## Time for Another Cleanup

If you have been following the cleanup pattern we established in Chapters 5a and 10a, you know the drill: the feature works, the code does not scale. Let us look at what Chapter 15 left us with.

Open your `main.cpp` and find the HUD section. You will see something like this:

```cpp
// Somewhere near the top of main() or at file scope
BitmapFont font;
font.textureId = fontTexture.getId();

HUDState hudState;

unsigned int hudVAO, hudVBO;
// ... 15 lines of glGen/glBind/glBufferData ...

// In the render section of the game loop
glDisable(GL_DEPTH_TEST);
glEnable(GL_BLEND);
glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

hudShader.use();
hudShader.setMat4("projection", orthoProjection);

drawCrosshair(hudVAO, hudShader, screenWidth, screenHeight);
drawHealthBar(hudVAO, hudShader, playerHealth, maxHealth);
drawAmmoDisplay(hudVAO, hudShader, currentAmmo, screenWidth);
drawDamageFlash(hudVAO, hudShader, damageFlashTimer, 0.3f,
                 screenWidth, screenHeight);
drawMessages(hudVAO, hudShader, font, hudState, screenWidth, dt);

glDisable(GL_BLEND);
glEnable(GL_DEPTH_TEST);
```

Count the problems:

1. **`HUDState` is a local variable, not an ECS component.** Every other piece of game state lives in the registry. The HUD lives in a loose struct in `main()`. If any system wants to post a HUD message (a pickup trigger, a damage system, a scoring system), it needs to receive `HUDState&` as a parameter. That coupling spreads like a weed.

2. **Six scattered free functions.** `drawCrosshair()`, `drawHealthBar()`, `drawAmmoDisplay()`, `drawDamageFlash()`, `drawMessages()`, plus `drawHUDQuad()`, `drawChar()`, and `drawText()`. They all take the same first two parameters (`hudVAO`, `hudShader`), which is a code smell -- if every function needs the same data, that data wants to be a member of something.

3. **`drawMessages()` mutates state during rendering.** It takes `float dt` and decrements message timers. That is update logic masquerading as a draw call. Back in Chapter 10a, we established that `Phase::Render` should be read-only. Timer updates belong in `Phase::GameLogic`.

4. **The HUD quad VAO/VBO setup is inline.** Fifteen lines of raw OpenGL in `main()`. We moved 3D mesh creation into `MeshFactory` back in Chapter 5a. The HUD quad deserves the same treatment.

5. **`BitmapFont` is a bare data struct.** The font data and the functions that use it (`drawChar`, `drawText`) are separate. They should be a single `TextRenderer` class with a coherent interface.

6. **No ResourceManager integration.** The HUD shader and font texture are created as raw locals. Every other resource goes through `ResourceManager` since Chapter 5a. The HUD should not be special.

Here is our plan:

| Problem | Solution |
|---|---|
| `HUDState` not in ECS | `HUDMessages` and `CrosshairStyle` components on the player entity |
| Scattered draw functions | `HUDRenderer` system class |
| Timer updates in render | Separate `hudUpdateSystem()` in GameLogic phase |
| Inline quad setup | `MeshFactory::createHUDQuad()` |
| Loose font struct + free functions | `TextRenderer` utility class |
| No resource caching | Load HUD shader and font texture through `ResourceManager` |

---

## C++ Concept: Separating Update from Render

This is a principle we touched on in Chapter 10a but is worth reinforcing here because the HUD code violates it directly.

The `drawMessages()` function from Chapter 15 does two things in one call:

```cpp
void drawMessages(/* ... */, float dt) {
    for (int i = static_cast<int>(hud.messages.size()) - 1; i >= 0; i--) {
        auto& msg = hud.messages[i];
        msg.timer -= dt;           // UPDATE: modifying state

        if (msg.timer <= 0.0f) {
            hud.messages.erase(...); // UPDATE: modifying state
            continue;
        }

        drawText(/* ... */);        // RENDER: drawing
    }
}
```

This is the kind of coupling that causes subtle bugs. What happens if you call this function twice in one frame (perhaps rendering to two viewports)? The timers tick down twice as fast. What if you skip rendering for a frame (minimised window)? The timers freeze. The update logic is hostage to the render schedule.

The fix is simple: split update and render into separate functions that run in their correct phases. The update function ticks timers and removes expired messages. The render function reads the current state and draws it. Neither knows or cares about the other.

```
Phase::GameLogic  →  hudUpdateSystem()   →  tick timers, remove expired messages
Phase::Render     →  hudRenderSystem()   →  read components, draw everything
```

This matches our `SystemPhase` enum from Chapter 10a perfectly. The HUD update is game logic (it decides what the player sees). The HUD render is rendering (it draws what game logic decided).

---

## Step 1: HUD Components

First, let us move all HUD state into ECS components. The `DamageFlash` component from Chapter 15 is already correct -- it lives on the player entity. We need to do the same for messages and crosshair configuration.

### Why components instead of a local struct?

When `HUDState` is a local variable in `main()`, posting a message requires threading a reference through every function call chain:

```cpp
// Before: coupling everywhere
void onPickup(HUDState& hud, /* ... */) {
    addHUDMessage(hud, "Picked up shotgun shells");
}
```

When `HUDMessages` is a component on the player entity, any system that has registry access can post a message:

```cpp
// After: decoupled
void onPickup(entt::registry& registry, entt::entity player, /* ... */) {
    auto& messages = registry.get<HUDMessages>(player);
    messages.add("Picked up shotgun shells");
}
```

This is the same principle that made `PhysicsConfig` work in Chapter 10a. The registry is the central hub for all game state. Systems that need to communicate do so through shared components, not through direct references to each other. In design pattern terms, the registry acts as a **Mediator** -- it decouples the producers of HUD messages from the consumers.

### The Components

Add these to your components header, or create a new `hud_components.h` file:

```cpp
// engine/ecs/hud_components.h
#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>

// ─── DamageFlash ─────────────────────────────────────────────────
// Already exists from Chapter 15. Included here for completeness.
// Attached to the player entity. Triggers a red screen overlay.

struct DamageFlash {
	float timer = 0.0f;
	float duration = 0.3f;
};

// ─── HUDMessage ──────────────────────────────────────────────────
// A single timed message displayed on the HUD.

struct HUDMessage {
	std::string text;
	float timer;
	float duration;
};

// ─── HUDMessages ─────────────────────────────────────────────────
// Component attached to the player entity. Stores the list of active
// on-screen messages (pickup notifications, damage indicators, etc.).

struct HUDMessages {
	std::vector<HUDMessage> messages;
	int maxMessages = 4;

	void add(const std::string& text, float duration = 2.0f)
	{
		messages.push_back({ text, duration, duration });

		// Keep only the most recent messages
		while (static_cast<int>(messages.size()) > maxMessages)
		{
			messages.erase(messages.begin());
		}
	}
};

// ─── CrosshairStyle ──────────────────────────────────────────────
// Component attached to the player entity. Defines crosshair appearance.
// Separating this into a component lets us change crosshair style at runtime
// (e.g. different weapons could have different crosshairs).

struct CrosshairStyle {
	float size = 12.0f;
	float thickness = 2.0f;
	glm::vec4 color = glm::vec4(1.0f, 1.0f, 1.0f, 0.8f);
	float gap = 0.0f;    // gap in the centre (0 = solid cross)
};
```

### Why `CrosshairStyle` is a component

You might think crosshair configuration is too trivial to be a component. But consider: when the player switches from a shotgun to a sniper rifle, the crosshair should change. When the player takes damage, the crosshair might spread. When the player is aiming down sights, the crosshair might disappear. All of these are runtime state changes, and ECS components are how we represent runtime state.

Making it a component also means the crosshair is data, not code. You could load crosshair configurations from a file, or expose them in a debug menu. You cannot easily do either of those things with hardcoded constants inside `drawCrosshair()`.

### Why `HUDMessages::add()` has a method

You might notice that `HUDMessages` has an `add()` method, which makes it slightly more than a pure data struct. This is intentional. The "keep only the most recent N messages" invariant is something that every caller would need to enforce manually if it were not encapsulated. When an invariant is simple and universal, putting it in the struct is the right call. We are not building a complex class hierarchy here -- just preventing a repeated three-line pattern from being forgotten.

---

## Step 2: TextRenderer

The bitmap font rendering code from Chapter 15 is spread across three free functions: `drawChar()`, `drawText()`, and the `BitmapFont` struct that they all take as a parameter. This is a textbook case for a class -- data and the functions that operate on it, bundled together.

### Why a class and not free functions?

The functions share state: the font texture, cell dimensions, column count, and start character. They also share a dependency on the HUD quad VAO and shader. Every function takes the same four parameters. When you see the same parameters passed to every function in a group, those parameters want to be member variables.

The `TextRenderer` also gives us a clean upgrade path. In Chapter 30, we will replace bitmap fonts with a proper FreeType-based font system. By hiding the implementation behind a `TextRenderer` interface now, that upgrade only changes one class instead of every callsite in the codebase.

### engine/renderer/text_renderer.h

```cpp
#pragma once

#include "engine/renderer/shader.h"

#include <glm/glm.hpp>
#include <string>
#include <memory>

class TextRenderer
{
public:
	// Construct with a font texture and its atlas layout.
	// The quad VAO is the same HUD quad used by the rest of the HUD system.
	TextRenderer(unsigned int quadVAO,
	             unsigned int fontTextureId,
	             int cellWidth = 8,
	             int cellHeight = 16,
	             int columns = 16,
	             int startChar = 32);

	// Draw a string at (x, y) in screen-space pixels.
	// scale multiplies the cell dimensions.
	// color tints the text (white = no tint).
	void drawText(const Shader& shader,
	              const std::string& text,
	              float x, float y,
	              float scale = 1.0f,
	              const glm::vec4& color = glm::vec4(1.0f)) const;

	// Getters for layout metrics (useful for centering text, etc.)
	int getCellWidth() const { return m_cellWidth; }
	int getCellHeight() const { return m_cellHeight; }

	// Calculate the pixel width of a string at a given scale.
	float measureText(const std::string& text, float scale = 1.0f) const;

private:
	// Draw a single character. Called internally by drawText().
	void drawChar(const Shader& shader, char c,
	              float x, float y, float scale,
	              const glm::vec4& color) const;

	unsigned int m_quadVAO;
	unsigned int m_fontTextureId;
	int m_cellWidth;
	int m_cellHeight;
	int m_columns;
	int m_startChar;
	int m_rows;

	// Precomputed atlas dimensions for UV calculations
	float m_atlasWidth;
	float m_atlasHeight;
};
```

### engine/renderer/text_renderer.cpp

```cpp
#include "engine/renderer/text_renderer.h"

#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>

TextRenderer::TextRenderer(unsigned int quadVAO,
                           unsigned int fontTextureId,
                           int cellWidth,
                           int cellHeight,
                           int columns,
                           int startChar)
	: m_quadVAO(quadVAO)
	, m_fontTextureId(fontTextureId)
	, m_cellWidth(cellWidth)
	, m_cellHeight(cellHeight)
	, m_columns(columns)
	, m_startChar(startChar)
	, m_rows(6)  // 6 rows covers ASCII 32-127 (96 printable characters)
	, m_atlasWidth(static_cast<float>(columns * cellWidth))
	, m_atlasHeight(static_cast<float>(6 * cellHeight))
{
}

void TextRenderer::drawText(const Shader& shader,
                            const std::string& text,
                            float x, float y,
                            float scale,
                            const glm::vec4& color) const
{
	// Bind the font texture once for the entire string.
	// Individual drawChar() calls reuse this binding.
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, m_fontTextureId);
	shader.setInt("useTexture", 1);

	float cursor = x;
	for (char c : text)
	{
		drawChar(shader, c, cursor, y, scale, color);
		cursor += m_cellWidth * scale;
	}
}

float TextRenderer::measureText(const std::string& text, float scale) const
{
	return static_cast<float>(text.length()) * m_cellWidth * scale;
}

void TextRenderer::drawChar(const Shader& shader, char c,
                            float x, float y, float scale,
                            const glm::vec4& color) const
{
	int index = static_cast<int>(c) - m_startChar;
	if (index < 0 || index >= m_columns * m_rows) return;

	int col = index % m_columns;
	int row = index / m_columns;

	// Calculate UV coordinates for this character in the atlas.
	// The atlas is laid out left-to-right, top-to-bottom, but OpenGL
	// UVs have (0,0) at the bottom-left, so we flip the Y.
	float u0 = (col * m_cellWidth) / m_atlasWidth;
	float v0 = 1.0f - ((row + 1) * m_cellHeight) / m_atlasHeight;
	float u1 = ((col + 1) * m_cellWidth) / m_atlasWidth;
	float v1 = 1.0f - (row * m_cellHeight) / m_atlasHeight;

	shader.setVec2("position", glm::vec2(x, y));
	shader.setVec2("size", glm::vec2(m_cellWidth * scale,
	                                   m_cellHeight * scale));
	shader.setVec4("color", color);

	// Set UV offset and scale for this character.
	// These uniforms let us select a sub-region of the atlas texture.
	shader.setVec2("uvOffset", glm::vec2(u0, v0));
	shader.setVec2("uvScale", glm::vec2(u1 - u0, v1 - v0));

	glBindVertexArray(m_quadVAO);
	glDrawArrays(GL_TRIANGLES, 0, 6);
}
```

### Updated HUD Shader (for UV sub-regions)

The `TextRenderer` uses `uvOffset` and `uvScale` uniforms to select characters from the atlas. We need to update the HUD vertex shader to support this:

```glsl
// assets/shaders/hud.vert (updated)
#version 460 core

layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;

out vec2 TexCoord;

uniform mat4 projection;
uniform vec2 position;
uniform vec2 size;
uniform vec2 uvOffset;    // NEW: UV sub-region offset (default 0,0)
uniform vec2 uvScale;     // NEW: UV sub-region scale (default 1,1)

void main() {
    vec2 screenPos = position + aPos * size;
    gl_Position = projection * vec4(screenPos, 0.0, 1.0);

    // Map the quad's 0-1 UVs to the sub-region of the atlas
    TexCoord = uvOffset + aTexCoord * uvScale;
}
```

When drawing non-text elements (crosshair, health bar), set `uvOffset = (0,0)` and `uvScale = (1,1)` to get the original full-quad behaviour. The `TextRenderer` sets them per character to select atlas sub-regions.

### Design Note: Why precompute atlas dimensions?

In the constructor, we compute `m_atlasWidth` and `m_atlasHeight` once and store them. The alternative is computing them in every `drawChar()` call -- two multiplications and two casts, repeated for every character in every string in every frame. It is a tiny cost, but there is no reason to pay it. Precomputing derived values in the constructor is a habit worth building.

---

## Step 3: HUD Quad in MeshFactory

Back in Chapter 5a, we created `MeshFactory` to centralise mesh creation. The HUD quad belongs there. It is currently 15 lines of inline OpenGL in `main()`, identical in structure to the mesh factory functions we already have.

Add this to `MeshFactory`:

### engine/core/mesh_factory.h (addition)

```cpp
namespace MeshFactory
{
	// ... existing createTriangleMesh(), createQuadMesh() ...

	// Create a 2D unit quad for HUD rendering.
	// Covers (0,0) to (1,1) with UV coordinates.
	// Layout: location 0 = vec2 position, location 1 = vec2 texcoord
	// NOTE: This is a 2D quad (vec2 positions), unlike createQuadMesh()
	// which creates a 3D quad (vec3 positions).
	MeshData createHUDQuad();
}
```

### engine/core/mesh_factory.cpp (addition)

```cpp
MeshData MeshFactory::createHUDQuad()
{
	// A unit quad from (0,0) to (1,1).
	// The HUD shader scales and positions it using uniforms.
	float vertices[] = {
		// Position    // UV
		0.0f, 0.0f,   0.0f, 0.0f,   // bottom-left
		1.0f, 0.0f,   1.0f, 0.0f,   // bottom-right
		1.0f, 1.0f,   1.0f, 1.0f,   // top-right

		0.0f, 0.0f,   0.0f, 0.0f,   // bottom-left
		1.0f, 1.0f,   1.0f, 1.0f,   // top-right
		0.0f, 1.0f,   0.0f, 1.0f    // top-left
	};

	MeshData mesh;
	mesh.vertexCount = 6;

	glGenVertexArrays(1, &mesh.vao);
	glGenBuffers(1, &mesh.vbo);

	glBindVertexArray(mesh.vao);

	glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

	// Attribute 0: Position (2 floats — this is a 2D quad)
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
		4 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);

	// Attribute 1: UV / Texcoord (2 floats)
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
		4 * sizeof(float), (void*)(2 * sizeof(float)));
	glEnableVertexAttribArray(1);

	glBindVertexArray(0);

	return mesh;
}
```

Now the HUD quad creation in `main()` goes from 15 lines to one:

```cpp
MeshData hudQuad = MeshFactory::createHUDQuad();
```

---

## Step 4: ResourceManager Integration

The HUD shader and font texture should go through the `ResourceManager`, just like every other resource. This was established in Chapter 5a, and the HUD should not be an exception.

### Before (raw locals):

```cpp
Shader hudShader("assets/shaders/hud.vert", "assets/shaders/hud.frag");
Texture fontTexture("assets/textures/font_atlas.png");
```

### After (ResourceManager):

```cpp
auto hudShader = resources.getShader("hud",
	"assets/shaders/hud.vert",
	"assets/shaders/hud.frag");

auto fontTexture = resources.getTexture("font_atlas",
	"assets/textures/font_atlas.png");
```

This gives us:

- **Caching.** If any other system needs the HUD shader (perhaps a debug overlay), it gets the cached version.
- **Named lookup.** Any system can retrieve the HUD shader with `resources.getShader("hud")` without needing a direct reference.
- **Lifetime management.** The `shared_ptr` ensures the shader and texture live as long as anyone needs them.
- **Consistency.** All resources are managed the same way. No special cases.

---

## Step 5: The HUDRenderer System

Now we consolidate all the scattered draw functions into a single system class. This is the biggest change in this chapter, so let us think about the design before writing code.

### Why a class and not a free function?

A simple `hudRenderSystem(registry)` free function would work, but the HUD renderer has persistent state: the quad VAO, the shader reference, the text renderer, and the screen dimensions. A free function would need to receive all of these as parameters, which means either a long parameter list or bundling them into a struct -- which is just a class without methods.

The `HUDRenderer` class owns the resources it needs and exposes a single `render()` method. This follows the same pattern as the `TextRenderer` we just built: bundle data with the functions that use it.

### Why not make it a system that takes only `registry`?

Some ECS purists would argue that a system should take only the registry and pull everything it needs from context variables. We could store the `TextRenderer` and shader as registry context. But these are rendering resources, not game state. The `PhysicsConfig` belongs in the registry because it is game configuration that systems modify. The HUD shader is an implementation detail of how we render -- it does not belong in the game state.

The pragmatic answer: `HUDRenderer` takes a registry and reads game state from it. Its own rendering machinery (shader, font, quad) is private implementation detail.

### engine/ecs/systems/hud_system.h

```cpp
#pragma once

#include "engine/renderer/shader.h"
#include "engine/renderer/text_renderer.h"
#include "engine/core/mesh_factory.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <memory>

// ─── HUD Update System ──────────────────────────────────────────
// Runs in Phase::GameLogic. Ticks message timers, updates damage flash,
// removes expired messages. No rendering here — just state updates.

void hudUpdateSystem(entt::registry& registry, float dt);

// ─── HUD Renderer ───────────────────────────────────────────────
// Runs in Phase::Render. Reads HUD components from the player entity
// and draws the complete HUD overlay.
//
// Replaces the scattered drawCrosshair(), drawHealthBar(),
// drawAmmoDisplay(), drawDamageFlash(), drawMessages() calls
// with a single render() method.

class HUDRenderer
{
public:
	HUDRenderer(std::shared_ptr<Shader> shader,
	            unsigned int hudQuadVAO,
	            unsigned int fontTextureId);

	// Draw the entire HUD. Call once per frame after 3D rendering.
	// Manages its own GL state (disables depth test, enables blending).
	void render(entt::registry& registry,
	            float screenWidth, float screenHeight);

private:
	// ─── Individual HUD element drawing ──────────────────────
	void drawCrosshair(float screenWidth, float screenHeight,
	                   const struct CrosshairStyle& style);

	void drawHealthBar(float currentHealth, float maxHealth);

	void drawAmmoDisplay(int ammoCount, float screenWidth);

	void drawDamageFlash(float timer, float duration,
	                     float screenWidth, float screenHeight);

	void drawMessages(const struct HUDMessages& messages,
	                  float screenWidth);

	// ─── Low-level quad drawing ──────────────────────────────
	void drawQuad(float x, float y, float width, float height,
	              const glm::vec4& color, unsigned int textureId = 0);

	// ─── Members ─────────────────────────────────────────────
	std::shared_ptr<Shader> m_shader;
	unsigned int m_hudQuadVAO;
	TextRenderer m_textRenderer;
};
```

### engine/ecs/systems/hud_system.cpp

```cpp
#include "engine/ecs/systems/hud_system.h"
#include "engine/ecs/hud_components.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <string>

// ─── HUD Update System ──────────────────────────────────────────
// Phase::GameLogic — tick timers, remove expired messages.
// This is the update logic that was previously buried inside
// drawMessages() and the render loop.

void hudUpdateSystem(entt::registry& registry, float dt)
{
	// Update damage flash timers
	auto flashView = registry.view<DamageFlash>();
	for (auto [entity, flash] : flashView.each())
	{
		if (flash.timer > 0.0f)
		{
			flash.timer -= dt;
			if (flash.timer < 0.0f)
				flash.timer = 0.0f;
		}
	}

	// Update message timers and remove expired messages
	auto msgView = registry.view<HUDMessages>();
	for (auto [entity, hudMessages] : msgView.each())
	{
		for (int i = static_cast<int>(hudMessages.messages.size()) - 1; i >= 0; i--)
		{
			hudMessages.messages[i].timer -= dt;

			if (hudMessages.messages[i].timer <= 0.0f)
			{
				hudMessages.messages.erase(hudMessages.messages.begin() + i);
			}
		}
	}
}

// ─── HUDRenderer implementation ─────────────────────────────────

HUDRenderer::HUDRenderer(std::shared_ptr<Shader> shader,
                         unsigned int hudQuadVAO,
                         unsigned int fontTextureId)
	: m_shader(std::move(shader))
	, m_hudQuadVAO(hudQuadVAO)
	, m_textRenderer(hudQuadVAO, fontTextureId)
{
}

void HUDRenderer::render(entt::registry& registry,
                         float screenWidth, float screenHeight)
{
	// ─── Set up 2D rendering state ───────────────────────────
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	m_shader->use();

	// Orthographic projection: pixel coordinates, origin at bottom-left
	glm::mat4 ortho = glm::ortho(0.0f, screenWidth,
	                               0.0f, screenHeight,
	                               -1.0f, 1.0f);
	m_shader->setMat4("projection", ortho);

	// Default UV uniforms (full quad, no atlas sub-region).
	// The TextRenderer overrides these per character.
	m_shader->setVec2("uvOffset", glm::vec2(0.0f, 0.0f));
	m_shader->setVec2("uvScale", glm::vec2(1.0f, 1.0f));

	// ─── Draw HUD elements from ECS components ───────────────
	// We iterate over entities that have the relevant components.
	// In practice, only the player entity will have these, but the
	// system does not hardcode that assumption.

	// Crosshair
	auto crosshairView = registry.view<CrosshairStyle>();
	for (auto [entity, style] : crosshairView.each())
	{
		drawCrosshair(screenWidth, screenHeight, style);
	}

	// Health bar (reads Health component — assumes float current/max)
	// For now, we draw a placeholder. Your Health component may differ.
	// Adapt this to match your project's health representation.
	auto healthView = registry.view<struct Health>();
	for (auto [entity, health] : healthView.each())
	{
		drawHealthBar(health.current, health.max);
	}

	// Ammo display
	auto ammoView = registry.view<struct Ammo>();
	for (auto [entity, ammo] : ammoView.each())
	{
		drawAmmoDisplay(ammo.count, screenWidth);
	}

	// Damage flash
	auto flashView = registry.view<DamageFlash>();
	for (auto [entity, flash] : flashView.each())
	{
		drawDamageFlash(flash.timer, flash.duration,
		                screenWidth, screenHeight);
	}

	// HUD messages
	auto msgView = registry.view<HUDMessages>();
	for (auto [entity, messages] : msgView.each())
	{
		drawMessages(messages, screenWidth);
	}

	// ─── Restore 3D rendering state ──────────────────────────
	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
}

// ─── Private drawing methods ─────────────────────────────────────

void HUDRenderer::drawCrosshair(float screenWidth, float screenHeight,
                                const CrosshairStyle& style)
{
	float cx = screenWidth / 2.0f;
	float cy = screenHeight / 2.0f;

	// Reset UV uniforms to full quad (not atlas sub-region)
	m_shader->setVec2("uvOffset", glm::vec2(0.0f, 0.0f));
	m_shader->setVec2("uvScale", glm::vec2(1.0f, 1.0f));

	// Horizontal line
	drawQuad(cx - style.size, cy - style.thickness / 2.0f,
	         style.size - style.gap, style.thickness, style.color);

	drawQuad(cx + style.gap, cy - style.thickness / 2.0f,
	         style.size - style.gap, style.thickness, style.color);

	// Vertical line
	drawQuad(cx - style.thickness / 2.0f, cy - style.size,
	         style.thickness, style.size - style.gap, style.color);

	drawQuad(cx - style.thickness / 2.0f, cy + style.gap,
	         style.thickness, style.size - style.gap, style.color);
}

void HUDRenderer::drawHealthBar(float currentHealth, float maxHealth)
{
	float barWidth = 200.0f;
	float barHeight = 20.0f;
	float x = 20.0f;
	float y = 20.0f;

	// Reset UV uniforms
	m_shader->setVec2("uvOffset", glm::vec2(0.0f, 0.0f));
	m_shader->setVec2("uvScale", glm::vec2(1.0f, 1.0f));

	// Background (dark)
	drawQuad(x, y, barWidth, barHeight,
	         glm::vec4(0.2f, 0.2f, 0.2f, 0.7f));

	// Foreground (health)
	float healthFraction = std::clamp(currentHealth / maxHealth, 0.0f, 1.0f);
	glm::vec4 healthColor;

	if (healthFraction > 0.6f)
		healthColor = glm::vec4(0.2f, 0.8f, 0.2f, 0.9f);  // Green
	else if (healthFraction > 0.3f)
		healthColor = glm::vec4(0.8f, 0.8f, 0.2f, 0.9f);  // Yellow
	else
		healthColor = glm::vec4(0.8f, 0.2f, 0.2f, 0.9f);  // Red

	drawQuad(x, y, barWidth * healthFraction, barHeight, healthColor);

	// Draw health as a number
	std::string healthText = std::to_string(static_cast<int>(currentHealth));
	m_textRenderer.drawText(*m_shader, healthText,
	                        x + 5.0f, y + 2.0f, 1.0f);
}

void HUDRenderer::drawAmmoDisplay(int ammoCount, float screenWidth)
{
	float barWidth = 150.0f;
	float barHeight = 20.0f;
	float x = screenWidth - barWidth - 20.0f;
	float y = 20.0f;

	// Reset UV uniforms
	m_shader->setVec2("uvOffset", glm::vec2(0.0f, 0.0f));
	m_shader->setVec2("uvScale", glm::vec2(1.0f, 1.0f));

	// Background
	drawQuad(x, y, barWidth, barHeight,
	         glm::vec4(0.2f, 0.2f, 0.2f, 0.7f));

	// Ammo fill
	float ammoFraction = std::clamp(static_cast<float>(ammoCount) / 50.0f,
	                                0.0f, 1.0f);
	drawQuad(x, y, barWidth * ammoFraction, barHeight,
	         glm::vec4(0.3f, 0.5f, 0.9f, 0.9f));

	// Draw ammo count as text
	std::string ammoText = std::to_string(ammoCount);
	float textWidth = m_textRenderer.measureText(ammoText, 1.0f);
	m_textRenderer.drawText(*m_shader, ammoText,
	                        x + barWidth - textWidth - 5.0f,
	                        y + 2.0f, 1.0f);
}

void HUDRenderer::drawDamageFlash(float timer, float duration,
                                  float screenWidth, float screenHeight)
{
	if (timer <= 0.0f) return;

	// Reset UV uniforms
	m_shader->setVec2("uvOffset", glm::vec2(0.0f, 0.0f));
	m_shader->setVec2("uvScale", glm::vec2(1.0f, 1.0f));

	float alpha = (timer / duration) * 0.4f;
	drawQuad(0.0f, 0.0f, screenWidth, screenHeight,
	         glm::vec4(1.0f, 0.0f, 0.0f, alpha));
}

void HUDRenderer::drawMessages(const HUDMessages& hudMessages,
                               float screenWidth)
{
	float y = 200.0f;

	for (const auto& msg : hudMessages.messages)
	{
		// Fade out during the last 0.5 seconds
		float alpha = std::min(msg.timer / 0.5f, 1.0f);
		glm::vec4 color(1.0f, 1.0f, 1.0f, alpha);

		// Centre the text horizontally
		float textWidth = m_textRenderer.measureText(msg.text, 1.5f);
		float x = (screenWidth - textWidth) / 2.0f;

		m_textRenderer.drawText(*m_shader, msg.text, x, y, 1.5f, color);
		y += 25.0f;
	}
}

void HUDRenderer::drawQuad(float x, float y, float width, float height,
                           const glm::vec4& color, unsigned int textureId)
{
	m_shader->setVec2("position", glm::vec2(x, y));
	m_shader->setVec2("size", glm::vec2(width, height));
	m_shader->setVec4("color", color);

	if (textureId != 0)
	{
		m_shader->setInt("useTexture", 1);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, textureId);
	}
	else
	{
		m_shader->setInt("useTexture", 0);
	}

	glBindVertexArray(m_hudQuadVAO);
	glDrawArrays(GL_TRIANGLES, 0, 6);
}
```

### What changed compared to Chapter 15?

Let us be specific about what moved and what is new:

| Chapter 15 | Chapter 15a | What changed |
|---|---|---|
| `drawCrosshair()` free function | `HUDRenderer::drawCrosshair()` private method | Now reads from `CrosshairStyle` component; supports gap |
| `drawHealthBar()` free function | `HUDRenderer::drawHealthBar()` private method | Also draws health number using `TextRenderer` |
| `drawAmmoDisplay()` free function | `HUDRenderer::drawAmmoDisplay()` private method | Also draws ammo count text, right-aligned |
| `drawDamageFlash()` free function | `HUDRenderer::drawDamageFlash()` private method | Identical logic, just a method now |
| `drawMessages()` free function (with `dt`!) | `HUDRenderer::drawMessages()` const method | Timer update removed -- uses `measureText()` for centering |
| Timer update inside `drawMessages()` | `hudUpdateSystem()` | Separated into Phase::GameLogic |
| `drawHUDQuad()` free function | `HUDRenderer::drawQuad()` private method | Same logic, no longer needs VAO/shader params |
| `drawChar()`, `drawText()` free functions | `TextRenderer` class | Bundled with font data, precomputed atlas dims |
| `BitmapFont` struct | Absorbed into `TextRenderer` | Data and functions unified |
| Inline HUD quad setup | `MeshFactory::createHUDQuad()` | Consistent with existing pattern |
| `HUDState` local variable | `HUDMessages` ECS component | Accessible from any system via registry |
| Hardcoded crosshair constants | `CrosshairStyle` ECS component | Data-driven, runtime-changeable |

---

## Step 6: Putting It All Together

### Updated Setup Code

Here is how the HUD initialisation looks in `main()` after the cleanup:

```cpp
// ─── Load HUD resources through ResourceManager ─────────────
auto hudShader = resources.getShader("hud",
	"assets/shaders/hud.vert",
	"assets/shaders/hud.frag");

auto fontTexture = resources.getTexture("font_atlas",
	"assets/textures/font_atlas.png");

// ─── Create HUD quad through MeshFactory ─────────────────────
MeshData hudQuad = MeshFactory::createHUDQuad();

// ─── Create HUD renderer ────────────────────────────────────
HUDRenderer hudRenderer(hudShader, hudQuad.vao, fontTexture->getId());

// ─── Attach HUD components to the player entity ─────────────
registry.emplace<DamageFlash>(player);
registry.emplace<HUDMessages>(player);
registry.emplace<CrosshairStyle>(player);
```

Compare this to the Chapter 15 setup:

```cpp
// Before: scattered, inline, no resource management
BitmapFont font;
font.textureId = fontTexture.getId();

HUDState hudState;

unsigned int hudVAO, hudVBO;
glGenVertexArrays(1, &hudVAO);
glGenBuffers(1, &hudVBO);
glBindVertexArray(hudVAO);
glBindBuffer(GL_ARRAY_BUFFER, hudVBO);
glBufferData(GL_ARRAY_BUFFER, sizeof(hudQuadVertices), hudQuadVertices, GL_STATIC_DRAW);
glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
glEnableVertexAttribArray(0);
glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                      (void*)(2 * sizeof(float)));
glEnableVertexAttribArray(1);
glBindVertexArray(0);

Shader hudShader("assets/shaders/hud.vert", "assets/shaders/hud.frag");
```

### Updated Game Loop

Here is the relevant part of the game loop, with phase comments from Chapter 10a:

```cpp
while (!window.shouldClose())
{
	fixedTimestep.accumulate();

	// -- Phase: Input --
	input.update();
	window.pollEvents();
	inputSystem(registry);

	// -- Phase: Physics (fixed timestep) --
	while (fixedTimestep.step())
	{
		gravitySystem(registry);
		movementSystem(registry);
		collisionSystem(registry);
		jumpSystem(registry);
	}

	// -- Phase: GameLogic --
	hudUpdateSystem(registry, deltaTime);    // tick HUD message timers

	// -- Phase: LateUpdate --
	cameraFollowSystem(registry);

	// -- Phase: Render --
	glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	float aspectRatio = static_cast<float>(window.getWidth())
		/ static_cast<float>(window.getHeight());
	renderSystem(registry, camera, aspectRatio);

	hudRenderer.render(registry,
		static_cast<float>(window.getWidth()),
		static_cast<float>(window.getHeight()));

	window.swapBuffers();
}
```

Compare the HUD rendering section. Before:

```cpp
glDisable(GL_DEPTH_TEST);
glEnable(GL_BLEND);
glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

hudShader.use();
hudShader.setMat4("projection", orthoProjection);

drawCrosshair(hudVAO, hudShader, screenWidth, screenHeight);
drawHealthBar(hudVAO, hudShader, playerHealth, maxHealth);
drawAmmoDisplay(hudVAO, hudShader, currentAmmo, screenWidth);
drawDamageFlash(hudVAO, hudShader, damageFlashTimer, 0.3f,
                 screenWidth, screenHeight);
drawMessages(hudVAO, hudShader, font, hudState, screenWidth, dt);

glDisable(GL_BLEND);
glEnable(GL_DEPTH_TEST);
```

After:

```cpp
hudRenderer.render(registry,
	static_cast<float>(window.getWidth()),
	static_cast<float>(window.getHeight()));
```

Twelve lines became two. The GL state management, projection setup, and individual draw calls are all encapsulated inside `HUDRenderer::render()`. The game loop reads like a description of what happens, not how it happens.

### Posting HUD Messages from Other Systems

One of the key benefits of making `HUDMessages` a component is that any system can post messages without knowing about the HUD renderer. For example, in a pickup system:

```cpp
void pickupSystem(entt::registry& registry)
{
	auto view = registry.view<TagPlayer, HUDMessages>();
	for (auto [entity, tag, messages] : view.each())
	{
		// ... collision check with pickup entity ...
		if (pickedUp)
		{
			messages.add("Picked up health pack", 2.0f);
		}
	}
}
```

Or triggering a damage flash from a damage system:

```cpp
void applyDamage(entt::registry& registry, entt::entity target, float amount)
{
	auto& health = registry.get<Health>(target);
	health.current -= amount;

	// Trigger the damage flash
	if (registry.all_of<DamageFlash>(target))
	{
		auto& flash = registry.get<DamageFlash>(target);
		flash.timer = flash.duration;
	}

	// Post a HUD message
	if (registry.all_of<HUDMessages>(target))
	{
		auto& messages = registry.get<HUDMessages>(target);
		messages.add("Took " + std::to_string(static_cast<int>(amount)) + " damage!");
	}
}
```

Neither of these functions needs a reference to `HUDRenderer`, `TextRenderer`, or any rendering code. They write to ECS components. The renderer reads those components later. This is the power of separating state from presentation.

---

## Updated File Structure

After this chapter, your project tree has these new and modified files:

```
src/
  engine/
    core/
      mesh_factory.h          ← MODIFIED: added createHUDQuad()
      mesh_factory.cpp         ← MODIFIED: added createHUDQuad()
      resource_manager.h       ← UNCHANGED (already supports shader/texture caching)
    ecs/
      hud_components.h         ← NEW: DamageFlash, HUDMessages, CrosshairStyle
      systems/
        hud_system.h           ← NEW: hudUpdateSystem() + HUDRenderer class
        hud_system.cpp         ← NEW: implementation
    renderer/
      text_renderer.h          ← NEW: TextRenderer class
      text_renderer.cpp        ← NEW: implementation
  assets/
    shaders/
      hud.vert                 ← MODIFIED: added uvOffset, uvScale uniforms
  main.cpp                     ← MODIFIED: uses HUDRenderer, components
```

Do not forget to add the new `.cpp` files to your `CMakeLists.txt`:

```cmake
add_executable(QEngine
	# ... existing files ...
	src/engine/ecs/systems/hud_system.cpp
	src/engine/renderer/text_renderer.cpp
)
```

---

## Build and Test

Rebuild the project:

```bash
cmake --build build
```

You should see the exact same HUD as before: crosshair in the centre, health bar bottom-left, ammo display bottom-right, damage flash on hit, timed messages. The behaviour is identical. The structure is fundamentally better.

If something does not render:

1. **Check the shader uniforms.** The updated `hud.vert` needs `uvOffset` and `uvScale`. If you forgot to add them, text rendering will break (all characters will show the full atlas).
2. **Check that components are attached.** If the player entity does not have `CrosshairStyle`, the crosshair will not draw. The system iterates over entities with that component -- no component, no drawing.
3. **Check the phase ordering.** `hudUpdateSystem()` must run before `hudRenderer.render()`. If you accidentally put the update after the render, messages will appear to last one frame too long.
4. **Check `uvOffset`/`uvScale` defaults.** Non-text elements (health bar, crosshair) need `uvOffset = (0,0)` and `uvScale = (1,1)`. If the shader does not have sensible defaults, add the reset calls shown in the `HUDRenderer` methods.

---

## What We Accomplished

Let us take stock. We made six changes:

1. **HUD state is now in ECS.** `HUDMessages`, `CrosshairStyle`, and `DamageFlash` are components on the player entity. Any system can read or write them through the registry. No more threading `HUDState&` through function call chains.

2. **Update logic is separated from rendering.** `hudUpdateSystem()` runs in `Phase::GameLogic` and ticks timers. `HUDRenderer::render()` runs in `Phase::Render` and only reads state. This matches the phase ordering from Chapter 10a and prevents the "timer ticks twice if rendered twice" bug.

3. **Text rendering is consolidated.** `TextRenderer` bundles font data, atlas metrics, and drawing logic into one class. All text goes through one interface. When we upgrade to FreeType in Chapter 30, we change one class.

4. **The HUD quad uses MeshFactory.** Fifteen lines of inline OpenGL replaced with `MeshFactory::createHUDQuad()`. Consistent with the pattern from Chapter 5a.

5. **Resources go through ResourceManager.** The HUD shader and font texture are cached and named, like every other resource in the engine.

6. **The game loop is shorter and clearer.** Two lines replace twelve. The loop reads as intent, not implementation.

No new features. No new visual output. Just better foundations for the features ahead. In Chapter 16, when we add audio, the pattern for "system posts event, HUD displays it" is already in place. In Chapter 30, when we upgrade fonts, the `TextRenderer` interface is ready. And if you ever need to support multiple HUD styles (split-screen, spectator mode, VR), the component-based approach makes it straightforward -- different entities, different components, same renderer.

That is the recurring theme of these cleanup chapters: spend a little time now to save a lot of time later.

---

## Exercises

1. **Weapon-specific crosshairs.** Add a `Weapon` component with a `CrosshairStyle` override. When the player switches weapons, update the `CrosshairStyle` component on the player entity. Test with at least two different styles (e.g. a tight dot for a sniper and a wide cross for a shotgun).

2. **Animated damage flash.** Instead of a simple linear fade, make the damage flash pulse. Modify `drawDamageFlash()` to use a sine wave on the alpha value: `alpha = sin(timer / duration * pi) * 0.4f`. Think about whether this logic belongs in the update system or the renderer (hint: the alpha computation is purely visual, so the renderer is fine).

3. **Message stacking direction.** Currently, messages stack upward from `y = 200`. Add a `bool stackDownward` field to `HUDMessages` and make `drawMessages()` respect it. This is a good test of whether your component design is flexible enough.

4. **Debug text overlay.** Create a `DebugOverlay` component that stores a `std::vector<std::string>` of debug lines (FPS, entity count, physics config values). Write a system that populates it each frame, and extend `HUDRenderer::render()` to draw it in the top-left corner. This exercises the same pattern: component holds data, renderer displays it.

5. **Centre the health number.** Right now, the health text is drawn at a fixed offset. Use `TextRenderer::measureText()` to centre the number within the health bar, both horizontally and vertically. This is a small change but exercises the measurement API.

---

*Next up: **Chapter 16 -- Audio**, where we will add sound effects and discover that our new HUD message system makes "Picked up shotgun shells" notifications trivial to trigger from the audio/pickup systems.*
