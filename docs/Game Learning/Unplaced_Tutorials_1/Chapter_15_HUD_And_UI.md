# Chapter 15: HUD & UI

## What You'll Learn
- Orthographic projection — rendering 2D on top of 3D
- Drawing quads for HUD elements
- A crosshair
- Health and ammo display
- On-screen messages (pickup notifications, damage indicators)
- Text rendering basics (bitmap fonts)

---

## 2D in a 3D Engine

The game world uses perspective projection (things get smaller with distance). The HUD needs **orthographic projection** — no perspective, just flat 2D. Pixel coordinates map directly to screen position.

```
Perspective (3D world):          Orthographic (HUD):
   ╱        ╲                    ┌──────────────────┐
  ╱   far    ╲                   │ HP: 100  Ammo: 25│
 ╱            ╲                  │                    │
╱    near      ╲                 │       +            │
──────────────────               │                    │
 Things shrink                   └──────────────────┘
 with distance                   Flat, pixel-perfect
```

### Setting Up Orthographic Projection

```cpp
// Screen dimensions in pixels
glm::mat4 ortho = glm::ortho(0.0f, 1280.0f,  // left, right
                               0.0f, 720.0f,   // bottom, top
                               -1.0f, 1.0f);   // near, far
```

In this projection:
- `(0, 0)` = bottom-left of the screen
- `(1280, 720)` = top-right
- No view matrix needed (camera is irrelevant for HUD)
- No model matrix needed for static elements

---

## HUD Shader

### assets/shaders/hud.vert

```glsl
#version 460 core

layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;

out vec2 TexCoord;

uniform mat4 projection;
uniform vec2 position;    // Screen-space position of the element
uniform vec2 size;         // Size in pixels

void main() {
    vec2 screenPos = position + aPos * size;
    gl_Position = projection * vec4(screenPos, 0.0, 1.0);
    TexCoord = aTexCoord;
}
```

### assets/shaders/hud.frag

```glsl
#version 460 core

in vec2 TexCoord;
out vec4 FragColor;

uniform sampler2D hudTexture;
uniform vec4 color;        // Tint colour (white = no tint)
uniform bool useTexture;   // false = solid colour, true = textured

void main() {
    if (useTexture) {
        vec4 texColor = texture(hudTexture, TexCoord);
        FragColor = texColor * color;
    } else {
        FragColor = color;
    }
}
```

---

## A HUD Quad

Every HUD element (health bar, crosshair, icon) is a textured quad. We create one quad VAO and reuse it for everything:

```cpp
// A unit quad from (0,0) to (1,1)
float hudQuadVertices[] = {
    // Position    // UV
    0.0f, 0.0f,   0.0f, 0.0f,   // bottom-left
    1.0f, 0.0f,   1.0f, 0.0f,   // bottom-right
    1.0f, 1.0f,   1.0f, 1.0f,   // top-right

    0.0f, 0.0f,   0.0f, 0.0f,   // bottom-left
    1.0f, 1.0f,   1.0f, 1.0f,   // top-right
    0.0f, 1.0f,   0.0f, 1.0f    // top-left
};

unsigned int hudVAO, hudVBO;
glGenVertexArrays(1, &hudVAO);
glGenBuffers(1, &hudVBO);

glBindVertexArray(hudVAO);
glBindBuffer(GL_ARRAY_BUFFER, hudVBO);
glBufferData(GL_ARRAY_BUFFER, sizeof(hudQuadVertices), hudQuadVertices, GL_STATIC_DRAW);

// Position: 2 floats
glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
glEnableVertexAttribArray(0);

// UV: 2 floats
glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                      (void*)(2 * sizeof(float)));
glEnableVertexAttribArray(1);

glBindVertexArray(0);
```

### Drawing a HUD Element

```cpp
void drawHUDQuad(unsigned int hudVAO, const Shader& hudShader,
                  float x, float y, float width, float height,
                  const glm::vec4& color, unsigned int textureId = 0) {

    hudShader.use();
    hudShader.setVec2("position", glm::vec2(x, y));
    hudShader.setVec2("size", glm::vec2(width, height));
    hudShader.setVec4("color", color);

    if (textureId != 0) {
        hudShader.setInt("useTexture", 1);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textureId);
    } else {
        hudShader.setInt("useTexture", 0);
    }

    glBindVertexArray(hudVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}
```

Add `setVec2` and `setVec4` to the Shader class. Declare them in `src/engine/renderer/shader.h` and implement them in `src/engine/renderer/shader.cpp`:

```cpp
// In shader.cpp
void Shader::setVec2(const std::string& name, const glm::vec2& value) const {
    glUniform2fv(glGetUniformLocation(m_programId, name.c_str()), 1,
                 glm::value_ptr(value));
}

void Shader::setVec4(const std::string& name, const glm::vec4& value) const {
    glUniform4fv(glGetUniformLocation(m_programId, name.c_str()), 1,
                 glm::value_ptr(value));
}
```

---

## The Crosshair

A simple crosshair — two thin rectangles in the centre of the screen:

```cpp
void drawCrosshair(unsigned int hudVAO, const Shader& hudShader,
                    float screenWidth, float screenHeight) {
    float cx = screenWidth / 2.0f;
    float cy = screenHeight / 2.0f;
    float size = 12.0f;
    float thickness = 2.0f;
    glm::vec4 white(1.0f, 1.0f, 1.0f, 0.8f);

    // Horizontal line
    drawHUDQuad(hudVAO, hudShader,
                cx - size, cy - thickness / 2.0f,
                size * 2.0f, thickness, white);

    // Vertical line
    drawHUDQuad(hudVAO, hudShader,
                cx - thickness / 2.0f, cy - size,
                thickness, size * 2.0f, white);
}
```

Or load a crosshair texture and draw a single textured quad.

---

## Health Bar

A Quake-style health display — a number in the bottom-left with a coloured backing:

```cpp
void drawHealthBar(unsigned int hudVAO, const Shader& hudShader,
                    float currentHealth, float maxHealth) {
    float barWidth = 200.0f;
    float barHeight = 20.0f;
    float x = 20.0f;
    float y = 20.0f;

    // Background (dark)
    drawHUDQuad(hudVAO, hudShader, x, y, barWidth, barHeight,
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

    drawHUDQuad(hudVAO, hudShader, x, y,
                barWidth * healthFraction, barHeight, healthColor);
}
```

---

## Ammo Counter

```cpp
void drawAmmoDisplay(unsigned int hudVAO, const Shader& hudShader,
                      int ammoCount, float screenWidth) {
    float barWidth = 150.0f;
    float barHeight = 20.0f;
    float x = screenWidth - barWidth - 20.0f;  // Right-aligned
    float y = 20.0f;

    // Background
    drawHUDQuad(hudVAO, hudShader, x, y, barWidth, barHeight,
                glm::vec4(0.2f, 0.2f, 0.2f, 0.7f));

    // Ammo (simple: fill bar based on ammo, max assumed 50)
    float ammoFraction = std::clamp(static_cast<float>(ammoCount) / 50.0f, 0.0f, 1.0f);
    drawHUDQuad(hudVAO, hudShader, x, y,
                barWidth * ammoFraction, barHeight,
                glm::vec4(0.3f, 0.5f, 0.9f, 0.9f));
}
```

---

## Bitmap Font Text Rendering

For displaying actual numbers and text, we need a font. The simplest approach: a **bitmap font** — a texture atlas where each character is at a known position.

### The Font Atlas

A bitmap font atlas is a texture containing all printable ASCII characters in a grid:

```
 !"#$%&'()*+,-./
0123456789:;<=>?
@ABCDEFGHIJKLMNO
PQRSTUVWXYZ[\]^_
`abcdefghijklmno
pqrstuvwxyz{|}~
```

Each character occupies a fixed-width cell (e.g. 8x16 pixels in a 128x96 texture for 16 columns x 6 rows).

### Font Data

```cpp
struct BitmapFont {
    unsigned int textureId;
    int cellWidth = 8;
    int cellHeight = 16;
    int columns = 16;        // Characters per row in the atlas
    int startChar = 32;      // ASCII 32 = space (first printable character)
};
```

### Drawing a Character

```cpp
void drawChar(unsigned int hudVAO, const Shader& hudShader,
               const BitmapFont& font, char c,
               float x, float y, float scale) {

    int index = static_cast<int>(c) - font.startChar;
    if (index < 0) return;

    int col = index % font.columns;
    int row = index / font.columns;

    // Calculate UV coordinates for this character in the atlas
    float atlasWidth = static_cast<float>(font.columns * font.cellWidth);
    float atlasHeight = static_cast<float>(6 * font.cellHeight);  // 6 rows

    float u0 = (col * font.cellWidth) / atlasWidth;
    float v0 = 1.0f - ((row + 1) * font.cellHeight) / atlasHeight;  // Flip Y
    float u1 = ((col + 1) * font.cellWidth) / atlasWidth;
    float v1 = 1.0f - (row * font.cellHeight) / atlasHeight;

    // For proper UV mapping, we'd need a more flexible quad
    // or set uniforms for UV offset/scale.
    // Simplified: use uniforms

    hudShader.use();
    hudShader.setVec2("position", glm::vec2(x, y));
    hudShader.setVec2("size", glm::vec2(font.cellWidth * scale,
                                         font.cellHeight * scale));
    hudShader.setVec4("color", glm::vec4(1.0f));
    hudShader.setInt("useTexture", 1);

    // You'd set UV offset/scale uniforms here
    // (requires adding uvOffset, uvScale uniforms to the shader)

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, font.textureId);
    glBindVertexArray(hudVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}
```

### Drawing a String

```cpp
void drawText(unsigned int hudVAO, const Shader& hudShader,
               const BitmapFont& font, const std::string& text,
               float x, float y, float scale) {

    float cursor = x;
    for (char c : text) {
        drawChar(hudVAO, hudShader, font, c, cursor, y, scale);
        cursor += font.cellWidth * scale;
    }
}
```

### Drawing Health as a Number

```cpp
// In the HUD rendering:
std::string healthText = std::to_string(static_cast<int>(currentHealth));
drawText(hudVAO, hudShader, font, healthText, 25.0f, 25.0f, 2.0f);

std::string ammoText = std::to_string(ammoCount);
drawText(hudVAO, hudShader, font, ammoText,
         screenWidth - 100.0f, 25.0f, 2.0f);
```

---

## Damage Indicator

A brief red flash when the player takes damage. Add a component:

```cpp
struct DamageFlash {
    float timer = 0.0f;
    float duration = 0.3f;
};
```

When the player takes damage:
```cpp
if (registry.all_of<DamageFlash>(player)) {
    registry.get<DamageFlash>(player).timer =
        registry.get<DamageFlash>(player).duration;
} else {
    registry.emplace<DamageFlash>(player, 0.3f, 0.3f);
}
```

Draw a screen-wide red overlay:
```cpp
void drawDamageFlash(unsigned int hudVAO, const Shader& hudShader,
                      float timer, float duration,
                      float screenWidth, float screenHeight) {
    if (timer <= 0.0f) return;

    float alpha = (timer / duration) * 0.4f;  // Fades out
    drawHUDQuad(hudVAO, hudShader, 0.0f, 0.0f, screenWidth, screenHeight,
                glm::vec4(1.0f, 0.0f, 0.0f, alpha));
}
```

---

## Pickup Messages

When the player picks up an item, show a brief message:

```cpp
struct HUDMessage {
    std::string text;
    float timer;
    float duration;
};

// Store recent messages
struct HUDState {
    std::vector<HUDMessage> messages;
};
```

```cpp
void addHUDMessage(HUDState& hud, const std::string& text, float duration = 2.0f) {
    hud.messages.push_back({ text, duration, duration });

    // Keep only last 4 messages
    if (hud.messages.size() > 4) {
        hud.messages.erase(hud.messages.begin());
    }
}

void drawMessages(unsigned int hudVAO, const Shader& hudShader,
                   const BitmapFont& font, HUDState& hud,
                   float screenWidth, float dt) {
    float y = 200.0f;  // Above the bottom bar

    for (int i = static_cast<int>(hud.messages.size()) - 1; i >= 0; i--) {
        auto& msg = hud.messages[i];
        msg.timer -= dt;

        if (msg.timer <= 0.0f) {
            hud.messages.erase(hud.messages.begin() + i);
            continue;
        }

        float alpha = std::min(msg.timer / 0.5f, 1.0f);  // Fade out last 0.5s
        // Draw text with alpha (would need colour tint in drawText)
        drawText(hudVAO, hudShader, font, msg.text,
                 screenWidth / 2.0f - msg.text.length() * 4.0f,
                 y, 1.5f);
        y += 25.0f;
    }
}
```

---

## Rendering Order — HUD Last

The HUD renders **after** the 3D scene, with depth testing disabled:

```cpp
// ─── Render 3D world ─────────────────────────────────────────
glEnable(GL_DEPTH_TEST);
renderSystem(registry, camera, aspectRatio);

// ─── Render HUD ──────────────────────────────────────────────
glDisable(GL_DEPTH_TEST);    // HUD is always on top
glEnable(GL_BLEND);           // Enable transparency
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
glEnable(GL_DEPTH_TEST);     // Restore for next frame
```

### Alpha Blending

```cpp
glEnable(GL_BLEND);
glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
```

This enables transparency. When a fragment has `alpha = 0.5`, the result is:

```
output = source.rgb * source.alpha + destination.rgb * (1 - source.alpha)
```

So a 50% transparent red HUD element blended over the 3D scene produces a reddish tint. This is how the damage flash and semi-transparent backgrounds work.

---

## What's Next

In **Chapter 16**, we'll add audio — sound effects for weapons, footsteps, pickups, and ambient sounds, using miniaudio for 3D positional audio.
