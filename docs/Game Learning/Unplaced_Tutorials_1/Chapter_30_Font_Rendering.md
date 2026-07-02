# Chapter 30: Font Rendering

## What You'll Learn
- Why the simple bitmap approach from Chapter 15 falls short for real games
- Two approaches to font rendering: bitmap font atlases and FreeType rasterisation
- Building a bitmap font atlas with fixed-width glyphs and UV calculation
- Integrating FreeType to load TrueType/OpenType fonts at any pixel size
- Glyph metrics: bearing, advance, baseline, and how they drive text layout
- Packing rasterised glyphs into a single GPU texture atlas at startup
- A complete `Font` class that loads fonts and measures text
- A batched `TextRenderer` that draws entire strings in one draw call
- Text shaders that use a single-channel atlas as alpha
- Utility functions for alignment and word wrapping
- Updating the HUD, console, and menu systems to use the new renderer

---

## Why Proper Font Rendering

In Chapter 15 we got text on screen with a basic bitmap approach -- enough to show "HEALTH: 100" in a fixed font. But real games need more:

- **Variable text**: player names, chat messages, item descriptions, damage numbers
- **Multiple sizes**: tiny labels on the minimap, large title text on the menu
- **Readability**: anti-aliased edges, correct spacing, proportional widths
- **Performance**: rendering hundreds of characters per frame without stuttering

There are two main approaches, and we will cover both.

**Bitmap fonts** are the simpler path. You create (or download) a texture containing every glyph arranged in a grid, then render quads textured with the right portion of that image. This is fast, easy to implement, and gives a retro pixel-art look. The downside: you are locked to one size, and scaling looks blurry or blocky.

**FreeType fonts** are the professional path. The FreeType library rasterises TrueType and OpenType font files (.ttf, .otf) into pixel data at any size you choose. You get proper kerning, anti-aliasing, and proportional widths. Every major game engine uses this approach (or a derivative like MSDF).

We will implement both, then build a unified `TextRenderer` that works with either.

---

## Bitmap Font Atlas

The simplest font atlas is a texture containing all 256 ASCII characters arranged in a 16x16 grid. Each cell is the same size -- say 8x16 pixels for an 8-pixel-wide, 16-pixel-tall monospace font.

```
Bitmap font atlas layout (16x16 grid, 128x256 pixels at 8x16 per cell):

     Col 0   Col 1   Col 2   Col 3  ...  Col 15
    +-------+-------+-------+-------+---+-------+
Row 0 | NUL   | SOH   | STX   | ETX   |   | DEL   |  chars 0-15
    +-------+-------+-------+-------+---+-------+
Row 1 |       |       |       |       |   |       |  chars 16-31
    +-------+-------+-------+-------+---+-------+
Row 2 | SPACE |   !   |   "   |   #   |   |   /   |  chars 32-47
    +-------+-------+-------+-------+---+-------+
Row 3 |   0   |   1   |   2   |   3   |   |   ?   |  chars 48-63
    +-------+-------+-------+-------+---+-------+
Row 4 |   @   |   A   |   B   |   C   |   |   O   |  chars 64-79
    +-------+-------+-------+-------+---+-------+
  ...                                               ...
    +-------+-------+-------+-------+---+-------+
Row 15|       |       |       |       |   |       |  chars 240-255
    +-------+-------+-------+-------+---+-------+

To find glyph for character c:
  row = c / 16
  col = c % 16
  uvX = col / 16.0
  uvY = row / 16.0
  uvW = 1.0 / 16.0
  uvH = 1.0 / 16.0
```

### Loading a Bitmap Font

```cpp
// In src/engine/renderer/bitmap_font.h

#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>

struct BitmapFont {
    GLuint textureID = 0;
    int cellWidth    = 8;    // Width of each glyph cell in pixels
    int cellHeight   = 16;   // Height of each glyph cell in pixels
    int columns      = 16;   // Grid columns (always 16 for ASCII)
    int rows         = 16;   // Grid rows
};

// Load a bitmap font atlas from an image file.
// The image must be a 16x16 grid of fixed-width glyphs.
BitmapFont loadBitmapFont(const std::string& imagePath,
                          int cellWidth, int cellHeight);

// Calculate UV coordinates for a given ASCII character.
glm::vec4 getBitmapGlyphUV(const BitmapFont& font, char c);
```

```cpp
// In src/engine/renderer/bitmap_font.cpp

#include "engine/renderer/bitmap_font.h"
#include <stb_image.h>
#include <iostream>

BitmapFont loadBitmapFont(const std::string& imagePath,
                          int cellWidth, int cellHeight) {
    BitmapFont font;
    font.cellWidth  = cellWidth;
    font.cellHeight = cellHeight;

    int width, height, channels;
    unsigned char* data = stbi_load(imagePath.c_str(),
                                    &width, &height, &channels, 4);
    if (!data) {
        std::cerr << "ERROR: Failed to load bitmap font: "
                  << imagePath << std::endl;
        return font;
    }

    glGenTextures(1, &font.textureID);
    glBindTexture(GL_TEXTURE_2D, font.textureID);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, data);

    // Nearest filtering preserves the pixel-art look
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    stbi_image_free(data);
    return font;
}

glm::vec4 getBitmapGlyphUV(const BitmapFont& font, char c) {
    unsigned char uc = static_cast<unsigned char>(c);
    int col = uc % font.columns;
    int row = uc / font.columns;

    float uvX = static_cast<float>(col) / font.columns;
    float uvY = static_cast<float>(row) / font.rows;
    float uvW = 1.0f / font.columns;
    float uvH = 1.0f / font.rows;

    return glm::vec4(uvX, uvY, uvW, uvH);
}
```

Bitmap fonts are perfect for retro-styled games, debug overlays, or any situation where you want a clean pixel look without external dependencies. But for the rest of this chapter, we will focus on the FreeType approach -- the one you will use in a shipping game.

---

## FreeType Integration

### What Is FreeType?

FreeType is an open-source font rasterisation library. You give it a `.ttf` or `.otf` file, tell it what pixel size you want, and it produces greyscale bitmaps of each glyph along with precise metrics (how wide each letter is, where the baseline sits, how far to advance the cursor). It handles the complex maths of Bezier curves, hinting, and anti-aliasing.

### Glyph Metrics

Before we write code, we need to understand how fonts describe character layout.

```
Glyph metrics for the letter 'g':

          |<-- width -->|
          |             |
  --------+-------------+-------- ascent (bearingY from baseline)
          |  ***   ***  |
          | *   * *   * |
          | *   * *   * |
          |  ***   ***  |
          |        *    |
          | *     *     |
  ========|==***=*======|======== baseline
          |             |
          | *     *     |
          |  *****      |
  --------+-------------+-------- descent (below baseline)
          |             |
  |<----->|             |
  bearingX              |
  |                     |
  |<------- advance --->|

  bearingX: horizontal offset from cursor to left edge of glyph
  bearingY: vertical offset from baseline to top edge of glyph
  advance:  horizontal distance to move cursor for the next character
  width:    pixel width of the glyph bitmap
  height:   pixel height of the glyph bitmap
```

The **baseline** is the invisible line that text sits on. Letters like 'g', 'p', and 'y' have **descenders** that drop below it. The **advance** tells you how far to move the cursor before placing the next character -- it includes the glyph width plus some spacing.

### Setup

Add FreeType to your project. If you use vcpkg: `vcpkg install freetype`. If you use CMake directly, `find_package(Freetype REQUIRED)` and link against `Freetype::Freetype`.

```cpp
// In CMakeLists.txt (add to your existing file)
find_package(Freetype REQUIRED)
target_link_libraries(qengine PRIVATE Freetype::Freetype)
```

### The GlyphInfo Struct

Each rasterised glyph needs its atlas position and metrics stored:

```cpp
// In src/engine/renderer/font.h

#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>

struct GlyphInfo {
    float uvX, uvY;         // Top-left UV in atlas
    float uvW, uvH;         // Size in UV coordinates
    int width, height;      // Pixel size of glyph bitmap
    int bearingX, bearingY; // Offset from cursor/baseline to glyph
    int advance;            // Horizontal advance (in 1/64 pixels)
};
```

---

## The Font Class

The `Font` class loads a TrueType font at a given pixel size, rasterises all printable ASCII characters (32 through 126) into a single texture atlas, and stores the glyph metrics for fast lookup.

### Building the Atlas

The atlas-building strategy is straightforward:

1. Initialise FreeType and load the font face
2. Set the desired pixel size
3. Loop through characters 32-126, rendering each glyph
4. Find the maximum glyph height, and calculate atlas dimensions
5. Pack glyphs left-to-right, row-by-row into a buffer
6. Upload the buffer as a single-channel (`GL_RED`) texture

```
Atlas packing (row-by-row, variable width per glyph):

+---+----+---+----+---+---+----+----+---+---+---+------+
| ! |  " | # | $  | % | & |  ' | (  | ) | * | + | ...  |
+---+----+---+----+---+---+----+----+---+---+---+------+
| A | B  | C | D  | E | F | G  | H  | I | J | K | ...  |
+---+----+---+----+---+---+----+----+---+---+---+------+
| a | b  | c | d  | e | f | g  | h  | i | j | k | ...  |
+---+----+---+----+---+---+----+----+---+---+---+------+
| ...                                                   |
+-------------------------------------------------------+

Each glyph occupies only its actual width (proportional).
Rows are spaced by the tallest glyph in the font.
```

### Complete Font Implementation

```cpp
// In src/engine/renderer/font.h

#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>

struct GlyphInfo {
    float uvX, uvY;         // Top-left UV in atlas
    float uvW, uvH;         // Size in UV coordinates
    int width, height;      // Pixel size of glyph bitmap
    int bearingX, bearingY; // Offset from cursor/baseline to glyph
    int advance;            // Horizontal advance (in 1/64 pixels)
};

class Font {
public:
    Font() = default;
    ~Font();

    // Non-copyable (owns GPU texture)
    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;

    // Movable
    Font(Font&& other) noexcept;
    Font& operator=(Font&& other) noexcept;

    // Load a TrueType/OpenType font at the given pixel size.
    // Returns false on failure.
    bool load(const std::string& fontPath, int pixelSize);

    // Access the atlas texture for binding
    GLuint getAtlasTexture() const { return m_atlasTexture; }

    // Look up metrics for a character (returns a default for unknown chars)
    const GlyphInfo& getGlyph(char c) const;

    // Vertical distance between lines (in pixels)
    int getLineHeight() const { return m_lineHeight; }

    // Measure the pixel width of a string without rendering it.
    // Useful for centering and wrapping calculations.
    float measureWidth(const std::string& text) const;

    bool isLoaded() const { return m_atlasTexture != 0; }

private:
    GLuint m_atlasTexture = 0;
    std::unordered_map<char, GlyphInfo> m_glyphs;
    int m_lineHeight = 0;

    GlyphInfo m_fallback{};  // Returned for characters not in the atlas

    void cleanup();
};
```

```cpp
// In src/engine/renderer/font.cpp

#include "engine/renderer/font.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <iostream>
#include <vector>
#include <algorithm>

// ─── Lifecycle ──────────────────────────────────────────────────

Font::~Font() {
    cleanup();
}

Font::Font(Font&& other) noexcept
    : m_atlasTexture(other.m_atlasTexture),
      m_glyphs(std::move(other.m_glyphs)),
      m_lineHeight(other.m_lineHeight),
      m_fallback(other.m_fallback)
{
    other.m_atlasTexture = 0;
}

Font& Font::operator=(Font&& other) noexcept {
    if (this != &other) {
        cleanup();
        m_atlasTexture = other.m_atlasTexture;
        m_glyphs       = std::move(other.m_glyphs);
        m_lineHeight   = other.m_lineHeight;
        m_fallback     = other.m_fallback;
        other.m_atlasTexture = 0;
    }
    return *this;
}

void Font::cleanup() {
    if (m_atlasTexture) {
        glDeleteTextures(1, &m_atlasTexture);
        m_atlasTexture = 0;
    }
    m_glyphs.clear();
}

// ─── Loading ────────────────────────────────────────────────────

bool Font::load(const std::string& fontPath, int pixelSize) {
    cleanup();

    // --- Initialise FreeType ---
    FT_Library ft;
    if (FT_Init_FreeType(&ft)) {
        std::cerr << "ERROR: Could not initialise FreeType" << std::endl;
        return false;
    }

    FT_Face face;
    if (FT_New_Face(ft, fontPath.c_str(), 0, &face)) {
        std::cerr << "ERROR: Could not load font: " << fontPath << std::endl;
        FT_Done_FreeType(ft);
        return false;
    }

    // Set the pixel size. Width 0 means "derive from height".
    FT_Set_Pixel_Sizes(face, 0, pixelSize);

    // --- First pass: determine atlas dimensions ---
    // We render all printable ASCII characters (32 through 126).
    const char FIRST_CHAR = 32;   // space
    const char LAST_CHAR  = 126;  // tilde ~
    const int  CHAR_COUNT = LAST_CHAR - FIRST_CHAR + 1;

    int maxGlyphHeight = 0;
    int totalWidth     = 0;

    // Temporary storage for glyph bitmaps
    struct TempGlyph {
        std::vector<unsigned char> bitmap;
        int width, height;
        int bearingX, bearingY;
        int advance;
    };
    std::vector<TempGlyph> tempGlyphs(CHAR_COUNT);

    for (int i = 0; i < CHAR_COUNT; i++) {
        char c = FIRST_CHAR + i;

        if (FT_Load_Char(face, c, FT_LOAD_RENDER)) {
            std::cerr << "WARNING: Failed to load glyph: '" << c << "'" << std::endl;
            continue;
        }

        FT_GlyphSlot g = face->glyph;

        TempGlyph& tg = tempGlyphs[i];
        tg.width    = g->bitmap.width;
        tg.height   = g->bitmap.rows;
        tg.bearingX = g->bitmap_left;
        tg.bearingY = g->bitmap_top;
        tg.advance  = static_cast<int>(g->advance.x);  // In 1/64 pixels

        // Copy the bitmap data (FreeType reuses the buffer)
        int size = tg.width * tg.height;
        tg.bitmap.resize(size);
        if (size > 0) {
            std::memcpy(tg.bitmap.data(), g->bitmap.buffer, size);
        }

        totalWidth += tg.width;
        maxGlyphHeight = std::max(maxGlyphHeight, tg.height);
    }

    m_lineHeight = pixelSize;  // A good default; could also use face->size->metrics

    // --- Calculate atlas size ---
    // Use a roughly square atlas. Pick a width, then compute rows needed.
    int atlasWidth = 1;
    while (atlasWidth < totalWidth) atlasWidth *= 2;
    // Clamp to something reasonable (e.g. 1024 is plenty for a single font size)
    atlasWidth = std::min(atlasWidth, 1024);

    // Figure out how many rows we need at this width
    int rowHeight = maxGlyphHeight + 2;  // 2px padding between rows
    int cursorX = 0;
    int cursorY = 0;
    int atlasHeight = rowHeight;

    for (int i = 0; i < CHAR_COUNT; i++) {
        const TempGlyph& tg = tempGlyphs[i];
        if (cursorX + tg.width + 1 > atlasWidth) {
            cursorX = 0;
            cursorY += rowHeight;
            atlasHeight = cursorY + rowHeight;
        }
        cursorX += tg.width + 1;  // 1px padding between glyphs
    }

    // Round atlas height up to next power of two
    int finalHeight = 1;
    while (finalHeight < atlasHeight) finalHeight *= 2;

    // --- Second pass: pack glyphs into the atlas buffer ---
    std::vector<unsigned char> atlasData(atlasWidth * finalHeight, 0);

    cursorX = 0;
    cursorY = 0;

    for (int i = 0; i < CHAR_COUNT; i++) {
        const TempGlyph& tg = tempGlyphs[i];
        char c = FIRST_CHAR + i;

        // Wrap to next row if needed
        if (cursorX + tg.width + 1 > atlasWidth) {
            cursorX = 0;
            cursorY += rowHeight;
        }

        // Copy glyph bitmap into atlas
        for (int row = 0; row < tg.height; row++) {
            for (int col = 0; col < tg.width; col++) {
                int atlasIndex = (cursorY + row) * atlasWidth + (cursorX + col);
                int glyphIndex = row * tg.width + col;
                atlasData[atlasIndex] = tg.bitmap[glyphIndex];
            }
        }

        // Record glyph info with UV coordinates
        GlyphInfo info;
        info.uvX      = static_cast<float>(cursorX) / atlasWidth;
        info.uvY      = static_cast<float>(cursorY) / finalHeight;
        info.uvW      = static_cast<float>(tg.width) / atlasWidth;
        info.uvH      = static_cast<float>(tg.height) / finalHeight;
        info.width    = tg.width;
        info.height   = tg.height;
        info.bearingX = tg.bearingX;
        info.bearingY = tg.bearingY;
        info.advance  = tg.advance;

        m_glyphs[c] = info;

        cursorX += tg.width + 1;
    }

    // --- Upload atlas to GPU ---
    glGenTextures(1, &m_atlasTexture);
    glBindTexture(GL_TEXTURE_2D, m_atlasTexture);

    // Single channel: GL_RED. The shader reads this as the alpha.
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);  // Glyph rows are not 4-byte aligned
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED,
                 atlasWidth, finalHeight, 0,
                 GL_RED, GL_UNSIGNED_BYTE,
                 atlasData.data());

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Restore default alignment
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    // --- Cleanup FreeType ---
    FT_Done_Face(face);
    FT_Done_FreeType(ft);

    std::cout << "Loaded font: " << fontPath
              << " (" << pixelSize << "px, atlas "
              << atlasWidth << "x" << finalHeight << ")" << std::endl;

    return true;
}

// ─── Glyph Lookup ───────────────────────────────────────────────

const GlyphInfo& Font::getGlyph(char c) const {
    auto it = m_glyphs.find(c);
    if (it != m_glyphs.end()) {
        return it->second;
    }
    return m_fallback;  // Zero-sized glyph for unknown characters
}

// ─── Text Measurement ───────────────────────────────────────────

float Font::measureWidth(const std::string& text) const {
    float width = 0.0f;
    for (char c : text) {
        const GlyphInfo& g = getGlyph(c);
        width += static_cast<float>(g.advance) / 64.0f;
    }
    return width;
}
```

### Why `GL_RED`?

FreeType produces greyscale bitmaps -- one byte per pixel, where 0 means transparent and 255 means fully opaque. We upload this as a `GL_RED` texture (single channel). In the fragment shader, we sample the red channel and use it as the alpha value, then multiply by the desired text colour. This keeps the atlas small (one byte per pixel instead of four) and lets us colour text dynamically.

### Why `GL_UNPACK_ALIGNMENT`?

By default, OpenGL expects each row of pixel data to start on a 4-byte boundary. FreeType glyph bitmaps have rows that are exactly `width` bytes long, which is not necessarily a multiple of 4. Setting `GL_UNPACK_ALIGNMENT` to 1 tells OpenGL to read rows byte-by-byte. Forgetting this produces garbled text -- one of the most common font rendering bugs.

---

## Text Shaders

The text shader is simple. The vertex shader applies an orthographic projection (screen-space coordinates). The fragment shader samples the font atlas and multiplies by a text colour uniform.

```glsl
// In assets/shaders/text.vert

#version 460 core

layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aUV;

out vec2 TexCoord;

uniform mat4 projection;

void main() {
    gl_Position = projection * vec4(aPos, 0.0, 1.0);
    TexCoord = aUV;
}
```

```glsl
// In assets/shaders/text.frag

#version 460 core

in vec2 TexCoord;

out vec4 FragColour;

uniform sampler2D fontAtlas;
uniform vec4 textColour;

void main() {
    // The atlas stores greyscale values in the red channel.
    // Use it as the alpha — the colour comes from the uniform.
    float alpha = texture(fontAtlas, TexCoord).r;

    if (alpha < 0.01) {
        discard;  // Skip fully transparent pixels
    }

    FragColour = vec4(textColour.rgb, textColour.a * alpha);
}
```

### The Rendering Pipeline

```
Text rendering pipeline:

  "Score: 42"
       |
       v
  +-----------+     For each character:
  |  Font     |     1. Look up GlyphInfo (UV, metrics)
  |  glyphs   |     2. Calculate screen quad position
  +-----------+          (cursor + bearing offsets)
       |             3. Emit 6 vertices (2 triangles)
       v                  with screen pos + atlas UVs
  +-----------+
  | TextRender|     4. Upload all vertices to VBO
  | vertex    |     5. Bind font atlas texture
  | buffer    |     6. Draw all triangles in one call
  +-----------+
       |
       v
  +-----------+     Vertex shader:
  | text.vert |       Apply orthographic projection
  +-----------+
       |
       v
  +-----------+     Fragment shader:
  | text.frag |       Sample atlas (red channel = alpha)
  +-----------+       Multiply by textColour uniform
       |
       v
  Screen pixels
```

---

## TextRenderer Class

The `TextRenderer` batches all characters from a single `renderText()` call into one draw call. It uses a dynamic VBO that is updated each frame.

### Screen-Space Coordinate System

We use an orthographic projection with the origin at the **top-left** corner of the screen. X increases to the right, Y increases downward. This matches how you naturally think about UI layout -- (0, 0) is the top-left, and (screenWidth, screenHeight) is the bottom-right.

```
Screen coordinate system (orthographic, origin top-left):

(0,0) ──────────────────── (screenWidth, 0)
  |                              |
  |   "Score: 42"                |
  |      drawn at (10, 30)       |
  |                              |
  |                              |
  |               "PAUSED"       |
  |          centred at          |
  |      (screenWidth/2, 300)    |
  |                              |
(0, screenHeight) ──── (screenWidth, screenHeight)
```

### Complete Implementation

```cpp
// In src/engine/renderer/text_renderer.h

#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>

class Font;
class Shader;

class TextRenderer {
public:
    TextRenderer() = default;
    ~TextRenderer();

    // Non-copyable
    TextRenderer(const TextRenderer&) = delete;
    TextRenderer& operator=(const TextRenderer&) = delete;

    // Initialise with screen dimensions (for orthographic projection).
    // Call once at startup, and again if the window is resized.
    void init(int screenWidth, int screenHeight);

    // Render a string at screen position (x, y) with the given scale and colour.
    // (x, y) is the top-left corner of the first character.
    void renderText(const Font& font, Shader& shader,
                    const std::string& text,
                    float x, float y,
                    float scale,
                    const glm::vec4& colour);

private:
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    glm::mat4 m_projection{1.0f};
    bool m_initialised = false;

    // Maximum vertices per renderText() call.
    // 6 vertices per character (2 triangles), 256 chars max per call.
    static constexpr int MAX_VERTICES = 256 * 6;

    struct TextVertex {
        glm::vec2 position;
        glm::vec2 uv;
    };

    void cleanup();
};
```

```cpp
// In src/engine/renderer/text_renderer.cpp

#include "engine/renderer/text_renderer.h"
#include "engine/renderer/font.h"
#include "engine/renderer/shader.h"

#include <glm/gtc/matrix_transform.hpp>
#include <vector>

// ─── Lifecycle ──────────────────────────────────────────────────

TextRenderer::~TextRenderer() {
    cleanup();
}

void TextRenderer::cleanup() {
    if (m_vao) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    if (m_vbo) { glDeleteBuffers(1, &m_vbo);      m_vbo = 0; }
}

// ─── Initialisation ─────────────────────────────────────────────

void TextRenderer::init(int screenWidth, int screenHeight) {
    cleanup();

    // Orthographic projection: origin top-left, Y increases downward.
    // glm::ortho(left, right, bottom, top, near, far)
    m_projection = glm::ortho(
        0.0f, static_cast<float>(screenWidth),   // left, right
        static_cast<float>(screenHeight), 0.0f,  // bottom, top (flipped!)
        -1.0f, 1.0f                               // near, far
    );

    // Create VAO and VBO for dynamic text quads
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

    // Allocate buffer (we will update it each frame with glBufferSubData)
    glBufferData(GL_ARRAY_BUFFER,
                 MAX_VERTICES * sizeof(TextVertex),
                 nullptr, GL_DYNAMIC_DRAW);

    // Position attribute: location 0, vec2
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                          sizeof(TextVertex),
                          (void*)offsetof(TextVertex, position));

    // UV attribute: location 1, vec2
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
                          sizeof(TextVertex),
                          (void*)offsetof(TextVertex, uv));

    glBindVertexArray(0);
    m_initialised = true;
}

// ─── Rendering ──────────────────────────────────────────────────

void TextRenderer::renderText(const Font& font, Shader& shader,
                              const std::string& text,
                              float x, float y,
                              float scale,
                              const glm::vec4& colour) {
    if (!m_initialised || !font.isLoaded() || text.empty()) return;

    // Build vertex data for all characters
    std::vector<TextVertex> vertices;
    vertices.reserve(text.size() * 6);

    float cursorX = x;
    float cursorY = y;

    for (char c : text) {
        const GlyphInfo& g = font.getGlyph(c);

        if (g.width == 0 || g.height == 0) {
            // Space or unknown character -- just advance cursor
            cursorX += (g.advance / 64.0f) * scale;
            continue;
        }

        // Calculate quad position using glyph metrics.
        // bearingX: horizontal offset from cursor to left edge of glyph
        // bearingY: offset from baseline to top of glyph (positive = above baseline)
        //
        // Since our Y axis points downward (top-left origin), we compute:
        //   top of glyph    = cursorY + (lineHeight - bearingY) * scale
        //   bottom of glyph = top + height * scale

        float xPos = cursorX + g.bearingX * scale;
        float yPos = cursorY + (font.getLineHeight() - g.bearingY) * scale;
        float w    = g.width * scale;
        float h    = g.height * scale;

        // UV coordinates in the atlas
        float u0 = g.uvX;
        float v0 = g.uvY;
        float u1 = g.uvX + g.uvW;
        float v1 = g.uvY + g.uvH;

        // Two triangles per character (6 vertices)
        //
        //  (xPos, yPos)-----(xPos+w, yPos)
        //       |   \            |
        //       |    \           |
        //       |     \          |
        //  (xPos, yPos+h)--(xPos+w, yPos+h)

        // Triangle 1
        vertices.push_back({{ xPos,     yPos     }, { u0, v0 }});
        vertices.push_back({{ xPos,     yPos + h }, { u0, v1 }});
        vertices.push_back({{ xPos + w, yPos + h }, { u1, v1 }});

        // Triangle 2
        vertices.push_back({{ xPos,     yPos     }, { u0, v0 }});
        vertices.push_back({{ xPos + w, yPos + h }, { u1, v1 }});
        vertices.push_back({{ xPos + w, yPos     }, { u1, v0 }});

        // Advance cursor to the next character position
        cursorX += (g.advance / 64.0f) * scale;
    }

    if (vertices.empty()) return;

    // --- Upload and draw ---
    shader.use();
    shader.setMat4("projection", m_projection);
    shader.setVec4("textColour", colour);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, font.getAtlasTexture());
    shader.setInt("fontAtlas", 0);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    vertices.size() * sizeof(TextVertex),
                    vertices.data());

    // Enable blending for anti-aliased text edges
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    glDrawArrays(GL_TRIANGLES, 0, static_cast<int>(vertices.size()));

    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glBindVertexArray(0);
}
```

### Why a Dynamic VBO?

Text changes every frame -- health values, score counters, console output, debug info. Rather than creating a new VBO each frame (slow) or maintaining one per text string (complex), we allocate a single buffer large enough for the longest string we expect, then overwrite it each call with `glBufferSubData`. This is the standard approach for dynamic UI rendering.

---

## Text Alignment and Wrapping

These are free functions -- pure utilities that compute positions without touching any state. They take a `Font` reference for measurement but produce no side effects. This keeps them ECS-compliant (no hidden state).

```cpp
// In src/engine/renderer/text_utils.h

#pragma once

#include <string>
#include <vector>

class Font;

enum class TextAlign {
    Left,
    Centre,
    Right
};

// Calculate the X position for aligned text within a given region.
// regionX is the left edge of the region, regionWidth is its width.
float alignedTextX(const Font& font, const std::string& text,
                   float scale, TextAlign align,
                   float regionX, float regionWidth);

// Break text into lines that fit within maxWidth pixels.
// Breaks on spaces (word wrapping). Returns a vector of lines.
std::vector<std::string> wrapText(const Font& font,
                                  const std::string& text,
                                  float scale,
                                  float maxWidth);
```

```cpp
// In src/engine/renderer/text_utils.cpp

#include "engine/renderer/text_utils.h"
#include "engine/renderer/font.h"

float alignedTextX(const Font& font, const std::string& text,
                   float scale, TextAlign align,
                   float regionX, float regionWidth) {
    switch (align) {
        case TextAlign::Left:
            return regionX;

        case TextAlign::Centre: {
            float textWidth = font.measureWidth(text) * scale;
            return regionX + (regionWidth - textWidth) * 0.5f;
        }

        case TextAlign::Right: {
            float textWidth = font.measureWidth(text) * scale;
            return regionX + regionWidth - textWidth;
        }
    }
    return regionX;  // Fallback
}

std::vector<std::string> wrapText(const Font& font,
                                  const std::string& text,
                                  float scale,
                                  float maxWidth) {
    std::vector<std::string> lines;
    std::string currentLine;
    std::string currentWord;

    for (size_t i = 0; i <= text.size(); i++) {
        char c = (i < text.size()) ? text[i] : ' ';  // Flush last word

        if (c == ' ' || c == '\n') {
            // Check if adding this word would exceed the width
            std::string testLine = currentLine.empty()
                ? currentWord
                : currentLine + " " + currentWord;

            float testWidth = font.measureWidth(testLine) * scale;

            if (testWidth > maxWidth && !currentLine.empty()) {
                // Word doesn't fit -- push current line, start new one
                lines.push_back(currentLine);
                currentLine = currentWord;
            } else {
                currentLine = testLine;
            }

            currentWord.clear();

            if (c == '\n') {
                lines.push_back(currentLine);
                currentLine.clear();
            }
        } else {
            currentWord += c;
        }
    }

    if (!currentLine.empty()) {
        lines.push_back(currentLine);
    }

    return lines;
}
```

### Usage Example

```cpp
// Centre a title on screen
float titleX = alignedTextX(menuFont, "PAUSED", 2.0f,
                            TextAlign::Centre, 0.0f, screenWidth);
textRenderer.renderText(menuFont, textShader, "PAUSED",
                        titleX, 100.0f, 2.0f,
                        glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));

// Word-wrap a description box
auto lines = wrapText(bodyFont, itemDescription, 1.0f, 300.0f);
float lineY = 200.0f;
for (const auto& line : lines) {
    textRenderer.renderText(bodyFont, textShader, line,
                            50.0f, lineY, 1.0f,
                            glm::vec4(0.9f, 0.9f, 0.9f, 1.0f));
    lineY += bodyFont.getLineHeight();
}
```

---

## Integration with Existing Systems

The `TextRenderer` replaces the ad-hoc text drawing we used in earlier chapters. Here is how to connect it to the dev console (Chapter 27), the HUD (Chapter 15), and the menu system (Chapter 22).

### Shared Resources

The `Font` and `TextRenderer` are shared resources -- not ECS entities. They sit alongside the `Window`, `AudioManager`, and `Shader` cache:

```cpp
// In main.cpp or your game context struct

Font hudFont;
Font consoleFont;
Font menuFont;

hudFont.load("assets/fonts/roboto_mono.ttf", 18);
consoleFont.load("assets/fonts/roboto_mono.ttf", 14);
menuFont.load("assets/fonts/roboto_bold.ttf", 32);

TextRenderer textRenderer;
textRenderer.init(screenWidth, screenHeight);

Shader textShader;
textShader.load("assets/shaders/text.vert", "assets/shaders/text.frag");
```

### Updating the HUD (Chapter 15)

Replace the old bitmap font calls with the new `TextRenderer`:

```cpp
// In src/game/systems/hud_system.cpp

void hudSystem(entt::registry& registry,
               const Font& font, TextRenderer& textRenderer,
               Shader& textShader, int screenWidth) {

    auto view = registry.view<TagPlayer, Health, Ammo>();
    for (auto [entity, tag, health, ammo] : view.each()) {

        // Health (bottom-left)
        std::string healthStr = "HEALTH: " + std::to_string(static_cast<int>(health.current));
        glm::vec4 healthColour = (health.current < 25.0f)
            ? glm::vec4(1.0f, 0.2f, 0.2f, 1.0f)   // Red when low
            : glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);   // White
        textRenderer.renderText(font, textShader, healthStr,
                                20.0f, 550.0f, 1.0f, healthColour);

        // Ammo (bottom-right, right-aligned)
        std::string ammoStr = "AMMO: " + std::to_string(ammo.current);
        float ammoX = alignedTextX(font, ammoStr, 1.0f,
                                   TextAlign::Right, 0.0f,
                                   static_cast<float>(screenWidth) - 20.0f);
        textRenderer.renderText(font, textShader, ammoStr,
                                ammoX, 550.0f, 1.0f,
                                glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
    }
}
```

### Updating the Console (Chapter 27)

The console rendering from Chapter 27 had placeholder comments for font calls. Now we fill them in:

```cpp
// In src/engine/debug/console_renderer.cpp

void renderConsole(const Console& console,
                   const Font& font, TextRenderer& textRenderer,
                   Shader& textShader,
                   int screenWidth, int screenHeight) {
    if (!console.isOpen()) return;

    float consoleHeight = screenHeight * 0.4f;
    float lineHeight    = static_cast<float>(font.getLineHeight());

    // Render output lines (scrolling up from the input line)
    const auto& output = console.getOutput();
    int maxVisible = static_cast<int>((consoleHeight - 40.0f) / lineHeight);
    int start = std::max(0, static_cast<int>(output.size()) - maxVisible);

    float y = 10.0f;
    for (int i = start; i < static_cast<int>(output.size()); i++) {
        textRenderer.renderText(font, textShader, output[i],
                                10.0f, y, 1.0f,
                                glm::vec4(0.8f, 0.8f, 0.8f, 1.0f));
        y += lineHeight;
    }

    // Render input line at the bottom of the console area
    float inputY = consoleHeight - lineHeight - 5.0f;
    std::string inputLine = "] " + console.getInput();
    textRenderer.renderText(font, textShader, inputLine,
                            10.0f, inputY, 1.0f,
                            glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
}
```

### Updating Menus (Chapter 22)

Menu items use the larger font with centre alignment:

```cpp
// In src/game/states/menu_state.cpp (render method)

void MenuState::render() {
    // ... draw menu background ...

    float centerX = static_cast<float>(m_screenWidth) / 2.0f;
    float y = 200.0f;

    for (int i = 0; i < static_cast<int>(m_items.size()); i++) {
        glm::vec4 colour = (i == m_selectedIndex)
            ? glm::vec4(1.0f, 0.9f, 0.2f, 1.0f)   // Yellow highlight
            : glm::vec4(0.7f, 0.7f, 0.7f, 1.0f);   // Grey

        float scale = (i == m_selectedIndex) ? 1.2f : 1.0f;

        float itemX = alignedTextX(m_menuFont, m_items[i], scale,
                                   TextAlign::Centre,
                                   0.0f, static_cast<float>(m_screenWidth));

        m_textRenderer.renderText(m_menuFont, m_textShader,
                                  m_items[i],
                                  itemX, y, scale, colour);

        y += m_menuFont.getLineHeight() * 1.5f;
    }
}
```

---

## C++ Concept: `std::unordered_map` vs `std::map`

We used `std::unordered_map<char, GlyphInfo>` to store glyph data. Why not `std::map`? The answer comes down to how they store and retrieve data.

### `std::unordered_map` -- Hash Table

```cpp
#include <unordered_map>

std::unordered_map<char, GlyphInfo> glyphs;
glyphs['A'] = myGlyphA;

// Lookup: O(1) average, O(n) worst case
const GlyphInfo& g = glyphs['A'];
```

Internally, `unordered_map` hashes the key (`char` in our case) to find a bucket, then searches that bucket (usually just one element). Average lookup is **O(1)** -- constant time regardless of how many entries exist. This is ideal for glyph lookup because we look up glyphs thousands of times per frame (once per character rendered).

### `std::map` -- Red-Black Tree

```cpp
#include <map>

std::map<char, GlyphInfo> glyphs;
glyphs['A'] = myGlyphA;

// Lookup: O(log n) always
const GlyphInfo& g = glyphs['A'];
```

`std::map` stores entries in a balanced binary tree (red-black tree). Lookup is **O(log n)** -- it traverses the tree. The entries are always sorted by key, which is useful if you need to iterate in order. For glyph lookup, we never need sorted order, so this is wasted overhead.

### When to Use Which

| Criterion | `std::unordered_map` | `std::map` |
|-----------|---------------------|------------|
| Lookup speed | O(1) average | O(log n) |
| Insertion speed | O(1) average | O(log n) |
| Memory overhead | Higher (hash table + buckets) | Lower (tree nodes) |
| Iteration order | Arbitrary | Sorted by key |
| Requires hashable key | Yes | No (requires `<` operator) |
| Best for | Frequent lookups, order irrelevant | Need sorted iteration |

For our font system, `unordered_map` is the clear winner. We look up glyphs by character value on every frame, and we never need them in sorted order.

### Custom Hash Functions

For `char`, `int`, `std::string`, and other standard types, the standard library provides built-in hash functions. If you ever use a custom type as a key, you need to provide your own:

```cpp
struct Vec2i {
    int x, y;

    bool operator==(const Vec2i& other) const {
        return x == other.x && y == other.y;
    }
};

// Custom hash function for Vec2i
struct Vec2iHash {
    std::size_t operator()(const Vec2i& v) const {
        // Combine hashes of individual fields
        std::size_t h1 = std::hash<int>{}(v.x);
        std::size_t h2 = std::hash<int>{}(v.y);
        return h1 ^ (h2 << 1);  // Simple but effective combine
    }
};

// Use it as the third template parameter
std::unordered_map<Vec2i, TileData, Vec2iHash> tileMap;
```

For `char`, no custom hash is needed -- `std::hash<char>` works out of the box. The glyph map just works.

---

## Summary

What we built this chapter:

```
src/engine/renderer/bitmap_font.h      -- BitmapFont struct and UV calculation
src/engine/renderer/bitmap_font.cpp    -- load bitmap atlas, compute glyph UVs
src/engine/renderer/font.h             -- GlyphInfo struct and Font class
src/engine/renderer/font.cpp           -- FreeType loading, atlas packing, measurement
src/engine/renderer/text_renderer.h    -- TextRenderer class (batched drawing)
src/engine/renderer/text_renderer.cpp  -- dynamic VBO, orthographic projection, draw
src/engine/renderer/text_utils.h       -- alignment and word-wrap free functions
src/engine/renderer/text_utils.cpp     -- left/centre/right alignment, word wrapping
assets/shaders/text.vert               -- orthographic projection, pass-through UVs
assets/shaders/text.frag               -- sample atlas red channel as alpha
```

The font system is a shared resource, not an ECS entity. Components like `Health` and `Ammo` store data; the HUD system reads that data and calls `TextRenderer` to display it. The `Font` and `TextRenderer` own GPU resources and live alongside the window and shader cache. Components have no behaviour. Systems have no state.

---

## What's Next

In **Chapter 31**, we will add decals -- bullet holes, scorch marks, and blood splatters that stick to world geometry. Decals project a texture onto nearby surfaces, adding visual feedback that makes combat feel impactful without modifying the underlying level mesh.
