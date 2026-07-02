# Chapter 53: Screen-Space Ambient Occlusion (SSAO)

## What You'll Learn
- Why a flat ambient term looks wrong -- the missing contact shadows problem
- What ambient occlusion is and how it approximates global illumination's self-shadowing
- The SSAO algorithm (John Chapman / Crytek style): hemisphere sampling, depth comparison, range checking
- Generating a sample kernel -- random hemisphere points biased towards the surface
- The 4x4 noise texture -- tiling random rotations to eliminate banding without extra samples
- Writing the SSAO fragment shader: TBN from normal + noise, depth comparison, occlusion factor
- The blur pass -- a 4x4 box blur to smooth the noisy SSAO output
- Integrating SSAO into the deferred lighting pass as a multiplier on the ambient term
- Where SSAO fits in the RenderPipeline: G-buffer, SSAO, SSAO Blur, Deferred Lighting
- Configuring sample count and radius via ConfigManager for quality vs performance tuning
- C++ concept: Pseudo-random number generation -- why `rand()` is bad, `std::mt19937`, uniform distributions, and deterministic seeds

---

## The Problem: Flat Ambient Light

Look at any scene rendered with our deferred lighting pass from Chapter 52. The directional and point lights create highlights and shadows, the PBR BRDF gives realistic specular reflections, and shadow mapping casts sharp shadows from the sun. But look at the corners of a room. Look at where a crate sits on the floor. Look at the crease where two walls meet.

They are all the same brightness as the flat, open surfaces around them.

That line in our deferred lighting shader is the culprit:

```glsl
// From deferred_lighting.frag (Ch 52)
vec3 ambient = vec3(0.03) * albedo * ao;
```

The `ao` value comes from the material's AO texture map -- a pre-baked per-texel value that represents occlusion within the texture itself (like the crevices in a brick pattern). It says nothing about the geometry of the scene. A crate placed in a corner gets the same ambient light as a crate floating in the middle of an open arena.

In reality, corners and crevices receive less ambient light because surrounding geometry blocks incoming light from many directions. These are **contact shadows** -- subtle darkening where surfaces meet.

```
THE MISSING CONTACT SHADOWS

  Real life:                          Our renderer:

  Wall          Wall                  Wall          Wall
  |            /                      |            /
  |  darker   /                       |  same     /
  |  corner  /                        |  bright  /
  |_________/                         |_________/
  Floor                               Floor

  The corner is darker because         The ambient term is constant.
  the two walls block light from       Every pixel gets vec3(0.03).
  most directions.                     Corners look identical to open floor.
```

We need a way to estimate, at each pixel, how much of the surrounding hemisphere is blocked by nearby geometry. This is **ambient occlusion**.

---

## The SSAO Algorithm

**Screen-Space Ambient Occlusion** is a real-time approximation. Instead of casting rays into the 3D scene, it samples the depth buffer around each pixel. If nearby sample points are behind geometry (according to the depth buffer), those samples are considered occluded. More occluded samples means darker ambient.

```
SSAO CONCEPT

  For each pixel on screen:
    1. Read the surface normal at this pixel
    2. Generate random sample points in a hemisphere above the surface
    3. For each sample, project to screen space and check the depth buffer:
       - Geometry IN FRONT of the sample = occluded
       - Nothing in front = unoccluded
    4. Occlusion = occluded_samples / total_samples
    5. Multiply the ambient term by (1 - occlusion)

  Side view of a corner:

  Wall |||
       |||      Hemisphere of samples around point P
       |||
       ||| . * . . .        * = occluded (wall is in front)
       |||* * P . . .        . = unoccluded (open space)
       |||* * . . . .
  _____||| . . . . .
  Floor

  5 out of 16 samples occluded --> AO = 0.69 (darker)
  An open floor pixel: 0 of 16 --> AO = 1.0  (full brightness)
```

The full pipeline:

```
SSAO PIPELINE

  G-buffer          SSAO FBO          SSAO Blur FBO      Deferred Lighting
  (from Ch 52)      (R8, noisy)       (R8, smooth)       (reads blurred AO)
  +-----------+     +-----------+     +-----------+      +-----------+
  | Position  |---->|           |     |           |      |           |
  | Normal    |---->| SSAO      |---->| 4x4 Box   |---->| Ambient * |
  | Depth     |---->| Shader    |     | Blur      |     | ssaoValue |
  +-----------+     +-----------+     +-----------+      +-----------+
                         ^
                    Kernel samples
                    + Noise texture
```

---

## The Sample Kernel

The kernel is a set of random 3D points inside a unit hemisphere. We only sample above the surface (along the normal) because points below the surface would always be "occluded," biasing everything dark. We also distribute samples closer to the origin -- occlusion matters most near the surface where geometry actually contacts.

```
KERNEL DISTRIBUTION

  Uniform (wasteful):               Biased (more samples near surface):

      . . . . . .                        .   .     .
    . . . . . . . .                    . . .   .
  . . . . . . . . . .               . . . . . .
  . . . . P . . . . .             . . . . . P . . . . .
  ========================       ========================
          Surface                          Surface
```

The scaling formula `0.1 + scale^2 * 0.9` clusters roughly 75% of samples within the first half of the hemisphere radius.

---

## The Noise Texture

With 64 samples per pixel, every pixel uses the same kernel orientations, producing visible banding. A 4x4 texture of random rotation vectors, tiled across the screen with `GL_REPEAT`, rotates the kernel differently per pixel. The result is high-frequency noise instead of low-frequency banding -- and noise is easily removed with a blur.

```
WITHOUT NOISE (64 samples):       WITH NOISE + BLUR:

  Visible banding patterns          Smooth, clean AO gradients
  (same kernel rotation             (randomised kernel + 4x4 blur
   at every pixel)                   exactly covers the noise tile)
```

The noise vectors have `z = 0` because they represent rotations in the tangent plane. The blur kernel is 4x4 to match the tile size.

---

## The SSAO Framebuffer

The SSAO pass outputs a single-channel texture. We need two FBOs -- one for the raw output and one for the blurred result.

```cpp
// src/engine/renderer/ssao_buffer.h
#pragma once
#include <glad/glad.h>
#include <iostream>

// Single-channel FBO for SSAO output (raw or blurred).
// GL_RED format -- one byte per pixel, ~2 MB at 1080p.
class SSAOBuffer {
public:
    SSAOBuffer() = default;
    ~SSAOBuffer();

    SSAOBuffer(const SSAOBuffer&) = delete;
    SSAOBuffer& operator=(const SSAOBuffer&) = delete;
    SSAOBuffer(SSAOBuffer&& other) noexcept;
    SSAOBuffer& operator=(SSAOBuffer&& other) noexcept;

    bool init(int width, int height);
    void bind() const;
    void resize(int width, int height);

    GLuint getTexture() const { return m_texture; }
    GLuint getID()      const { return m_fbo; }

private:
    GLuint m_fbo = 0, m_texture = 0;
    int    m_width = 0, m_height = 0;
    void create();
    void cleanup();
};
```

```cpp
// src/engine/renderer/ssao_buffer.cpp
#include "engine/renderer/ssao_buffer.h"

SSAOBuffer::~SSAOBuffer() { cleanup(); }

SSAOBuffer::SSAOBuffer(SSAOBuffer&& other) noexcept
    : m_fbo(other.m_fbo), m_texture(other.m_texture),
      m_width(other.m_width), m_height(other.m_height)
{ other.m_fbo = 0; other.m_texture = 0; }

SSAOBuffer& SSAOBuffer::operator=(SSAOBuffer&& other) noexcept {
    if (this != &other) {
        cleanup();
        m_fbo = other.m_fbo; m_texture = other.m_texture;
        m_width = other.m_width; m_height = other.m_height;
        other.m_fbo = 0; other.m_texture = 0;
    }
    return *this;
}

bool SSAOBuffer::init(int width, int height) {
    m_width = width; m_height = height;
    create();
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "ERROR: SSAO framebuffer is not complete!" << std::endl;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

void SSAOBuffer::create() {
    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED,
                 m_width, m_height, 0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, m_texture, 0);
    // No depth attachment -- SSAO is a full-screen pass
}

void SSAOBuffer::bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_width, m_height);
}

void SSAOBuffer::resize(int width, int height) {
    if (width == m_width && height == m_height) return;
    cleanup();
    m_width = width; m_height = height;
    create();
}

void SSAOBuffer::cleanup() {
    if (m_texture) { glDeleteTextures(1, &m_texture); m_texture = 0; }
    if (m_fbo)     { glDeleteFramebuffers(1, &m_fbo); m_fbo = 0; }
}
```

---

## The SSAO Shader

This is the core of the technique. For each pixel, it reads the G-buffer, builds a TBN matrix from the surface normal and a noise rotation, loops over the kernel samples, projects them to screen space, and compares against the depth buffer.

```glsl
// assets/shaders/ssao.vert
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
// assets/shaders/ssao.frag
#version 460 core

out float FragColor;   // Single-channel output (GL_RED)
in vec2 TexCoords;

// ── G-buffer inputs ─────────────────────────────────────────────
uniform sampler2D gPosition;    // World-space position (RGB16F)
uniform sampler2D gNormal;      // World-space normal   (RGB16F)

// ── SSAO inputs ─────────────────────────────────────────────────
uniform sampler2D noiseTexture;  // 4x4 random rotation vectors
uniform vec3  samples[64];       // Hemisphere kernel samples
uniform int   kernelSize;        // Number of samples (default 64)
uniform float radius;            // Sampling radius in world units
uniform float bias;              // Depth bias to prevent self-occlusion
uniform float power;             // Exponent to darken the result

// ── Matrices ────────────────────────────────────────────────────
uniform mat4 projection;
uniform mat4 view;
uniform vec2 screenSize;         // e.g. (1920, 1080)

void main() {
    // ── Step 1: Read surface data from G-buffer ─────────────────
    vec3 fragPos = texture(gPosition, TexCoords).xyz;
    vec3 normal  = normalize(texture(gNormal, TexCoords).rgb);

    // Skip pixels with no geometry (normal is zero-length)
    if (length(texture(gNormal, TexCoords).rgb) < 0.1) {
        FragColor = 1.0;
        return;
    }

    // ── Step 2: Read noise and build TBN matrix ─────────────────
    // The noise texture tiles: UV = screen position / noise size (4)
    vec2 noiseScale = screenSize / 4.0;
    vec3 randomVec  = texture(noiseTexture, TexCoords * noiseScale).xyz;

    // Gram-Schmidt: orthonormal basis from normal + random vector
    vec3 tangent   = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN       = mat3(tangent, bitangent, normal);

    // ── Step 3: Sample the hemisphere ───────────────────────────
    float occlusion = 0.0;

    for (int i = 0; i < kernelSize; ++i) {
        // Transform sample from tangent space to world space
        vec3 samplePos = TBN * samples[i];
        samplePos = fragPos + samplePos * radius;

        // Project to screen space
        vec4 offset = projection * view * vec4(samplePos, 1.0);
        offset.xyz /= offset.w;               // Perspective divide
        offset.xyz  = offset.xyz * 0.5 + 0.5; // Map to [0, 1]

        // What does the depth buffer say is at this screen position?
        vec3 sampledPos = texture(gPosition, offset.xy).xyz;
        float sampleDepthInView = (view * vec4(sampledPos, 1.0)).z;

        // Where is our sample point in view space?
        float samplePointDepth = (view * vec4(samplePos, 1.0)).z;

        // Original fragment depth in view space
        float fragDepthInView = (view * vec4(fragPos, 1.0)).z;

        // ── Range check ─────────────────────────────────────────
        // Ignore samples whose depth difference exceeds the radius.
        // Without this, a wall 50m away falsely occludes a floor pixel.
        float rangeCheck = smoothstep(0.0, 1.0,
            radius / abs(fragDepthInView - sampleDepthInView));

        // Occluded if actual surface is in front of the sample point
        occlusion += (sampleDepthInView >= samplePointDepth + bias ? 1.0 : 0.0)
                   * rangeCheck;
    }

    // ── Normalise and output ────────────────────────────────────
    occlusion = 1.0 - (occlusion / float(kernelSize));
    FragColor = pow(occlusion, power);
}
```

### How the TBN Matrix Works

```
TBN CONSTRUCTION

  Surface normal N = (0, 1, 0)   (pointing up)
  Random vector  R = (0.7, 0.3, 0)  (from noise texture)

  Gram-Schmidt:
    T = normalize(R - N * dot(R, N)) = (1, 0, 0)
    B = cross(N, T)                  = (0, 0, -1)
    TBN = [T | B | N]

  A kernel sample (0.3, 0.2, 0.8) in tangent space becomes:
    TBN * (0.3, 0.2, 0.8) = (0.3, 0.8, -0.2) in world space
    -- mostly "up" along the normal, slightly offset.

  The noise texture randomises T for each pixel, rotating the
  entire hemisphere around the normal axis. Adjacent pixels
  sample different directions, breaking up banding.
```

### The Range Check

Without a range check, distant geometry causes false occlusion. A floor pixel's sample might project onto a wall 20m away -- the depth buffer says "wall here," much closer than the sample, so it is wrongly counted as occluded. The `smoothstep` fades the contribution to zero when the depth difference exceeds the SSAO radius.

---

## The Blur Pass

The SSAO output is noisy. A 4x4 box blur matches the noise texture tile size and averages out the random variations.

```glsl
// assets/shaders/ssao_blur.frag
#version 460 core

out float FragColor;
in vec2 TexCoords;

uniform sampler2D ssaoInput;

void main() {
    vec2 texelSize = 1.0 / vec2(textureSize(ssaoInput, 0));
    float result = 0.0;

    for (int x = -2; x < 2; ++x) {
        for (int y = -2; y < 2; ++y) {
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            result += texture(ssaoInput, TexCoords + offset).r;
        }
    }

    FragColor = result / 16.0;  // 4x4 = 16 samples
}
```

The vertex shader is the same full-screen quad shader as `ssao.vert`. The blur is intentionally simple -- one pass, 16 texture reads. A bilateral blur (which checks depth differences to preserve edges) would be cleaner but costs extra reads. For QEngine, the box blur is sufficient.

---

## The SSAO Manager

This class owns the kernel, noise texture, and both FBOs.

```cpp
// src/engine/renderer/ssao_manager.h
#pragma once

#include "engine/renderer/ssao_buffer.h"
#include "engine/renderer/screen_quad.h"
#include "engine/renderer/shader.h"
#include "engine/renderer/gbuffer.h"
#include <glm/glm.hpp>
#include <vector>
#include <random>

class SSAOManager {
public:
    SSAOManager() = default;
    ~SSAOManager();

    SSAOManager(const SSAOManager&) = delete;
    SSAOManager& operator=(const SSAOManager&) = delete;

    bool init(int width, int height, int sampleCount = 64,
              float radius = 0.5f, float bias = 0.025f, float power = 2.0f);

    void renderSSAO(const GBuffer& gbuffer, const ScreenQuad& quad,
                    Shader& ssaoShader, const glm::mat4& projection,
                    const glm::mat4& view, int screenWidth, int screenHeight);

    void renderBlur(const ScreenQuad& quad, Shader& blurShader);
    void resize(int width, int height);

    GLuint getBlurredTexture() const { return m_blurBuffer.getTexture(); }
    GLuint getRawTexture()     const { return m_ssaoBuffer.getTexture(); }

    void setRadius(float r)     { m_radius = r; }
    void setBias(float b)       { m_bias = b; }
    void setPower(float p)      { m_power = p; }
    void setSampleCount(int n);

    float getRadius()      const { return m_radius; }
    int   getSampleCount() const { return m_sampleCount; }

private:
    SSAOBuffer m_ssaoBuffer;    // Raw noisy output
    SSAOBuffer m_blurBuffer;    // Blurred output

    std::vector<glm::vec3> m_kernel;
    int m_sampleCount = 64;
    GLuint m_noiseTexture = 0;

    float m_radius = 0.5f;
    float m_bias   = 0.025f;
    float m_power  = 2.0f;

    void generateKernel(unsigned int seed = 42);
    void generateNoiseTexture(unsigned int seed = 42);
};
```

```cpp
// src/engine/renderer/ssao_manager.cpp
#include "engine/renderer/ssao_manager.h"

SSAOManager::~SSAOManager() {
    if (m_noiseTexture) { glDeleteTextures(1, &m_noiseTexture); m_noiseTexture = 0; }
}

bool SSAOManager::init(int width, int height, int sampleCount,
                       float radius, float bias, float power) {
    m_sampleCount = sampleCount;
    m_radius = radius; m_bias = bias; m_power = power;

    generateKernel();
    generateNoiseTexture();

    if (!m_ssaoBuffer.init(width, height)) return false;
    if (!m_blurBuffer.init(width, height)) return false;
    return true;
}

void SSAOManager::generateKernel(unsigned int seed) {
    m_kernel.clear();
    m_kernel.reserve(m_sampleCount);

    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    for (int i = 0; i < m_sampleCount; ++i) {
        glm::vec3 sample(
            dist(rng) * 2.0f - 1.0f,   // x: [-1, 1]
            dist(rng) * 2.0f - 1.0f,   // y: [-1, 1]
            dist(rng)                    // z: [ 0, 1] -- hemisphere
        );
        sample = glm::normalize(sample);
        sample *= dist(rng);

        // Accelerating interpolation -- bias towards origin
        float scale = static_cast<float>(i) / static_cast<float>(m_sampleCount);
        scale = glm::mix(0.1f, 1.0f, scale * scale);
        sample *= scale;

        m_kernel.push_back(sample);
    }
}

void SSAOManager::generateNoiseTexture(unsigned int seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    // 4x4 random rotation vectors in the tangent plane (z = 0)
    std::vector<glm::vec3> noise;
    noise.reserve(16);
    for (int i = 0; i < 16; ++i) {
        noise.emplace_back(dist(rng) * 2.0f - 1.0f, dist(rng) * 2.0f - 1.0f, 0.0f);
    }

    glGenTextures(1, &m_noiseTexture);
    glBindTexture(GL_TEXTURE_2D, m_noiseTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 4, 4, 0, GL_RGB, GL_FLOAT, noise.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);  // Tile across screen
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
}

void SSAOManager::renderSSAO(
    const GBuffer& gbuffer, const ScreenQuad& quad,
    Shader& ssaoShader, const glm::mat4& projection,
    const glm::mat4& view, int screenWidth, int screenHeight)
{
    m_ssaoBuffer.bind();
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);

    ssaoShader.use();

    // Bind G-buffer position and normal textures
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gbuffer.getTexture(GBuffer::Position));
    ssaoShader.setInt("gPosition", 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, gbuffer.getTexture(GBuffer::Normal));
    ssaoShader.setInt("gNormal", 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_noiseTexture);
    ssaoShader.setInt("noiseTexture", 2);

    // Upload kernel samples
    for (int i = 0; i < m_sampleCount; ++i)
        ssaoShader.setVec3("samples[" + std::to_string(i) + "]", m_kernel[i]);

    ssaoShader.setInt("kernelSize", m_sampleCount);
    ssaoShader.setFloat("radius", m_radius);
    ssaoShader.setFloat("bias", m_bias);
    ssaoShader.setFloat("power", m_power);
    ssaoShader.setMat4("projection", projection);
    ssaoShader.setMat4("view", view);
    ssaoShader.setVec2("screenSize", glm::vec2(screenWidth, screenHeight));

    quad.draw();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void SSAOManager::renderBlur(const ScreenQuad& quad, Shader& blurShader) {
    m_blurBuffer.bind();
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);

    blurShader.use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_ssaoBuffer.getTexture());
    blurShader.setInt("ssaoInput", 0);

    quad.draw();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void SSAOManager::resize(int width, int height) {
    m_ssaoBuffer.resize(width, height);
    m_blurBuffer.resize(width, height);
}

void SSAOManager::setSampleCount(int n) {
    if (n == m_sampleCount) return;
    m_sampleCount = glm::clamp(n, 8, 128);
    generateKernel();
}
```

---

## Integrating SSAO with Deferred Lighting

The blurred SSAO texture multiplies into the ambient term. This is a small update to the deferred lighting shader from Chapter 52.

```glsl
// assets/shaders/deferred_lighting.frag (changes only)
// Add near the other texture uniforms:
uniform sampler2D ssaoTexture;
uniform int       ssaoEnabled;

// In main(), replace the ambient calculation:
void main() {
    // ... (G-buffer reads and lighting calculations unchanged) ...

    // ── SSAO integration ─────────────────────────────────────────
    // Material AO handles per-texel occlusion (brick crevices, etc).
    // SSAO handles per-pixel geometric occlusion (corners, contacts).
    // Multiplying them combines both effects.
    float ssao = ssaoEnabled == 1 ? texture(ssaoTexture, TexCoords).r : 1.0;
    float combinedAO = ao * ssao;

    vec3 ambient = vec3(0.03) * albedo * combinedAO;
    FragColor = vec4(ambient + Lo, 1.0);
}
```

---

## Updated Render Pipeline

SSAO adds two passes between the G-buffer and deferred lighting.

```
UPDATED RENDER PIPELINE

  Pass    Target              What Happens
  ----    ------              -----------------------
  1       Shadow FBO          Shadow depth pass (Ch 29)
  2       G-buffer FBO        Geometry pass -- opaque objects write surface data
  3       SSAO FBO            SSAO pass -- hemisphere sampling, raw occlusion
  4       SSAO Blur FBO       SSAO blur -- 4x4 box blur
  5       Scene FBO           Deferred lighting (ambient *= SSAO)
  6       Scene FBO           Skybox (depth <= test)
  7       Scene FBO           Forward pass (transparent objects, particles)
  8       Scene FBO           View model (clear depth, weapon on top)
  9       Default FBO         Post-processing (bloom, tone mapping)
  10      Default FBO         HUD (2D overlay)
```

```cpp
// engine/renderer/render_pipeline.cpp (updated)

void RenderPipeline::execute(RenderContext& ctx) {
    renderShadows(ctx);
    renderGBuffer(ctx);
    renderSSAO(ctx);          // NEW
    renderSSAOBlur(ctx);      // NEW
    renderDeferredLighting(ctx);
    renderSkybox(ctx);
    renderForwardTransparent(ctx);
    renderViewModels(ctx);
    renderPostProcess(ctx);
    renderHUD(ctx);
}

// ── Pass 3: SSAO ────────────────────────────────────────────────
void RenderPipeline::renderSSAO(RenderContext& ctx) {
    if (!ctx.ssaoEnabled) return;
    auto shader = ctx.shaders.get("ssao");
    if (!shader) return;

    ctx.ssaoManager.renderSSAO(
        ctx.gbuffer, ctx.screenQuad, *shader,
        ctx.camera.getProjectionMatrix(),
        ctx.camera.getViewMatrix(),
        ctx.window.getWidth(), ctx.window.getHeight());
}

// ── Pass 4: SSAO Blur ───────────────────────────────────────────
void RenderPipeline::renderSSAOBlur(RenderContext& ctx) {
    if (!ctx.ssaoEnabled) return;
    auto shader = ctx.shaders.get("ssao_blur");
    if (!shader) return;

    ctx.ssaoManager.renderBlur(ctx.screenQuad, *shader);
}

// ── Pass 5: Deferred Lighting (updated) ─────────────────────────
void RenderPipeline::renderDeferredLighting(RenderContext& ctx) {
    ctx.sceneFBO.bind();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    auto dirShader = ctx.shaders.get("deferred_lighting");
    auto volShader = ctx.shaders.get("deferred_light_volume");
    if (!dirShader || !volShader) return;

    // Bind SSAO texture for the lighting shader
    dirShader->use();
    if (ctx.ssaoEnabled) {
        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_2D, ctx.ssaoManager.getBlurredTexture());
        dirShader->setInt("ssaoTexture", 5);
        dirShader->setInt("ssaoEnabled", 1);
    } else {
        dirShader->setInt("ssaoEnabled", 0);
    }

    // Gather lights and call deferredLightingPass (unchanged from Ch 52)
    // ...
}
```

---

## Quality vs Performance: ConfigManager Integration

```lua
-- config.lua

ssao = {
    enabled      = true,
    sample_count = 64,     -- 16 = fast, 32 = balanced, 64 = quality
    radius       = 0.5,    -- World units. Larger = more spread out
    bias         = 0.025,  -- Prevents self-occlusion acne
    power        = 2.0,    -- Exponent. Higher = stronger/darker
}
```

```cpp
// In PlayingState::init()
auto& config = registry.ctx().get<ConfigManager>();

m_ssaoEnabled = config.get<bool>("ssao.enabled", true);
m_ssaoManager.init(
    m_window.getWidth(), m_window.getHeight(),
    config.get<int>("ssao.sample_count", 64),
    config.get<float>("ssao.radius", 0.5f),
    config.get<float>("ssao.bias", 0.025f),
    config.get<float>("ssao.power", 2.0f));

// Load SSAO shaders
m_shaderCache.load("ssao",
    "assets/shaders/ssao.vert", "assets/shaders/ssao.frag");
m_shaderCache.load("ssao_blur",
    "assets/shaders/ssao_blur.vert", "assets/shaders/ssao_blur.frag");
```

```
SSAO PERFORMANCE (1080p, mid-range GPU)

  Samples    SSAO Pass    Blur Pass    Total    Quality
  ------     ---------    ---------    -----    -------
     16        0.3 ms       0.1 ms     0.4 ms   Visible noise after blur
     32        0.5 ms       0.1 ms     0.6 ms   Acceptable
     64        0.9 ms       0.1 ms     1.0 ms   Good -- standard choice
    128        1.7 ms       0.1 ms     1.8 ms   Diminishing returns

  At 60 FPS you have 16.7 ms per frame. 1 ms for SSAO is ~6%.
```

The radius also matters: 0.2 gives tight, detailed AO in small crevices; 1.5 catches room-scale occlusion but can produce halos. A radius of 0.5 is a good default for Quake-scale levels.

---

## Common Pitfalls

**SSAO is all white (no effect).** The G-buffer position/normal textures are not bound correctly. Check texture unit assignments.

**SSAO is all black (everything occluded).** The bias is too small -- every surface self-occludes. Increase `bias` to 0.05 or higher.

**Dark halos around objects.** The radius is too large. Reduce it to 0.3-0.5 for typical Quake-scale levels.

**Banding patterns instead of noise.** The noise texture is not tiling. Check `GL_REPEAT` wrapping and that `noiseScale = screenSize / 4.0`.

**SSAO affects the skybox.** Skybox pixels have zero-length normals in the G-buffer. The `length(normal) < 0.1` check should output 1.0 for these pixels.

**Performance is poor.** Reduce `kernelSize` to 32. Ensure `GL_RED` format (not `GL_RGBA`) for the SSAO buffers.

---

## C++ Concept Sidebar: Pseudo-Random Number Generation

The SSAO kernel and noise texture both need random numbers. We used `std::mt19937` and `std::uniform_real_distribution`. Here is why, and why the alternatives are worse.

### Why `rand()` Is Bad

```cpp
// The C way -- DO NOT USE
srand(static_cast<unsigned>(time(nullptr)));
float x = static_cast<float>(rand()) / RAND_MAX;
```

Problems: (1) **Global state** -- two systems calling `rand()` interfere with each other. Thread safety is impossible. (2) **Poor precision** -- `RAND_MAX` is often 32767, giving only 15 bits of resolution. (3) **Non-reproducible** -- seeding with `time()` gives different results every second, making bugs impossible to reproduce. (4) **Low quality** -- many implementations use simple LCGs with visible patterns.

### The Modern C++ Way

```cpp
#include <random>

// Step 1: Choose a generator
std::mt19937 rng(42);  // Mersenne Twister, deterministic seed

// Step 2: Choose a distribution
std::uniform_real_distribution<float> dist(0.0f, 1.0f);

// Step 3: Generate values
float value = dist(rng);  // Full 32-bit precision, reproducible
```

The `<random>` library separates **generators** (produce raw bits: `mt19937`, `minstd_rand`) from **distributions** (shape bits into values: `uniform_real_distribution`, `normal_distribution`). You can swap either independently.

### Why Deterministic Seeds Matter for Rendering

```
Deterministic (seed = 42):
  Run 1: kernel = [0.31, -0.72, 0.44], [0.15, 0.88, 0.21], ...
  Run 2: kernel = [0.31, -0.72, 0.44], [0.15, 0.88, 0.21], ...

  Same visual result every time. Bugs are reproducible.
  Screenshots match between runs.

Non-deterministic (seed = time()):
  Run 1: kernel = [0.31, -0.72, 0.44], ...
  Run 2: kernel = [0.67, 0.12, -0.55], ...

  SSAO looks slightly different every launch.
  "It only happens sometimes" bugs are impossible to track.
```

For rendering, we want "random enough to look good" but "deterministic enough to reproduce." A fixed seed achieves both. If you need truly non-deterministic seeding (gameplay randomness), use `std::random_device`:

```cpp
std::random_device rd;
std::mt19937 rng(rd());  // Hardware entropy -- different every time
```

One practical note: `std::mt19937` has 2.5 KB of state. For SSAO we create one generator at startup and discard it after generating 80 values (64 kernel + 16 noise), so state size does not matter. If you were creating thousands of generators (one per particle), consider `std::minstd_rand` (4 bytes of state) at the cost of lower-quality randomness.

---

## File Summary

| File | Status | Purpose |
|------|--------|---------|
| `src/engine/renderer/ssao_buffer.h` | **New** | `SSAOBuffer` class -- single-channel FBO for SSAO output |
| `src/engine/renderer/ssao_buffer.cpp` | **New** | `SSAOBuffer` implementation -- create, bind, resize, cleanup |
| `src/engine/renderer/ssao_manager.h` | **New** | `SSAOManager` class -- owns kernel, noise, FBOs, render methods |
| `src/engine/renderer/ssao_manager.cpp` | **New** | `SSAOManager` implementation -- generation, SSAO pass, blur pass |
| `src/engine/renderer/render_pipeline.cpp` | **Modified** | Added SSAO and blur passes between G-buffer and lighting |
| `src/engine/renderer/gbuffer_debug.h` | **Modified** | Added SSAORaw and SSAOBlurred debug modes |
| `assets/shaders/ssao.vert` | **New** | SSAO pass vertex shader (full-screen quad) |
| `assets/shaders/ssao.frag` | **New** | SSAO fragment shader -- hemisphere sampling, depth comparison |
| `assets/shaders/ssao_blur.vert` | **New** | SSAO blur vertex shader (full-screen quad, reuses pattern) |
| `assets/shaders/ssao_blur.frag` | **New** | SSAO blur fragment shader -- 4x4 box blur |
| `assets/shaders/deferred_lighting.frag` | **Modified** | Added `ssaoTexture` uniform, multiplied into ambient term |
| `assets/shaders/gbuffer_debug.frag` | **Modified** | Added SSAO debug visualisation modes |
| `config.lua` | **Modified** | Added `ssao` configuration section |

---

## What's Next

In **Chapter 54**, we will implement **Anti-Aliasing** -- specifically FXAA (Fast Approximate Anti-Aliasing). Since deferred rendering breaks traditional MSAA (the G-buffer stores per-pixel data, not per-sample), we need a screen-space solution. FXAA analyses the final rendered image for high-contrast edges and smooths them in a single post-processing pass. It is fast, simple to integrate, and eliminates the jagged edges that become especially visible now that SSAO adds fine detail to every surface.