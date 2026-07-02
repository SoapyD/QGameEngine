# Chapter 54: Anti-Aliasing

## What You'll Learn
- What aliasing is and why it happens -- sampling theory, staircase artefacts, specular flickering
- MSAA (Multisample Anti-Aliasing) -- hardware-based edge smoothing and why it conflicts with deferred rendering
- FXAA (Fast Approximate Anti-Aliasing) -- a post-process shader that detects and smooths edges via luminance contrast
- TAA (Temporal Anti-Aliasing) -- the modern standard: jittered projection, history buffers, motion vectors, neighbourhood clamping
- How to compute per-pixel motion vectors from current and previous frame matrices
- A comparison of all three techniques: quality, performance, complexity, and when to use each
- Integrating AA as a configurable pass in `RenderPipeline` via `ConfigManager`
- C++ concept: Halton sequences -- low-discrepancy sequences for uniform sampling, and why they beat random jitter

---

## The Problem: Jagged Edges

Look carefully at any diagonal or curved edge in the engine right now. The silhouette of a column, the edge of a crate, the roofline of a building against the skybox. You will see staircase patterns -- blocky, pixel-sized steps where the edge should be smooth. These are **aliasing artefacts**, and they are one of the most visible quality problems in any renderer.

```
ALIASING: WHY EDGES LOOK JAGGED

  The real edge is a continuous diagonal line.
  The screen is a discrete grid of square pixels.
  Each pixel is either "inside" or "outside" the triangle.

  Continuous edge:              Rasterised result:

        \                        . . X X X X
         \                       . . . X X X
          \                      . . . . X X
           \                     . . . . . X
            \                    . . . . . .

  The smooth diagonal becomes a blocky staircase.
  Every step is exactly one pixel wide and one pixel tall.
  This is spatial aliasing -- insufficient sampling of a continuous signal.
```

### Sampling Theory in One Paragraph

A pixel is a single point sample of the continuous image. If the image contains detail finer than the pixel grid (a thin edge crossing the middle of a pixel, for example), that detail either gets captured or missed depending on whether the sample point happens to fall on one side or the other. This is the Nyquist problem: to faithfully capture a signal, you need at least two samples per cycle. Triangle edges, by definition, create infinitely sharp transitions -- one side is the triangle surface, the other side is whatever is behind it. No finite pixel grid can represent that perfectly.

### Specular Aliasing and Temporal Flickering

Spatial aliasing (staircase edges) is the most obvious symptom, but aliasing also causes **temporal flickering**. Specular highlights on rough surfaces can be smaller than a pixel. As the camera moves by sub-pixel amounts, these highlights appear and disappear from frame to frame, causing a shimmering effect. This is especially noticeable on metallic surfaces with high roughness, and it gets worse at higher resolutions because the highlights are sharper.

```
TEMPORAL ALIASING (SPECULAR SHIMMER)

  Frame 1: highlight lands ON pixel center     Frame 2: camera shifts 0.3 pixels
                                                        highlight BETWEEN pixels

    . . . . . .                                 . . . . . .
    . . * . . .   <-- bright specular dot       . . . . . .   <-- gone!
    . . . . . .                                 . . . . . .

  Result: the highlight flickers on and off every few frames.
  The viewer perceives shimmering / sparkling on distant surfaces.
```

Anti-aliasing techniques aim to smooth these artefacts. There are three major approaches we will cover, from simplest to most capable.

---

## Technique 1: MSAA (Multisample Anti-Aliasing)

MSAA is the oldest and most widely understood technique. Instead of taking one sample per pixel, the rasteriser evaluates triangle coverage at multiple sub-pixel positions (typically 2, 4, or 8). If a triangle edge passes through a pixel, some sub-samples will be inside the triangle and some will be outside. The final pixel colour is the average of the covered samples.

```
MSAA 4x: FOUR SUB-SAMPLES PER PIXEL

  Single pixel with 4 sample points:

    +---+---+
    | 1 | 2 |      If triangle covers samples 1, 2, 3 but not 4:
    +---+---+        Final colour = (3 x triangle_colour + 1 x background) / 4
    | 3 | 4 |        Edge pixel gets 75% triangle, 25% background
    +---+---+        -- a smooth blend instead of hard on/off

  The fragment shader runs ONCE per pixel (at the pixel center).
  Only coverage is evaluated at each sub-sample, not shading.
  This is what makes MSAA cheaper than supersampling.
```

### Enabling MSAA

MSAA requires a multisampled framebuffer. For the default framebuffer, request it from GLFW before window creation:

```cpp
// Before glfwCreateWindow()
glfwWindowHint(GLFW_SAMPLES, 4);  // Request 4x MSAA

// After context creation
glEnable(GL_MULTISAMPLE);
```

For rendering to an FBO (which is what QEngine does for post-processing), you need multisampled textures:

```cpp
// Creating a multisampled colour attachment
GLuint msTexture;
glGenTextures(1, &msTexture);
glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, msTexture);
glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, 4, GL_RGBA16F,
                        width, height, GL_TRUE);
glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                       GL_TEXTURE_2D_MULTISAMPLE, msTexture, 0);

// Multisampled depth/stencil
GLuint msDepth;
glGenRenderbuffers(1, &msDepth);
glBindRenderbuffer(GL_RENDERBUFFER, msDepth);
glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_DEPTH24_STENCIL8,
                                width, height);
glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                          GL_RENDERBUFFER, msDepth);
```

To resolve the multisampled buffer into a regular texture for post-processing:

```cpp
// Blit (resolve) from multisampled FBO to regular FBO
glBindFramebuffer(GL_READ_FRAMEBUFFER, msFBO);
glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolvedFBO);
glBlitFramebuffer(0, 0, width, height,
                  0, 0, width, height,
                  GL_COLOR_BUFFER_BIT, GL_NEAREST);
```

### Why MSAA Doesn't Work with Deferred Rendering

Here is the problem. Our G-buffer from Chapter 52 stores per-pixel surface data: position, normal, albedo, metallic/roughness. MSAA needs per-sample data to work -- it needs to know which sub-samples belong to which triangle. To make MSAA work with deferred rendering, every G-buffer texture would need to be multisampled:

```
MSAA + DEFERRED: THE MEMORY AND BANDWIDTH PROBLEM

  Regular G-buffer (1080p):
    Position:  1920 x 1080 x 8 bytes  =  16 MB
    Normal:    1920 x 1080 x 8 bytes  =  16 MB
    Albedo:    1920 x 1080 x 4 bytes  =   8 MB
    Material:  1920 x 1080 x 4 bytes  =   8 MB
    Depth:     1920 x 1080 x 4 bytes  =   8 MB
                                        ------
                                         56 MB

  4x MSAA G-buffer:
    Every texture needs 4 sub-samples per pixel.
    56 MB x 4 = 224 MB   (just for the G-buffer!)

  The lighting pass must also evaluate PER-SAMPLE, not per-pixel,
  quadrupling the lighting cost and defeating the purpose of deferred.
```

MSAA with deferred rendering is technically possible, but the memory cost and the requirement to shade per-sample in the lighting pass make it impractical. This is one of the trade-offs we noted in Chapter 52.

**When to use MSAA:** In QEngine, the forward rendering pass for transparent objects (particles, glass) can benefit from MSAA because it bypasses the G-buffer entirely. If you have a fully forward renderer, MSAA is the straightforward choice.

For the deferred path, we need screen-space solutions: FXAA and TAA.

---

## Technique 2: FXAA (Fast Approximate Anti-Aliasing)

FXAA is a pure post-processing effect. It takes the final rendered image, detects edges by looking at luminance contrast between neighbouring pixels, and blurs along those edges. It knows nothing about geometry, triangles, or depth -- it operates on the 2D colour image alone.

```
FXAA PIPELINE

  Rendered Scene         FXAA Pass               Output
  (jagged edges)        (post-process)          (smoothed edges)
  +-----------+         +-----------+           +-----------+
  |           |  --->   | Detect    |  --->     |           |
  | Aliased   |         | edges via |           | Smoothed  |
  | image     |         | luminance |           | image     |
  +-----------+         | contrast, |           +-----------+
                        | blur along|
                        | edge dir  |
                        +-----------+
```

### How FXAA Works

The algorithm has four steps for each pixel:

1. **Compute local contrast.** Read the luminance of the current pixel and its four immediate neighbours (N, S, E, W). If the difference between the brightest and darkest exceeds a threshold, this pixel is on or near an edge.

2. **Determine edge direction.** Compare horizontal vs vertical luminance gradients to decide if the edge runs horizontally or vertically.

3. **Search along the edge.** Step along the edge direction in both directions to find where the edge ends. This determines how far the blend should extend.

4. **Blend.** Shift the texture coordinate slightly perpendicular to the edge and sample. The result blends the edge pixel with its neighbour, smoothing the staircase.

```
FXAA EDGE DETECTION (LUMINANCE)

  Luminance values for a 3x3 neighbourhood:

    0.2   0.2   0.8        N=0.2
    0.2  [0.5]  0.8      W=0.2  C=0.5  E=0.8
    0.2   0.2   0.8        S=0.2

  Contrast = max(N,S,E,W,C) - min(N,S,E,W,C) = 0.8 - 0.2 = 0.6
  Threshold = 0.0625 (adjustable)
  0.6 > 0.0625 --> this pixel is on an EDGE

  Horizontal gradient: |N + S - 2*C| = |0.2 + 0.2 - 1.0| = 0.6
  Vertical gradient:   |E + W - 2*C| = |0.8 + 0.2 - 1.0| = 0.0

  Horizontal gradient > vertical --> edge is VERTICAL
  (The brightness change happens going left-to-right)
```

### The FXAA Shader

This is a simplified but functional implementation based on FXAA 3.11 quality. It handles edge detection, direction determination, endpoint search, and sub-pixel blending.

```glsl
// assets/shaders/fxaa.vert
#version 460 core

layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoords;

out vec2 TexCoords;

void main() {
    TexCoords   = aTexCoords;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
```

```glsl
// assets/shaders/fxaa.frag
#version 460 core

out vec4 FragColor;
in vec2 TexCoords;

uniform sampler2D screenTexture;
uniform vec2      inverseScreenSize;  // 1.0 / vec2(width, height)

// ── Tuning constants ─────────────────────────────────────────────
// Minimum contrast to trigger AA.  Lower = more edges detected.
const float EDGE_THRESHOLD_MIN = 0.0312;
const float EDGE_THRESHOLD_MAX = 0.125;

// Sub-pixel blending.  Higher = more blur on low-contrast edges.
const float SUBPIX_QUALITY = 0.75;

// Maximum search steps along the edge in each direction.
const int   SEARCH_STEPS = 12;

// Step quality (larger steps = faster but less accurate search).
// First few steps are 1 texel, then we accelerate.
const float QUALITY[12] = float[12](
    1.0, 1.0, 1.0, 1.0, 1.0,
    1.5, 2.0, 2.0, 2.0, 2.0, 4.0, 8.0
);

// ── Luminance ────────────────────────────────────────────────────
float luminance(vec3 colour) {
    return dot(colour, vec3(0.299, 0.587, 0.114));
}

void main() {
    vec2 uv = TexCoords;

    // ── Step 1: Sample neighbourhood luminance ───────────────────
    vec3 rgbC = texture(screenTexture, uv).rgb;
    float lumaC = luminance(rgbC);

    float lumaN = luminance(textureOffset(screenTexture, uv, ivec2( 0,  1)).rgb);
    float lumaS = luminance(textureOffset(screenTexture, uv, ivec2( 0, -1)).rgb);
    float lumaE = luminance(textureOffset(screenTexture, uv, ivec2( 1,  0)).rgb);
    float lumaW = luminance(textureOffset(screenTexture, uv, ivec2(-1,  0)).rgb);

    float lumaMax = max(lumaC, max(max(lumaN, lumaS), max(lumaE, lumaW)));
    float lumaMin = min(lumaC, min(min(lumaN, lumaS), min(lumaE, lumaW)));
    float lumaRange = lumaMax - lumaMin;

    // ── Early exit: no edge here ─────────────────────────────────
    if (lumaRange < max(EDGE_THRESHOLD_MIN, lumaMax * EDGE_THRESHOLD_MAX)) {
        FragColor = vec4(rgbC, 1.0);
        return;
    }

    // ── Step 2: Sample diagonal neighbours ───────────────────────
    float lumaNW = luminance(textureOffset(screenTexture, uv, ivec2(-1,  1)).rgb);
    float lumaNE = luminance(textureOffset(screenTexture, uv, ivec2( 1,  1)).rgb);
    float lumaSW = luminance(textureOffset(screenTexture, uv, ivec2(-1, -1)).rgb);
    float lumaSE = luminance(textureOffset(screenTexture, uv, ivec2( 1, -1)).rgb);

    float lumaNS = lumaN + lumaS;
    float lumaEW = lumaE + lumaW;

    // ── Step 3: Determine edge direction (horizontal or vertical) ─
    float edgeHorz = abs(lumaNW + lumaNE - 2.0 * lumaN)
                   + abs(lumaW  + lumaE  - 2.0 * lumaC) * 2.0
                   + abs(lumaSW + lumaSE - 2.0 * lumaS);

    float edgeVert = abs(lumaNW + lumaSW - 2.0 * lumaW)
                   + abs(lumaN  + lumaS  - 2.0 * lumaC) * 2.0
                   + abs(lumaNE + lumaSE - 2.0 * lumaE);

    bool isHorizontal = (edgeHorz >= edgeVert);

    // ── Step 4: Choose the steepest gradient side ────────────────
    // Perpendicular to the edge direction
    float luma1 = isHorizontal ? lumaS : lumaW;
    float luma2 = isHorizontal ? lumaN : lumaE;

    float gradient1 = abs(luma1 - lumaC);
    float gradient2 = abs(luma2 - lumaC);
    bool steepest1 = gradient1 >= gradient2;

    float gradientScaled = 0.25 * max(gradient1, gradient2);

    // Step size in the perpendicular direction
    float stepLength = isHorizontal ? inverseScreenSize.y : inverseScreenSize.x;

    float lumaLocalAvg;
    if (steepest1) {
        stepLength = -stepLength;
        lumaLocalAvg = 0.5 * (luma1 + lumaC);
    } else {
        lumaLocalAvg = 0.5 * (luma2 + lumaC);
    }

    // Move half a pixel perpendicular to the edge
    vec2 currentUV = uv;
    if (isHorizontal) {
        currentUV.y += stepLength * 0.5;
    } else {
        currentUV.x += stepLength * 0.5;
    }

    // ── Step 5: Search along the edge in both directions ─────────
    vec2 offset = isHorizontal
        ? vec2(inverseScreenSize.x, 0.0)
        : vec2(0.0, inverseScreenSize.y);

    vec2 uv1 = currentUV - offset;
    vec2 uv2 = currentUV + offset;

    float lumaEnd1 = luminance(texture(screenTexture, uv1).rgb) - lumaLocalAvg;
    float lumaEnd2 = luminance(texture(screenTexture, uv2).rgb) - lumaLocalAvg;

    bool reached1 = abs(lumaEnd1) >= gradientScaled;
    bool reached2 = abs(lumaEnd2) >= gradientScaled;
    bool reachedBoth = reached1 && reached2;

    if (!reached1) uv1 -= offset;
    if (!reached2) uv2 += offset;

    if (!reachedBoth) {
        for (int i = 2; i < SEARCH_STEPS; ++i) {
            if (!reached1) {
                lumaEnd1 = luminance(texture(screenTexture, uv1).rgb) - lumaLocalAvg;
            }
            if (!reached2) {
                lumaEnd2 = luminance(texture(screenTexture, uv2).rgb) - lumaLocalAvg;
            }

            reached1 = abs(lumaEnd1) >= gradientScaled;
            reached2 = abs(lumaEnd2) >= gradientScaled;
            reachedBoth = reached1 && reached2;

            if (!reached1) uv1 -= offset * QUALITY[i];
            if (!reached2) uv2 += offset * QUALITY[i];

            if (reachedBoth) break;
        }
    }

    // ── Step 6: Compute the blend factor ─────────────────────────
    float dist1 = isHorizontal ? (uv.x - uv1.x) : (uv.y - uv1.y);
    float dist2 = isHorizontal ? (uv2.x - uv.x) : (uv2.y - uv.y);

    bool isDir1 = dist1 < dist2;
    float distFinal = min(dist1, dist2);
    float edgeLength = dist1 + dist2;
    float pixelOffset = -distFinal / edgeLength + 0.5;

    // Ensure we are blending in the correct direction
    bool isLumaCSmaller = lumaC < lumaLocalAvg;
    bool correctVariation = ((isDir1 ? lumaEnd1 : lumaEnd2) < 0.0) != isLumaCSmaller;
    float finalOffset = correctVariation ? pixelOffset : 0.0;

    // ── Sub-pixel anti-aliasing ──────────────────────────────────
    // Handles single-pixel-wide lines and low-contrast details
    float lumaAvg = (1.0 / 12.0) * (2.0 * lumaNS + 2.0 * lumaEW
                  + lumaNW + lumaNE + lumaSW + lumaSE);
    float subPixOffset = clamp(abs(lumaAvg - lumaC) / lumaRange, 0.0, 1.0);
    subPixOffset = (-2.0 * subPixOffset + 3.0) * subPixOffset * subPixOffset;
    float subPixFinal = subPixOffset * subPixOffset * SUBPIX_QUALITY;

    finalOffset = max(finalOffset, subPixFinal);

    // ── Final sample ─────────────────────────────────────────────
    vec2 finalUV = uv;
    if (isHorizontal) {
        finalUV.y += finalOffset * stepLength;
    } else {
        finalUV.x += finalOffset * stepLength;
    }

    FragColor = vec4(texture(screenTexture, finalUV).rgb, 1.0);
}
```

### How the Search Works

```
FXAA EDGE SEARCH

  Suppose the edge is vertical (brightness changes left to right).
  We search UP and DOWN along the edge to find its endpoints.

  Column of pixels along the edge:

    ...
    pixel -3  luma=0.5  <-- endpoint found (contrast drops)
    pixel -2  luma=0.3
    pixel -1  luma=0.3
    pixel  0  luma=0.3  <-- current pixel (on edge)
    pixel +1  luma=0.3
    pixel +2  luma=0.5  <-- endpoint found
    ...

  Edge spans from -3 to +2 (length = 5).
  Current pixel is at offset 3 from start.
  Blend factor = 3/5 = 0.6  (closer to one end than the other)

  This determines how much to shift the sample coordinate
  perpendicular to the edge for smooth blending.
```

### FXAA Integration

FXAA slots into the post-processing pipeline as a pass after tone mapping (so it operates on LDR colours) but before the HUD overlay.

```cpp
// src/engine/renderer/aa_fxaa.h
#pragma once

#include "engine/renderer/shader.h"
#include "engine/renderer/screen_quad.h"
#include <glad/glad.h>

class FXAAPass {
public:
    FXAAPass() = default;

    bool init(int width, int height);
    void resize(int width, int height);

    // Takes the input scene texture and renders the FXAA result
    // into its own FBO.  Call getOutputTexture() to read the result.
    void execute(GLuint inputTexture, Shader& fxaaShader,
                 const ScreenQuad& quad);

    GLuint getOutputTexture() const { return m_texture; }
    GLuint getFBO()           const { return m_fbo; }

private:
    GLuint m_fbo     = 0;
    GLuint m_texture = 0;
    int    m_width   = 0;
    int    m_height  = 0;
    void create();
    void cleanup();
};
```

```cpp
// src/engine/renderer/aa_fxaa.cpp
#include "engine/renderer/aa_fxaa.h"
#include <iostream>

bool FXAAPass::init(int width, int height) {
    m_width = width; m_height = height;
    create();
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "ERROR: FXAA framebuffer is not complete!" << std::endl;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

void FXAAPass::create() {
    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                 m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, m_texture, 0);
}

void FXAAPass::resize(int width, int height) {
    if (width == m_width && height == m_height) return;
    cleanup();
    m_width = width; m_height = height;
    create();
}

void FXAAPass::cleanup() {
    if (m_texture) { glDeleteTextures(1, &m_texture); m_texture = 0; }
    if (m_fbo)     { glDeleteFramebuffers(1, &m_fbo); m_fbo = 0; }
}

void FXAAPass::execute(GLuint inputTexture, Shader& fxaaShader,
                       const ScreenQuad& quad) {
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_width, m_height);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);

    fxaaShader.use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    fxaaShader.setInt("screenTexture", 0);
    fxaaShader.setVec2("inverseScreenSize",
                       glm::vec2(1.0f / m_width, 1.0f / m_height));

    quad.draw();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
```

### FXAA Pros and Cons

**Pros:** Very cheap (< 1ms at 1080p). Works perfectly with deferred rendering. Simple to integrate -- one full-screen pass. No extra geometry data needed.

**Cons:** Blurs fine detail. Text, thin wires, and high-frequency textures lose sharpness. Cannot fix specular aliasing or sub-pixel detail because it only sees the final image. Does not accumulate information over time, so each frame is processed independently with no temporal stability.

---

## Technique 3: TAA (Temporal Anti-Aliasing)

TAA is the modern standard. It is used by virtually every AAA game shipped in the last decade. The core idea: instead of taking more samples per pixel in one frame, take one sample per pixel but at a **different sub-pixel position each frame**, then blend results over time.

```
TAA CONCEPT

  Frame 1: sample at pixel center + jitter offset A
  Frame 2: sample at pixel center + jitter offset B
  Frame 3: sample at pixel center + jitter offset C
  ...

  Each frame's jitter shifts the sampling grid slightly.
  Over N frames, the pixel accumulates N sub-pixel samples.
  The accumulated result smoothly covers the entire pixel area.

    Frame 1 sample: x         Frame 2 sample:   x
    +-------+                 +-------+
    |       |                 |       |
    |   x   |                 | x     |
    |       |                 |       |
    +-------+                 +-------+

    After blending: both positions contribute to the pixel colour.
    Edges are smoothed, sub-pixel detail is captured over time.
```

TAA has four components:

1. **Jitter** -- offset the projection matrix each frame by a sub-pixel amount
2. **History buffer** -- store last frame's result
3. **Motion vectors** -- track per-pixel movement so the history buffer can be reprojected correctly
4. **Resolve shader** -- blend current frame with history, using neighbourhood clamping to prevent ghosting

### Component 1: Projection Jitter

Each frame, we add a tiny sub-pixel offset to the projection matrix. This shifts where the rasteriser samples, effectively supersampling over time. The offsets come from a Halton sequence (explained in the C++ concept sidebar) for even coverage.

```cpp
// src/engine/renderer/taa_jitter.h
#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class TAAJitter {
public:
    TAAJitter() = default;

    // Call once per frame to advance the jitter sequence.
    // Returns the jittered projection matrix.
    glm::mat4 apply(const glm::mat4& projection, int screenWidth, int screenHeight);

    // The raw jitter offset in pixel units (for the resolve shader).
    glm::vec2 getJitterOffset() const { return m_jitterOffset; }

    // Previous frame's jitter (for motion vector computation).
    glm::vec2 getPreviousJitterOffset() const { return m_prevJitterOffset; }

    void setSequenceLength(int n) { m_sequenceLength = n; }

private:
    int       m_frameIndex     = 0;
    int       m_sequenceLength = 16;  // Halton sequence length
    glm::vec2 m_jitterOffset     {0.0f};
    glm::vec2 m_prevJitterOffset {0.0f};

    // Halton sequence: base-2 and base-3 for x and y
    static float halton(int index, int base);
};
```

```cpp
// src/engine/renderer/taa_jitter.cpp
#include "engine/renderer/taa_jitter.h"

float TAAJitter::halton(int index, int base) {
    float result = 0.0f;
    float fraction = 1.0f / static_cast<float>(base);
    int i = index;
    while (i > 0) {
        result += static_cast<float>(i % base) * fraction;
        i /= base;
        fraction /= static_cast<float>(base);
    }
    return result;
}

glm::mat4 TAAJitter::apply(const glm::mat4& projection,
                            int screenWidth, int screenHeight)
{
    m_prevJitterOffset = m_jitterOffset;

    // Halton(base 2, base 3), centred around 0 (subtract 0.5)
    int idx = (m_frameIndex % m_sequenceLength) + 1;  // 1-based for Halton
    float jx = halton(idx, 2) - 0.5f;
    float jy = halton(idx, 3) - 0.5f;

    // Store in pixel units (for the resolve shader)
    m_jitterOffset = glm::vec2(jx, jy);

    // Convert to NDC: one pixel = 2.0 / screenSize in clip space
    float offsetX = (2.0f * jx) / static_cast<float>(screenWidth);
    float offsetY = (2.0f * jy) / static_cast<float>(screenHeight);

    // Apply jitter by modifying the projection matrix's translation
    glm::mat4 jitteredProj = projection;
    jitteredProj[2][0] += offsetX;
    jitteredProj[2][1] += offsetY;

    m_frameIndex++;
    return jitteredProj;
}
```

The jitter modifies `projection[2][0]` and `projection[2][1]` -- the elements that translate in clip space before the perspective divide. This shifts the entire scene by a sub-pixel amount without affecting depth or perspective.

```
HALTON SEQUENCE JITTER PATTERN (16 frames)

  Each dot is where a frame's sample lands within a pixel:

  +---+---+---+---+
  |   | . |   | . |
  +---+---+---+---+
  | . |   | . |   |
  +---+---+---+---+
  |   | . |   | . |
  +---+---+---+---+
  | . |   | . |   |
  +---+---+---+---+

  The Halton sequence ensures even distribution across the pixel.
  No clumping, no gaps.  Compare to random jitter, which would
  cluster in some areas and leave others unsampled.
```

### Component 2: Motion Vectors

When the camera moves, what was at pixel (500, 300) last frame may now be at pixel (502, 301). To blend this frame with the correct pixel from the history buffer, we need to know how each pixel moved. This is a **motion vector** (also called **velocity buffer**).

For static geometry, the motion comes entirely from camera movement. For animated objects, it also includes per-vertex skeletal or vertex animation movement.

The motion vector pass renders all objects and computes per-pixel screen-space velocity.

```glsl
// assets/shaders/motion_vectors.vert
#version 460 core

layout (location = 0) in vec3 aPos;

uniform mat4 model;
uniform mat4 viewProjection;       // Current frame's jittered VP
uniform mat4 prevViewProjection;   // Previous frame's VP (also jittered)
uniform mat4 prevModel;            // Previous frame's model matrix
                                   // (same as model for static objects)

out vec4 clipPos;
out vec4 prevClipPos;

void main() {
    clipPos     = viewProjection     * model     * vec4(aPos, 1.0);
    prevClipPos = prevViewProjection * prevModel * vec4(aPos, 1.0);
    gl_Position = clipPos;
}
```

```glsl
// assets/shaders/motion_vectors.frag
#version 460 core

layout (location = 0) out vec2 FragVelocity;  // RG16F

in vec4 clipPos;
in vec4 prevClipPos;

void main() {
    // Convert from clip space to NDC
    vec2 currentNDC  = (clipPos.xy     / clipPos.w)     * 0.5 + 0.5;
    vec2 previousNDC = (prevClipPos.xy / prevClipPos.w) * 0.5 + 0.5;

    // Velocity = how many UV units this pixel moved since last frame
    FragVelocity = currentNDC - previousNDC;
}
```

For a camera-only implementation (no animated objects), you can skip the geometry pass and compute motion vectors in the TAA resolve shader from the depth buffer and the previous/current view-projection matrices:

```glsl
// Camera-only motion vectors (computed in resolve shader, no extra pass)
vec3 worldPos = reconstructWorldPos(depth, uv, invViewProjection);
vec4 prevClip = prevViewProjection * vec4(worldPos, 1.0);
vec2 prevUV   = (prevClip.xy / prevClip.w) * 0.5 + 0.5;
vec2 velocity = uv - prevUV;
```

This is cheaper and sufficient for QEngine if skeletal animation has not been added yet. We will implement the full geometry pass version for completeness, but note that the camera-only shortcut works well for static scenes.

### Component 3: The Velocity Buffer FBO

```cpp
// src/engine/renderer/velocity_buffer.h
#pragma once

#include <glad/glad.h>
#include <iostream>

class VelocityBuffer {
public:
    VelocityBuffer() = default;
    ~VelocityBuffer();

    VelocityBuffer(const VelocityBuffer&) = delete;
    VelocityBuffer& operator=(const VelocityBuffer&) = delete;

    bool init(int width, int height);
    void bind() const;
    void resize(int width, int height);

    GLuint getTexture() const { return m_texture; }

private:
    GLuint m_fbo     = 0;
    GLuint m_texture = 0;
    GLuint m_depth   = 0;
    int    m_width   = 0;
    int    m_height  = 0;
    void create();
    void cleanup();
};
```

```cpp
// src/engine/renderer/velocity_buffer.cpp
#include "engine/renderer/velocity_buffer.h"

VelocityBuffer::~VelocityBuffer() { cleanup(); }

bool VelocityBuffer::init(int width, int height) {
    m_width = width; m_height = height;
    create();
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "ERROR: Velocity buffer framebuffer is not complete!" << std::endl;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

void VelocityBuffer::create() {
    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

    // RG16F: two channels for x and y velocity
    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F,
                 m_width, m_height, 0, GL_RG, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, m_texture, 0);

    // Depth buffer (shared with G-buffer or re-rendered)
    glGenRenderbuffers(1, &m_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, m_depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8,
                          m_width, m_height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                              GL_RENDERBUFFER, m_depth);
}

void VelocityBuffer::bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_width, m_height);
}

void VelocityBuffer::resize(int width, int height) {
    if (width == m_width && height == m_height) return;
    cleanup();
    m_width = width; m_height = height;
    create();
}

void VelocityBuffer::cleanup() {
    if (m_texture) { glDeleteTextures(1, &m_texture); m_texture = 0; }
    if (m_depth)   { glDeleteRenderbuffers(1, &m_depth); m_depth = 0; }
    if (m_fbo)     { glDeleteFramebuffers(1, &m_fbo); m_fbo = 0; }
}
```

### Component 4: The TAA Resolve Shader

This is the heart of TAA. It reads the current jittered frame, uses the motion vector to find the corresponding pixel in the history buffer, clamps the history colour to the current frame's neighbourhood to prevent ghosting, and blends.

```glsl
// assets/shaders/taa_resolve.vert
#version 460 core

layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoords;

out vec2 TexCoords;

void main() {
    TexCoords   = aTexCoords;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
```

```glsl
// assets/shaders/taa_resolve.frag
#version 460 core

out vec4 FragColor;
in vec2 TexCoords;

uniform sampler2D currentFrame;    // This frame's jittered render
uniform sampler2D historyBuffer;   // Last frame's resolved result
uniform sampler2D velocityTexture; // Motion vectors (RG16F)
uniform sampler2D depthTexture;    // Current frame's depth

uniform vec2  jitterOffset;        // Current frame's jitter in UV space
uniform float blendFactor;         // Base blend weight (0.05 = 95% history)

// ── Neighbourhood clamping ───────────────────────────────────────
// Clamp the history colour to the min/max of the current frame's
// 3x3 neighbourhood.  This prevents ghosting: if the history pixel
// contains an object that has since moved away, its colour will be
// outside the neighbourhood range and gets clamped to a plausible value.

vec3 neighbourhoodClamp(vec3 historyColour, vec2 uv) {
    vec2 texelSize = 1.0 / vec2(textureSize(currentFrame, 0));

    vec3 minCol = vec3(9999.0);
    vec3 maxCol = vec3(-9999.0);

    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            vec3 sample_ = texture(currentFrame,
                uv + vec2(float(x), float(y)) * texelSize).rgb;
            minCol = min(minCol, sample_);
            maxCol = max(maxCol, sample_);
        }
    }

    return clamp(historyColour, minCol, maxCol);
}

void main() {
    // ── Read motion vector ───────────────────────────────────────
    vec2 velocity = texture(velocityTexture, TexCoords).rg;

    // ── Reproject: where was this pixel last frame? ──────────────
    vec2 historyUV = TexCoords - velocity;

    // ── Sample current frame and history ─────────────────────────
    vec3 currentColour = texture(currentFrame, TexCoords).rgb;
    vec3 historyColour = texture(historyBuffer, historyUV).rgb;

    // ── Neighbourhood clamping ───────────────────────────────────
    historyColour = neighbourhoodClamp(historyColour, TexCoords);

    // ── Reject history if reprojected UV is out of screen ────────
    float weight = blendFactor;
    if (historyUV.x < 0.0 || historyUV.x > 1.0 ||
        historyUV.y < 0.0 || historyUV.y > 1.0) {
        weight = 1.0;  // No valid history -- use current frame only
    }

    // ── Velocity-based weight adjustment ─────────────────────────
    // Fast-moving pixels get more weight on the current frame
    // to reduce ghosting during rapid motion.
    float speed = length(velocity);
    weight = clamp(weight + speed * 10.0, blendFactor, 0.5);

    // ── Blend ────────────────────────────────────────────────────
    vec3 result = mix(historyColour, currentColour, weight);
    FragColor = vec4(result, 1.0);
}
```

### Understanding Neighbourhood Clamping

This is the key innovation that makes TAA practical. Without it, ghosting is terrible.

```
NEIGHBOURHOOD CLAMPING -- WHY IT MATTERS

  Scenario: a red crate moves to the right between frames.

  Frame N-1:  [red crate] [grey wall]     History at pixel X: red
  Frame N:    [grey wall] [red crate]     Current at pixel X: grey

  Without clamping:
    Blend red history with grey current = pinkish ghost artifact
    The red crate leaves a trail behind it.

  With neighbourhood clamping:
    Current frame 3x3 neighbourhood at pixel X: all grey
    min = grey, max = grey
    Clamp red history to [grey, grey] = grey
    Blend grey with grey = grey (correct!)

  The clamp detects that "red" is impossible at this pixel now
  (nothing in the neighbourhood is red) and forces the history
  to a plausible value.
```

### The TAA Manager

```cpp
// src/engine/renderer/taa_manager.h
#pragma once

#include "engine/renderer/taa_jitter.h"
#include "engine/renderer/velocity_buffer.h"
#include "engine/renderer/screen_quad.h"
#include "engine/renderer/shader.h"
#include <glad/glad.h>
#include <glm/glm.hpp>

class TAAManager {
public:
    TAAManager() = default;
    ~TAAManager();

    TAAManager(const TAAManager&) = delete;
    TAAManager& operator=(const TAAManager&) = delete;

    bool init(int width, int height);
    void resize(int width, int height);

    // Call before rendering the scene to get the jittered projection.
    glm::mat4 jitterProjection(const glm::mat4& projection,
                               int screenWidth, int screenHeight);

    // Render velocity pass (call after geometry pass with same objects).
    void renderVelocity(/* scene objects, model matrices, etc. */
                        Shader& velocityShader,
                        const glm::mat4& viewProjection,
                        const glm::mat4& prevViewProjection);

    // Resolve: blend current frame with history.
    void resolve(GLuint currentFrameTexture, GLuint depthTexture,
                 Shader& resolveShader, const ScreenQuad& quad);

    // The resolved output -- feed this to post-processing.
    GLuint getOutputTexture() const { return m_resolvedTexture[m_currentIndex]; }

    // Store current VP for next frame's motion vectors.
    void endFrame(const glm::mat4& viewProjection);

    VelocityBuffer&       getVelocityBuffer()       { return m_velocityBuffer; }
    const VelocityBuffer& getVelocityBuffer() const { return m_velocityBuffer; }

    void setBlendFactor(float f) { m_blendFactor = f; }
    void setSequenceLength(int n) { m_jitter.setSequenceLength(n); }

private:
    TAAJitter      m_jitter;
    VelocityBuffer m_velocityBuffer;

    // Double-buffered history: we read from one and write to the other.
    GLuint m_resolvedFBO[2]     = {0, 0};
    GLuint m_resolvedTexture[2] = {0, 0};
    int    m_currentIndex = 0;

    int   m_width  = 0;
    int   m_height = 0;
    float m_blendFactor = 0.05f;  // 5% current, 95% history

    glm::mat4 m_prevViewProjection{1.0f};

    void createResolveBuffers();
    void cleanupResolveBuffers();
};
```

```cpp
// src/engine/renderer/taa_manager.cpp
#include "engine/renderer/taa_manager.h"
#include <iostream>

TAAManager::~TAAManager() { cleanupResolveBuffers(); }

bool TAAManager::init(int width, int height) {
    m_width = width; m_height = height;

    if (!m_velocityBuffer.init(width, height)) return false;

    createResolveBuffers();

    // Verify both FBOs
    for (int i = 0; i < 2; ++i) {
        glBindFramebuffer(GL_FRAMEBUFFER, m_resolvedFBO[i]);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "ERROR: TAA resolve FBO " << i << " is not complete!" << std::endl;
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return false;
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

void TAAManager::createResolveBuffers() {
    for (int i = 0; i < 2; ++i) {
        glGenFramebuffers(1, &m_resolvedFBO[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, m_resolvedFBO[i]);

        glGenTextures(1, &m_resolvedTexture[i]);
        glBindTexture(GL_TEXTURE_2D, m_resolvedTexture[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F,
                     m_width, m_height, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, m_resolvedTexture[i], 0);
    }
}

void TAAManager::cleanupResolveBuffers() {
    for (int i = 0; i < 2; ++i) {
        if (m_resolvedTexture[i]) {
            glDeleteTextures(1, &m_resolvedTexture[i]);
            m_resolvedTexture[i] = 0;
        }
        if (m_resolvedFBO[i]) {
            glDeleteFramebuffers(1, &m_resolvedFBO[i]);
            m_resolvedFBO[i] = 0;
        }
    }
}

glm::mat4 TAAManager::jitterProjection(const glm::mat4& projection,
                                        int screenWidth, int screenHeight) {
    return m_jitter.apply(projection, screenWidth, screenHeight);
}

void TAAManager::renderVelocity(Shader& velocityShader,
                                const glm::mat4& viewProjection,
                                const glm::mat4& prevViewProjection) {
    m_velocityBuffer.bind();
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    velocityShader.use();
    velocityShader.setMat4("viewProjection", viewProjection);
    velocityShader.setMat4("prevViewProjection", prevViewProjection);

    // Here you would iterate over all scene objects and draw them,
    // setting model and prevModel uniforms per object.
    // For static objects: prevModel == model.
    // For animated objects: prevModel is last frame's transform.
    //
    // Example:
    // for (auto& [entity, mesh, transform] : view) {
    //     velocityShader.setMat4("model", transform.matrix);
    //     velocityShader.setMat4("prevModel", transform.prevMatrix);
    //     mesh.draw();
    // }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void TAAManager::resolve(GLuint currentFrameTexture, GLuint depthTexture,
                         Shader& resolveShader, const ScreenQuad& quad) {
    int writeIndex = m_currentIndex;
    int readIndex  = 1 - m_currentIndex;  // History is the other buffer

    glBindFramebuffer(GL_FRAMEBUFFER, m_resolvedFBO[writeIndex]);
    glViewport(0, 0, m_width, m_height);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);

    resolveShader.use();

    // Current frame (jittered)
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, currentFrameTexture);
    resolveShader.setInt("currentFrame", 0);

    // History buffer (previous frame's resolved output)
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_resolvedTexture[readIndex]);
    resolveShader.setInt("historyBuffer", 1);

    // Velocity
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_velocityBuffer.getTexture());
    resolveShader.setInt("velocityTexture", 2);

    // Depth
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, depthTexture);
    resolveShader.setInt("depthTexture", 3);

    resolveShader.setVec2("jitterOffset", m_jitter.getJitterOffset());
    resolveShader.setFloat("blendFactor", m_blendFactor);

    quad.draw();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Swap buffers: next frame reads what we just wrote
    m_currentIndex = writeIndex;
}

void TAAManager::endFrame(const glm::mat4& viewProjection) {
    m_prevViewProjection = viewProjection;
    m_currentIndex = 1 - m_currentIndex;
}

void TAAManager::resize(int width, int height) {
    if (width == m_width && height == m_height) return;
    m_velocityBuffer.resize(width, height);
    cleanupResolveBuffers();
    m_width = width; m_height = height;
    createResolveBuffers();
}
```

### TAA Pros and Cons

**Pros:** Best quality of all three techniques. Smooths geometric edges, specular aliasing, and sub-pixel detail. Handles thin features (wires, distant geometry) that FXAA misses. Cost is low -- one velocity pass and one resolve pass.

**Cons:** Ghosting on fast-moving objects (mitigated by neighbourhood clamping and velocity-based weight). Requires motion vectors, which adds a render pass. Slight input lag feeling because the displayed image is a blend of current and past frames. More complex to implement and debug.

---

## Comparison: When to Use Each Technique

```
AA TECHNIQUE COMPARISON

  Technique   Quality    Performance    Complexity    Deferred?
  ---------   -------    -----------    ----------    ---------
  None        --         Free           None          N/A
  MSAA 4x     Good       ~2-3 ms        Low           No *
  FXAA        Moderate   ~0.5 ms        Low           Yes
  TAA         Excellent  ~1-2 ms        High          Yes

  * MSAA works with forward rendering only (or requires multisampled
    G-buffer, which is impractical).

  Edge quality:     TAA > MSAA > FXAA > None
  Sub-pixel:        TAA >> FXAA ~ MSAA > None
  Specular AA:      TAA >> FXAA > MSAA > None
  Ghosting risk:    TAA (some)  |  MSAA, FXAA: none
  Memory overhead:  TAA (~2 history buffers + velocity)
                    MSAA (Nx framebuffer samples)
                    FXAA (none -- operates in-place)
  Detail blur:      FXAA (most) > TAA (some) > MSAA (none)
```

**Recommended defaults for QEngine:**
- Deferred rendering path (opaque geometry): **TAA** as the default, **FXAA** as the fallback for simpler setups or lower-end hardware
- Forward rendering path (transparent objects): **MSAA 4x** for particle and glass edges
- Allow `"none"` for debugging and benchmarking

---

## Integration with RenderPipeline

AA is a configurable pass. The `ConfigManager` controls which technique is active.

```lua
-- config.lua

aa = {
    mode = "taa",       -- "taa", "fxaa", "msaa", "none"
    fxaa = {
        enabled = true, -- Included in TAA mode too (as a final cleanup)
    },
    taa = {
        blend_factor     = 0.05,  -- 5% current, 95% history
        sequence_length  = 16,    -- Halton sequence length
    },
    msaa = {
        samples = 4,    -- 2, 4, or 8
    },
}
```

```cpp
// src/engine/renderer/aa_manager.h
#pragma once

#include "engine/renderer/aa_fxaa.h"
#include "engine/renderer/taa_manager.h"

#include <string>

enum class AAMode {
    None,
    FXAA,
    TAA,
    MSAA  // Forward path only
};

class AAManager {
public:
    AAManager() = default;

    bool init(int width, int height, const std::string& mode);
    void resize(int width, int height);

    AAMode getMode() const { return m_mode; }

    FXAAPass&   getFXAA()   { return m_fxaa; }
    TAAManager& getTAA()    { return m_taa; }

    // Convert string from config to enum
    static AAMode parseMode(const std::string& str);

private:
    AAMode     m_mode = AAMode::TAA;
    FXAAPass   m_fxaa;
    TAAManager m_taa;
};
```

```cpp
// src/engine/renderer/aa_manager.cpp
#include "engine/renderer/aa_manager.h"

AAMode AAManager::parseMode(const std::string& str) {
    if (str == "fxaa") return AAMode::FXAA;
    if (str == "taa")  return AAMode::TAA;
    if (str == "msaa") return AAMode::MSAA;
    if (str == "none") return AAMode::None;
    return AAMode::TAA;  // Default
}

bool AAManager::init(int width, int height, const std::string& mode) {
    m_mode = parseMode(mode);

    // FXAA is always initialised (used as a pass in TAA mode too)
    if (!m_fxaa.init(width, height)) return false;

    if (m_mode == AAMode::TAA) {
        if (!m_taa.init(width, height)) return false;
    }

    return true;
}

void AAManager::resize(int width, int height) {
    m_fxaa.resize(width, height);
    if (m_mode == AAMode::TAA) {
        m_taa.resize(width, height);
    }
}
```

### Updated Render Pipeline

```
UPDATED RENDER PIPELINE WITH AA

  Pass    Target              What Happens
  ----    ------              -----------------------
  1       Shadow FBO          Shadow depth pass (Ch 29)
  2       G-buffer FBO        Geometry pass -- with jittered projection if TAA
  3       Velocity FBO        Motion vector pass (TAA only)
  4       SSAO FBO            SSAO pass (Ch 53)
  5       SSAO Blur FBO       SSAO blur (Ch 53)
  6       Scene FBO           Deferred lighting
  7       Scene FBO           Skybox
  8       Scene FBO           Forward pass (transparent, MSAA if enabled)
  9       Scene FBO           View model
  10      TAA Resolve FBO     TAA resolve: blend with history (TAA only)
  11      Post-process FBO    Bloom, tone mapping
  12      FXAA FBO            FXAA pass (FXAA mode, or final cleanup in TAA)
  13      Default FBO         HUD overlay
```

```cpp
// engine/renderer/render_pipeline.cpp (updated)

void RenderPipeline::execute(RenderContext& ctx) {
    // ── Jitter projection for TAA ────────────────────────────────
    glm::mat4 projection = ctx.camera.getProjectionMatrix();
    if (ctx.aaManager.getMode() == AAMode::TAA) {
        projection = ctx.aaManager.getTAA().jitterProjection(
            projection, ctx.window.getWidth(), ctx.window.getHeight());
    }
    ctx.currentProjection = projection;

    renderShadows(ctx);
    renderGBuffer(ctx);                     // Uses jittered projection
    renderMotionVectors(ctx);               // NEW -- TAA only
    renderSSAO(ctx);
    renderSSAOBlur(ctx);
    renderDeferredLighting(ctx);
    renderSkybox(ctx);
    renderForwardTransparent(ctx);
    renderViewModels(ctx);
    renderTAAResolve(ctx);                  // NEW -- TAA only
    renderPostProcess(ctx);
    renderFXAA(ctx);                        // NEW -- FXAA or TAA cleanup
    renderHUD(ctx);

    // ── Store matrices for next frame ────────────────────────────
    if (ctx.aaManager.getMode() == AAMode::TAA) {
        glm::mat4 vp = projection * ctx.camera.getViewMatrix();
        ctx.aaManager.getTAA().endFrame(vp);
    }
}

// ── Pass 3: Motion Vectors ───────────────────────────────────────
void RenderPipeline::renderMotionVectors(RenderContext& ctx) {
    if (ctx.aaManager.getMode() != AAMode::TAA) return;

    auto shader = ctx.shaders.get("motion_vectors");
    if (!shader) return;

    glm::mat4 currentVP = ctx.currentProjection * ctx.camera.getViewMatrix();
    ctx.aaManager.getTAA().renderVelocity(
        *shader, currentVP, ctx.prevViewProjection);
}

// ── Pass 10: TAA Resolve ─────────────────────────────────────────
void RenderPipeline::renderTAAResolve(RenderContext& ctx) {
    if (ctx.aaManager.getMode() != AAMode::TAA) return;

    auto shader = ctx.shaders.get("taa_resolve");
    if (!shader) return;

    ctx.aaManager.getTAA().resolve(
        ctx.sceneFBO.getColourTexture(),
        ctx.gbuffer.getTexture(GBuffer::Depth),
        *shader, ctx.screenQuad);
}

// ── Pass 12: FXAA ────────────────────────────────────────────────
void RenderPipeline::renderFXAA(RenderContext& ctx) {
    AAMode mode = ctx.aaManager.getMode();
    if (mode != AAMode::FXAA && mode != AAMode::TAA) return;

    auto shader = ctx.shaders.get("fxaa");
    if (!shader) return;

    // Input: post-processed scene (after tone mapping)
    GLuint inputTexture = ctx.postProcessFBO.getColourTexture();
    if (mode == AAMode::TAA) {
        // TAA's resolved output has already been tone-mapped
        inputTexture = ctx.postProcessFBO.getColourTexture();
    }

    ctx.aaManager.getFXAA().execute(inputTexture, *shader, ctx.screenQuad);
}
```

### Initialisation

```cpp
// In PlayingState::init()
auto& config = registry.ctx().get<ConfigManager>();

std::string aaMode = config.get<std::string>("aa.mode", "taa");
m_aaManager.init(m_window.getWidth(), m_window.getHeight(), aaMode);

if (m_aaManager.getMode() == AAMode::TAA) {
    m_aaManager.getTAA().setBlendFactor(
        config.get<float>("aa.taa.blend_factor", 0.05f));
    m_aaManager.getTAA().setSequenceLength(
        config.get<int>("aa.taa.sequence_length", 16));
}

// Load AA shaders
m_shaderCache.load("fxaa",
    "assets/shaders/fxaa.vert", "assets/shaders/fxaa.frag");
m_shaderCache.load("taa_resolve",
    "assets/shaders/taa_resolve.vert", "assets/shaders/taa_resolve.frag");
m_shaderCache.load("motion_vectors",
    "assets/shaders/motion_vectors.vert", "assets/shaders/motion_vectors.frag");
```

---

## Common Pitfalls

**FXAA has no visible effect.** The `inverseScreenSize` uniform is not set correctly. It must be `1.0 / vec2(width, height)`. Also check that the input texture uses `GL_LINEAR` filtering -- FXAA relies on bilinear interpolation for sub-pixel sampling.

**TAA produces extreme ghosting.** The velocity buffer is incorrect. Check that `prevViewProjection` is actually being stored from the previous frame. A common mistake is computing it from the current frame's matrices.

**TAA makes the image blurry.** The jitter is too large or the blend factor is too high. The default blend factor of 0.05 means 95% history, 5% current -- this converges quickly. If the image looks soft, ensure the jitter values are being subtracted when sampling the current frame for the resolve.

**TAA jitters the HUD / UI text.** The HUD pass must use the un-jittered projection matrix. Only the geometry pass, velocity pass, and lighting pass should use the jittered projection. Overlay rendering should bypass TAA entirely.

**MSAA and deferred rendering produce no anti-aliasing.** As discussed, MSAA has no effect on the deferred lighting pass. It only works for geometry rendered in the forward path (transparent objects). If you see no improvement, verify that the forward objects are actually drawing into a multisampled FBO.

**Shimmering returns after enabling TAA.** Make sure the geometry pass uses the jittered projection matrix. If only the lighting pass is jittered but the G-buffer was rendered without jitter, the sub-pixel offsets do not propagate correctly.

---

## C++ Concept Sidebar: Halton Sequences

TAA jitter uses a Halton sequence rather than random offsets. Understanding why requires knowing what a **low-discrepancy sequence** is and why it matters for sampling.

### The Problem with Random Points

Suppose you want to place 16 sample points inside a unit square. Using random numbers (`std::uniform_real_distribution`), you might get this:

```
RANDOM SAMPLES (16 points)

  +----------------------------------+
  |    .   .                         |
  |         .  .                     |
  |                   .              |
  |  .                    .          |
  |                                  |
  |        .                 .       |
  |                                  |
  |            . .                   |
  |  .                  .            |
  |                         .       .|
  +----------------------------------+

  Some areas have clusters of points (wasted).
  Some areas have gaps (undersampled).
  With only 16 points, the uneven coverage is visible.
```

### Low-Discrepancy Sequences

A low-discrepancy (or quasi-random) sequence distributes points as evenly as possible. The **discrepancy** measures how far the point distribution deviates from perfectly uniform. Random sequences have O(1/sqrt(N)) discrepancy. Halton sequences achieve O(log(N)/N), which is dramatically better for small N.

```
HALTON SEQUENCE (16 points, base 2 + base 3)

  +----------------------------------+
  | .       .       .       .        |
  |                                  |
  |     .       .       .       .    |
  |                                  |
  | .       .       .       .        |
  |                                  |
  |     .       .       .       .    |
  |                                  |
  +----------------------------------+

  Even coverage.  No clusters, no gaps.
  Every sub-region of the square gets roughly proportional points.
```

### How the Halton Sequence Works

The Halton sequence in base `b` works by reflecting the digits of the integer index in base `b` around the decimal point:

```
HALTON SEQUENCE IN BASE 2:

  Index    Binary    Reflected    Value
  -----    ------    ---------    -----
    1        1         0.1         0.5
    2       10         0.01        0.25
    3       11         0.11        0.75
    4      100         0.001       0.125
    5      101         0.101       0.625
    6      110         0.011       0.375
    7      111         0.111       0.875

  The values automatically fill in the gaps between previous values.
  First bisects [0,1] at 0.5.
  Then bisects [0, 0.5] and [0.5, 1].
  Then quarters, eighths, etc.
```

For 2D sampling, we use two different bases (2 and 3) for the x and y coordinates. This avoids the correlation that would occur if both dimensions used the same base.

### The Implementation

```cpp
float halton(int index, int base) {
    float result = 0.0f;
    float fraction = 1.0f / static_cast<float>(base);
    int i = index;
    while (i > 0) {
        result += static_cast<float>(i % base) * fraction;
        i /= base;
        fraction /= static_cast<float>(base);
    }
    return result;
}

// Usage: 2D Halton point for TAA jitter
float x = halton(frameIndex, 2) - 0.5f;  // Centre around 0
float y = halton(frameIndex, 3) - 0.5f;
```

The `-0.5f` centres the sequence around zero so that the jitter offsets are roughly symmetric. Without this, the projection would be consistently biased in one direction.

### Beyond Anti-Aliasing

Halton sequences (and other low-discrepancy sequences like Sobol and Hammersley) appear throughout real-time and offline rendering:

- **Monte Carlo ray tracing.** Quasi-random samples converge faster than random samples, producing less noise for the same sample count.
- **SSAO.** The 64-sample hemisphere kernel from Chapter 53 could use Halton points instead of random points for more uniform hemisphere coverage.
- **Soft shadows.** Sampling the area light with low-discrepancy points reduces noise in penumbra regions.
- **Depth of field.** Sampling the lens aperture for bokeh effects benefits from even distribution.
- **Blue noise.** A related concept where the frequency spectrum is concentrated at high frequencies, avoiding the low-frequency clumping that produces visible patterns. Blue noise textures are often used as an alternative to Halton sequences for spatial (rather than temporal) sampling.

The general principle: whenever you need N sample points in some domain, low-discrepancy sequences give better coverage than random sampling, especially when N is small (under ~256). For large N, random sampling eventually catches up, but in real-time rendering we almost always have small N due to performance constraints.

---

## File Summary

| File | Status | Purpose |
|------|--------|---------|
| `src/engine/renderer/aa_fxaa.h` | **New** | `FXAAPass` class -- FBO and execute method for FXAA |
| `src/engine/renderer/aa_fxaa.cpp` | **New** | `FXAAPass` implementation |
| `src/engine/renderer/taa_jitter.h` | **New** | `TAAJitter` class -- Halton sequence projection jitter |
| `src/engine/renderer/taa_jitter.cpp` | **New** | `TAAJitter` implementation |
| `src/engine/renderer/velocity_buffer.h` | **New** | `VelocityBuffer` class -- RG16F FBO for motion vectors |
| `src/engine/renderer/velocity_buffer.cpp` | **New** | `VelocityBuffer` implementation |
| `src/engine/renderer/taa_manager.h` | **New** | `TAAManager` class -- owns jitter, velocity, history buffers |
| `src/engine/renderer/taa_manager.cpp` | **New** | `TAAManager` implementation -- jitter, velocity, resolve |
| `src/engine/renderer/aa_manager.h` | **New** | `AAManager` class -- top-level AA mode selection |
| `src/engine/renderer/aa_manager.cpp` | **New** | `AAManager` implementation |
| `assets/shaders/fxaa.vert` | **New** | FXAA vertex shader (full-screen quad) |
| `assets/shaders/fxaa.frag` | **New** | FXAA fragment shader -- edge detection, search, blend |
| `assets/shaders/taa_resolve.vert` | **New** | TAA resolve vertex shader (full-screen quad) |
| `assets/shaders/taa_resolve.frag` | **New** | TAA resolve fragment shader -- history blend, neighbourhood clamp |
| `assets/shaders/motion_vectors.vert` | **New** | Motion vector vertex shader -- current and previous clip positions |
| `assets/shaders/motion_vectors.frag` | **New** | Motion vector fragment shader -- per-pixel velocity output |
| `src/engine/renderer/render_pipeline.cpp` | **Modified** | Added motion vector, TAA resolve, and FXAA passes |
| `config.lua` | **Modified** | Added `aa` configuration section |

---

## What's Next

In **Chapter 55**, we will implement **Profiling and Optimisation** -- GPU timer queries, a frame profiler overlay, and systematic approaches to finding and fixing performance bottlenecks. With SSAO, deferred lighting, and now anti-aliasing all running, our render pipeline has enough stages that performance tuning becomes essential. We will build tools to measure exactly how long each pass takes, identify which passes dominate frame time, and apply targeted optimisations to keep the engine running smoothly at 60 FPS.