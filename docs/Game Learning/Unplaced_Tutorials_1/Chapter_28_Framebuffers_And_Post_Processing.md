# Chapter 28: Framebuffers & Post-Processing

## What You'll Learn
- What a Framebuffer Object (FBO) is and why rendering to a texture unlocks post-processing
- Building a complete RAII `Framebuffer` class with colour texture and depth-stencil renderbuffer
- Setting up a full-screen quad to display the rendered texture
- Restructuring the render loop into a two-pass pipeline: scene pass then post-process pass
- Implementing six post-processing effects: invert, greyscale, vignette, bloom (with ping-pong blur), damage flash, and colour grading
- Creating an ECS-compliant `PostProcessSettings` component to control effects at runtime
- RAII for GPU resources: why the Framebuffer class deletes its copy constructor and implements move semantics

---

## Why Post-Processing?

Up to now, QEngine has been rendering directly to the screen. Every draw call writes pixels straight to the **default framebuffer** — the one the window system gave us. That works, but it means we can only affect pixels as we draw them. We cannot go back and modify the final image as a whole.

Post-processing flips this around. Instead of drawing to the screen, we draw to an **off-screen texture**. Once the entire scene is captured in that texture, we draw a single full-screen rectangle textured with it — and in that final draw call, a fragment shader can read, modify, and combine pixels however it likes. Blur, colour grading, bloom, damage flashes — all become simple shader effects applied to the finished image.

```
Without post-processing:           With post-processing:

  Scene geometry                     Scene geometry
       |                                  |
       v                                  v
  Default framebuffer              Off-screen FBO (texture)
       |                                  |
       v                                  v
    Screen                         Full-screen quad + shader
                                          |
                                          v
                                   Default framebuffer
                                          |
                                          v
                                       Screen
```

The cost is one extra full-screen draw per frame (plus one per blur pass for bloom). On modern hardware, this is trivial.

---

## What is a Framebuffer Object (FBO)?

A **Framebuffer Object** is an OpenGL container that holds a set of rendering targets. When you bind an FBO, all subsequent draw calls write to its attachments instead of the screen.

Every OpenGL context has a **default framebuffer** (ID 0). That is the one connected to your window — writing to it puts pixels on screen. Custom FBOs let you redirect rendering to textures or renderbuffers that you control.

An FBO needs at least two attachments:

| Attachment | What it stores | OpenGL type |
|-----------|---------------|-------------|
| **Colour** (GL_COLOR_ATTACHMENT0) | RGBA pixel data | Texture (so we can sample it later) |
| **Depth + Stencil** (GL_DEPTH24_STENCIL8) | Depth and stencil values | Renderbuffer (we never need to sample it) |

Why a **texture** for colour and a **renderbuffer** for depth? We need to read the colour data back in the post-processing pass (by sampling it in a shader), so it must be a texture. We never read the depth/stencil data in a shader, so a renderbuffer is more efficient — the driver can store it in whatever format is fastest.

```
Framebuffer Object (FBO)
 ┌──────────────────────────────────────┐
 │  Colour Attachment 0  ──> Texture    │  <-- we sample this later
 │  Depth/Stencil        ──> RBO       │  <-- internal use only
 └──────────────────────────────────────┘
```

---

## The Framebuffer Class

This is a full RAII wrapper. The constructor allocates everything; the destructor frees everything. No manual cleanup needed.

### src/engine/renderer/framebuffer.h

```cpp
// In src/engine/renderer/framebuffer.h
#pragma once

#include <glad/glad.h>

class Framebuffer {
public:
    Framebuffer(int width, int height);
    ~Framebuffer();

    // Non-copyable (owns GPU resources)
    Framebuffer(const Framebuffer&) = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;

    // Movable
    Framebuffer(Framebuffer&& other) noexcept;
    Framebuffer& operator=(Framebuffer&& other) noexcept;

    // Bind this FBO — all subsequent draws go here
    void bind() const;

    // Unbind — return to the default framebuffer (the screen)
    static void unbind();

    // Resize attachments (call on window resize)
    void resize(int width, int height);

    // Get the colour texture ID for sampling in a shader
    GLuint getColourTexture() const { return m_colourTexture; }

    int getWidth()  const { return m_width; }
    int getHeight() const { return m_height; }

private:
    GLuint m_fbo            = 0;
    GLuint m_colourTexture  = 0;
    GLuint m_depthStencilRBO = 0;
    int    m_width          = 0;
    int    m_height         = 0;

    void create();
    void cleanup();
};
```

### src/engine/renderer/framebuffer.cpp

```cpp
// In src/engine/renderer/framebuffer.cpp
#include "engine/renderer/framebuffer.h"
#include <iostream>
#include <utility>

Framebuffer::Framebuffer(int width, int height)
    : m_width(width), m_height(height)
{
    create();
}

Framebuffer::~Framebuffer() {
    cleanup();
}

// ─── Move constructor ───────────────────────────────────────────
Framebuffer::Framebuffer(Framebuffer&& other) noexcept
    : m_fbo(other.m_fbo),
      m_colourTexture(other.m_colourTexture),
      m_depthStencilRBO(other.m_depthStencilRBO),
      m_width(other.m_width),
      m_height(other.m_height)
{
    other.m_fbo             = 0;
    other.m_colourTexture   = 0;
    other.m_depthStencilRBO = 0;
    other.m_width           = 0;
    other.m_height          = 0;
}

// ─── Move assignment ────────────────────────────────────────────
Framebuffer& Framebuffer::operator=(Framebuffer&& other) noexcept {
    if (this != &other) {
        cleanup();
        m_fbo             = other.m_fbo;
        m_colourTexture   = other.m_colourTexture;
        m_depthStencilRBO = other.m_depthStencilRBO;
        m_width           = other.m_width;
        m_height          = other.m_height;

        other.m_fbo             = 0;
        other.m_colourTexture   = 0;
        other.m_depthStencilRBO = 0;
        other.m_width           = 0;
        other.m_height          = 0;
    }
    return *this;
}

// ─── Create FBO, texture, and renderbuffer ──────────────────────
void Framebuffer::create() {
    // 1. Create the framebuffer object
    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

    // 2. Create the colour attachment (a 2D texture)
    glGenTextures(1, &m_colourTexture);
    glBindTexture(GL_TEXTURE_2D, m_colourTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F,
                 m_width, m_height, 0,
                 GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, m_colourTexture, 0);

    // 3. Create the depth+stencil renderbuffer
    glGenRenderbuffers(1, &m_depthStencilRBO);
    glBindRenderbuffer(GL_RENDERBUFFER, m_depthStencilRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8,
                          m_width, m_height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                              GL_RENDERBUFFER, m_depthStencilRBO);

    // 4. Check completeness
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "ERROR: Framebuffer is not complete!" << std::endl;
    }

    // 5. Unbind so we don't accidentally draw to this FBO
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ─── Bind / Unbind ──────────────────────────────────────────────
void Framebuffer::bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_width, m_height);
}

void Framebuffer::unbind() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ─── Resize ─────────────────────────────────────────────────────
void Framebuffer::resize(int width, int height) {
    if (width == m_width && height == m_height) return;

    m_width  = width;
    m_height = height;

    // Resize the colour texture
    glBindTexture(GL_TEXTURE_2D, m_colourTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F,
                 m_width, m_height, 0,
                 GL_RGBA, GL_FLOAT, nullptr);

    // Resize the depth-stencil renderbuffer
    glBindRenderbuffer(GL_RENDERBUFFER, m_depthStencilRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8,
                          m_width, m_height);
}

// ─── Cleanup ────────────────────────────────────────────────────
void Framebuffer::cleanup() {
    if (m_colourTexture) {
        glDeleteTextures(1, &m_colourTexture);
        m_colourTexture = 0;
    }
    if (m_depthStencilRBO) {
        glDeleteRenderbuffers(1, &m_depthStencilRBO);
        m_depthStencilRBO = 0;
    }
    if (m_fbo) {
        glDeleteFramebuffers(1, &m_fbo);
        m_fbo = 0;
    }
}
```

### Key Details

- **GL_RGBA16F**: We use a 16-bit floating-point colour format instead of the standard GL_RGBA8. This gives us high dynamic range (HDR) — colour values can exceed 1.0, which is essential for the bloom bright-pass extraction later. Without HDR, there are no "bright" pixels to extract.
- **GL_CLAMP_TO_EDGE**: Prevents sampling outside the texture when the post-processing shader reads edge pixels.
- **`nullptr` for data**: We do not upload any pixel data. The texture is just an empty buffer that OpenGL will render into.
- **Completeness check**: `glCheckFramebufferStatus` verifies that the FBO is valid. If the colour format and depth format are incompatible, or an attachment is missing, this catches it.

---

## The Full-Screen Quad

After the scene is rendered into the FBO's texture, we need to display it. We draw a rectangle that covers the entire screen in normalised device coordinates (NDC: -1 to +1 on both axes), and texture it with the FBO's colour attachment.

```
NDC space:

    (-1, 1) ──────── (1, 1)
       │                │
       │   Full-screen  │
       │      quad      │
       │                │
    (-1,-1) ──────── (1,-1)

Two triangles:
  Triangle 1: (-1,-1) (1,-1) (1, 1)
  Triangle 2: (-1,-1) (1, 1) (-1, 1)
```

### src/engine/renderer/screen_quad.h

```cpp
// In src/engine/renderer/screen_quad.h
#pragma once

#include <glad/glad.h>

class ScreenQuad {
public:
    ScreenQuad();
    ~ScreenQuad();

    ScreenQuad(const ScreenQuad&) = delete;
    ScreenQuad& operator=(const ScreenQuad&) = delete;

    // Draw the quad (assumes a shader is already bound)
    void draw() const;

private:
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
};
```

### src/engine/renderer/screen_quad.cpp

```cpp
// In src/engine/renderer/screen_quad.cpp
#include "engine/renderer/screen_quad.h"

// Each vertex: position (x, y), texcoord (u, v)
static constexpr float quadVertices[] = {
    // pos        // uv
    -1.0f, -1.0f,  0.0f, 0.0f,
     1.0f, -1.0f,  1.0f, 0.0f,
     1.0f,  1.0f,  1.0f, 1.0f,

    -1.0f, -1.0f,  0.0f, 0.0f,
     1.0f,  1.0f,  1.0f, 1.0f,
    -1.0f,  1.0f,  0.0f, 1.0f,
};

ScreenQuad::ScreenQuad() {
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices),
                 quadVertices, GL_STATIC_DRAW);

    // Position: layout(location = 0), 2 floats
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                          4 * sizeof(float), (void*)0);

    // Texture coordinates: layout(location = 1), 2 floats
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
                          4 * sizeof(float),
                          (void*)(2 * sizeof(float)));

    glBindVertexArray(0);
}

ScreenQuad::~ScreenQuad() {
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
}

void ScreenQuad::draw() const {
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}
```

### Passthrough Shaders

The simplest post-process shader just copies the texture to the screen with no modification. This is the baseline we build every effect on top of.

```glsl
// assets/shaders/postprocess.vert
#version 460 core

layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoords;

out vec2 TexCoords;

void main() {
    TexCoords = aTexCoords;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
```

```glsl
// assets/shaders/postprocess_passthrough.frag
#version 460 core

in vec2 TexCoords;
out vec4 FragColour;

uniform sampler2D screenTexture;

void main() {
    FragColour = texture(screenTexture, TexCoords);
}
```

---

## The Post-Processing Pipeline

Here is the fundamental change to the render loop. Instead of drawing straight to the screen, we render into the FBO first, then draw the FBO's texture to the screen through a post-processing shader.

```
BEFORE (Chapters 1-27):              AFTER (Chapter 28+):

 glClear(...)                          sceneFBO.bind()
 skybox.render(...)                    glClear(...)
 renderSystem(...)                     skybox.render(...)
 particleSystem(...)                   renderSystem(...)
 hudSystem(...)                        particleSystem(...)
 glfwSwapBuffers(...)                  viewModelSystem(...)
                                       Framebuffer::unbind()

                                       glClear(...)
                                       postProcessShader.use()
                                       bind sceneFBO colour texture
                                       screenQuad.draw()

                                       hudSystem(...)
                                       glfwSwapBuffers(...)
```

Notice that the HUD is drawn **after** unbinding the FBO, directly to the default framebuffer. We do not want post-processing applied to the HUD — bloom and vignette on health bars would look terrible.

### Updated PlayingState::render()

```cpp
// In src/game/states/playing_state.cpp

void PlayingState::render() {
    // ─── Pass 1: Render scene into the FBO ───────────────────────
    m_sceneFBO.bind();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glm::mat4 view       = m_camera.getViewMatrix();
    glm::mat4 projection = m_camera.getProjectionMatrix();

    // 1. Skybox
    m_skybox.render(m_skyboxShader, view, projection);

    // 2. Shadow pass (Chapter 29 — placeholder)
    // shadowSystem(m_registry, m_camera, m_shadowMap);

    // 3. Opaque geometry
    renderSystem(m_registry, m_camera);

    // 4. Transparent geometry / particles
    particleSystem(m_registry, m_camera);

    // 5. View model (weapon in front of camera)
    viewModelSystem(m_registry, m_camera);

    Framebuffer::unbind();

    // ─── Pass 2: Post-processing ─────────────────────────────────
    // Restore viewport to window size (in case FBO was a different size)
    glViewport(0, 0, m_window.getWidth(), m_window.getHeight());
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);  // Full-screen quad needs no depth

    postProcessSystem(m_registry, m_sceneFBO, m_bloomFBOs,
                      m_screenQuad, m_postProcessShaders);

    glEnable(GL_DEPTH_TEST);

    // ─── Pass 3: HUD (directly to default framebuffer) ───────────
    hudSystem(m_registry, m_window);
}
```

---

## Post-Processing Effects

Each effect is a separate fragment shader. They all share the same vertex shader (`postprocess.vert` above) and the same full-screen quad. The difference is only in how the fragment shader modifies the sampled colour.

### Effect 1: Invert Colours

The simplest possible effect. Good for testing that the pipeline works.

```glsl
// assets/shaders/pp_invert.frag
#version 460 core

in vec2 TexCoords;
out vec4 FragColour;

uniform sampler2D screenTexture;

void main() {
    vec3 colour = texture(screenTexture, TexCoords).rgb;
    FragColour = vec4(1.0 - colour, 1.0);
}
```

### Effect 2: Greyscale

Converts to greyscale using perceptual luminance weights. The human eye is most sensitive to green, less to red, least to blue — so we weight accordingly.

```glsl
// assets/shaders/pp_greyscale.frag
#version 460 core

in vec2 TexCoords;
out vec4 FragColour;

uniform sampler2D screenTexture;

void main() {
    vec3 colour = texture(screenTexture, TexCoords).rgb;

    // ITU-R BT.709 luminance coefficients
    float luminance = dot(colour, vec3(0.2126, 0.7152, 0.0722));
    FragColour = vec4(vec3(luminance), 1.0);
}
```

### Effect 3: Vignette

Darkens the edges of the screen based on distance from the centre. This subtly draws the player's eye to the middle of the screen and adds a cinematic feel.

```glsl
// assets/shaders/pp_vignette.frag
#version 460 core

in vec2 TexCoords;
out vec4 FragColour;

uniform sampler2D screenTexture;
uniform float vignetteStrength;  // 0.0 = off, 1.0 = heavy

void main() {
    vec3 colour = texture(screenTexture, TexCoords).rgb;

    // Distance from centre (0,0 at centre, ~0.707 at corners)
    vec2 centreOffset = TexCoords - 0.5;
    float dist = length(centreOffset);

    // Smooth falloff: darken more the further from centre
    float vignette = smoothstep(0.2, 0.9, dist * vignetteStrength);
    colour *= (1.0 - vignette);

    FragColour = vec4(colour, 1.0);
}
```

### Effect 4: Damage Flash

A red tint overlay driven by a timer. When the player takes damage, set the timer to a positive value; it fades out over time.

```glsl
// assets/shaders/pp_damage.frag
#version 460 core

in vec2 TexCoords;
out vec4 FragColour;

uniform sampler2D screenTexture;
uniform float damageIntensity;  // 0.0 = normal, 1.0 = full red

void main() {
    vec3 colour = texture(screenTexture, TexCoords).rgb;

    // Blend towards red based on intensity
    vec3 redTint = vec3(0.8, 0.0, 0.0);
    colour = mix(colour, redTint, damageIntensity * 0.6);

    // Also slightly desaturate to emphasise the hit
    float lum = dot(colour, vec3(0.2126, 0.7152, 0.0722));
    colour = mix(colour, vec3(lum), damageIntensity * 0.3);

    FragColour = vec4(colour, 1.0);
}
```

### Effect 5: Colour Grading / Contrast

Simple tone mapping with adjustable contrast and saturation.

```glsl
// assets/shaders/pp_colour_grade.frag
#version 460 core

in vec2 TexCoords;
out vec4 FragColour;

uniform sampler2D screenTexture;
uniform float contrast;    // 1.0 = normal, >1 = more contrast
uniform float saturation;  // 1.0 = normal, 0 = greyscale, >1 = vivid

void main() {
    vec3 colour = texture(screenTexture, TexCoords).rgb;

    // Contrast: shift around midpoint (0.5)
    colour = (colour - 0.5) * contrast + 0.5;

    // Saturation: blend between luminance and full colour
    float lum = dot(colour, vec3(0.2126, 0.7152, 0.0722));
    colour = mix(vec3(lum), colour, saturation);

    // Clamp to valid range
    colour = clamp(colour, 0.0, 1.0);

    FragColour = vec4(colour, 1.0);
}
```

### Effect 6: Bloom

Bloom is the most complex effect. It makes bright areas of the scene glow and bleed into surrounding pixels. This requires **three extra FBOs** and **multiple passes**.

The algorithm:

1. **Bright-pass extraction**: Sample the scene texture. If a pixel's brightness exceeds a threshold, keep it; otherwise output black. Write the result to a "bright" FBO.
2. **Gaussian blur (ping-pong)**: Blur the bright-pass texture. A single-pass 2D blur is expensive, so we separate it into horizontal and vertical passes — each is a cheap 1D blur. We ping-pong between two FBOs: blur horizontally into FBO A, then blur vertically from FBO A into FBO B, and repeat for however many iterations we want.
3. **Additive blend**: Combine the original scene texture with the blurred bright texture by adding them together.

```
Scene FBO                    Bright-pass FBO
    |                              |
    v                              v
 [Full scene]    ───>     [Only bright pixels]
    |                              |
    |                     ┌────────┴────────┐
    |                     v                 v
    |               Ping FBO           Pong FBO
    |                 (H blur)          (V blur)
    |                     |                 |
    |                     └──> repeat <─────┘
    |                           |
    |                     [Blurred bloom]
    |                           |
    v                           v
 ┌──────────────────────────────────┐
 │  Final = scene + bloom * weight  │
 └──────────────────────────────────┘
```

#### Bright-Pass Shader

```glsl
// assets/shaders/pp_bloom_bright.frag
#version 460 core

in vec2 TexCoords;
out vec4 FragColour;

uniform sampler2D screenTexture;
uniform float threshold;

void main() {
    vec3 colour = texture(screenTexture, TexCoords).rgb;

    // Calculate perceived brightness
    float brightness = dot(colour, vec3(0.2126, 0.7152, 0.0722));

    // Soft knee: smooth transition around the threshold
    float soft = brightness - threshold;
    soft = clamp(soft, 0.0, threshold);
    float contribution = max(soft, brightness - threshold)
                        / max(brightness, 0.0001);

    FragColour = vec4(colour * contribution, 1.0);
}
```

#### Gaussian Blur Shader

A single shader handles both horizontal and vertical passes. The `horizontal` uniform switches the blur direction.

```glsl
// assets/shaders/pp_blur.frag
#version 460 core

in vec2 TexCoords;
out vec4 FragColour;

uniform sampler2D image;
uniform bool horizontal;

// 9-tap Gaussian weights (sigma ~= 4)
const float weights[5] = float[](
    0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216
);

void main() {
    vec2 texelSize = 1.0 / textureSize(image, 0);
    vec3 result = texture(image, TexCoords).rgb * weights[0];

    if (horizontal) {
        for (int i = 1; i < 5; i++) {
            result += texture(image, TexCoords + vec2(texelSize.x * i, 0.0)).rgb
                      * weights[i];
            result += texture(image, TexCoords - vec2(texelSize.x * i, 0.0)).rgb
                      * weights[i];
        }
    } else {
        for (int i = 1; i < 5; i++) {
            result += texture(image, TexCoords + vec2(0.0, texelSize.y * i)).rgb
                      * weights[i];
            result += texture(image, TexCoords - vec2(0.0, texelSize.y * i)).rgb
                      * weights[i];
        }
    }

    FragColour = vec4(result, 1.0);
}
```

#### Bloom Combine Shader

```glsl
// assets/shaders/pp_bloom_combine.frag
#version 460 core

in vec2 TexCoords;
out vec4 FragColour;

uniform sampler2D sceneTexture;
uniform sampler2D bloomTexture;
uniform float bloomIntensity;

void main() {
    vec3 scene = texture(sceneTexture, TexCoords).rgb;
    vec3 bloom = texture(bloomTexture, TexCoords).rgb;

    // Additive blend
    vec3 result = scene + bloom * bloomIntensity;

    // Tone mapping (Reinhard) to bring HDR values back into [0, 1]
    result = result / (result + vec3(1.0));

    FragColour = vec4(result, 1.0);
}
```

#### Bloom Ping-Pong: How It Works

The ping-pong technique uses two equally sized FBOs. In each iteration, we read from one and write to the other, then swap.

```
Iteration 1:  brightFBO ──[H blur]──> pingFBO
Iteration 2:  pingFBO   ──[V blur]──> pongFBO
Iteration 3:  pongFBO   ──[H blur]──> pingFBO
Iteration 4:  pingFBO   ──[V blur]──> pongFBO
...

After N iterations, the result is in whichever FBO was written to last.
More iterations = wider, smoother blur.
```

---

## PostProcessSettings Component

This is a pure data component — no methods, no behaviour. Systems read it to decide which effects to apply and with what parameters.

```cpp
// In src/engine/ecs/components.h (or a dedicated post_process_components.h)

struct PostProcessSettings {
    bool  bloomEnabled       = true;
    float bloomThreshold     = 0.8f;
    float bloomIntensity     = 1.0f;
    int   bloomIterations    = 10;     // Ping-pong passes (more = wider blur)

    bool  vignetteEnabled    = true;
    float vignetteStrength   = 0.5f;

    float damageFlashTimer   = 0.0f;   // > 0 means currently flashing
    float damageFlashIntensity = 0.0f; // Computed from timer, fades over time

    float contrast           = 1.0f;   // 1.0 = normal
    float saturation         = 1.0f;   // 1.0 = normal
};
```

The `PostProcessSettings` component is attached to a single "settings" entity — or the player entity, if you prefer. There should only be one active set of post-process settings at a time.

```cpp
// During scene setup
auto settingsEntity = registry.create();
registry.emplace<PostProcessSettings>(settingsEntity);
```

### Damage Flash Timer Update

A system ticks down the damage flash timer each frame. When the player takes damage, another system sets `damageFlashTimer` to (say) 0.3 seconds.

```cpp
// In src/engine/ecs/systems/post_process_update_system.h

inline void postProcessUpdateSystem(entt::registry& registry, float dt) {
    auto view = registry.view<PostProcessSettings>();
    for (auto [entity, settings] : view.each()) {
        // Tick down the damage flash
        if (settings.damageFlashTimer > 0.0f) {
            settings.damageFlashTimer -= dt;
            if (settings.damageFlashTimer < 0.0f)
                settings.damageFlashTimer = 0.0f;

            // Intensity fades linearly
            settings.damageFlashIntensity = settings.damageFlashTimer / 0.3f;
            settings.damageFlashIntensity = glm::clamp(
                settings.damageFlashIntensity, 0.0f, 1.0f);
        } else {
            settings.damageFlashIntensity = 0.0f;
        }
    }
}
```

And when the player takes damage:

```cpp
// In the damage handling system
auto& settings = registry.get<PostProcessSettings>(settingsEntity);
settings.damageFlashTimer = 0.3f;  // Flash for 0.3 seconds
```

---

## The Post-Process Render System

This is the system that orchestrates the entire post-processing pipeline. It reads the `PostProcessSettings` component and applies each enabled effect in sequence. Notice: the system itself holds no state. The FBOs, quad, and shaders are passed in as parameters.

```cpp
// In src/engine/ecs/systems/post_process_system.h
#pragma once

#include <entt/entt.hpp>
#include "engine/renderer/framebuffer.h"
#include "engine/renderer/screen_quad.h"
#include "engine/renderer/shader.h"

struct PostProcessShaders {
    Shader* brightPass  = nullptr;
    Shader* blur        = nullptr;
    Shader* bloomCombine = nullptr;
    Shader* vignette    = nullptr;
    Shader* damage      = nullptr;
    Shader* colourGrade = nullptr;
    Shader* passthrough = nullptr;
};

struct BloomFBOs {
    Framebuffer* bright = nullptr;
    Framebuffer* ping   = nullptr;
    Framebuffer* pong   = nullptr;
};

inline void postProcessSystem(
    entt::registry& registry,
    const Framebuffer& sceneFBO,
    BloomFBOs& bloomFBOs,
    const ScreenQuad& quad,
    PostProcessShaders& shaders)
{
    // Find the settings component
    auto view = registry.view<PostProcessSettings>();
    if (view.size_hint() == 0) {
        // No settings — just blit the scene texture directly
        shaders.passthrough->use();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, sceneFBO.getColourTexture());
        shaders.passthrough->setInt("screenTexture", 0);
        quad.draw();
        return;
    }

    auto& settings = view.get<PostProcessSettings>(view.front());

    // Track which texture holds the current result
    GLuint currentTexture = sceneFBO.getColourTexture();

    // ─── Bloom ──────────────────────────────────────────────────
    GLuint bloomTexture = 0;
    if (settings.bloomEnabled) {
        // Step 1: Extract bright pixels
        bloomFBOs.bright->bind();
        glClear(GL_COLOR_BUFFER_BIT);
        shaders.brightPass->use();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, currentTexture);
        shaders.brightPass->setInt("screenTexture", 0);
        shaders.brightPass->setFloat("threshold", settings.bloomThreshold);
        quad.draw();

        // Step 2: Ping-pong Gaussian blur
        bool horizontal = true;
        GLuint pingPongInput = bloomFBOs.bright->getColourTexture();

        for (int i = 0; i < settings.bloomIterations; i++) {
            Framebuffer* target = horizontal
                ? bloomFBOs.ping
                : bloomFBOs.pong;

            target->bind();
            glClear(GL_COLOR_BUFFER_BIT);
            shaders.blur->use();
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, pingPongInput);
            shaders.blur->setInt("image", 0);
            shaders.blur->setBool("horizontal", horizontal);
            quad.draw();

            pingPongInput = target->getColourTexture();
            horizontal = !horizontal;
        }

        // The last-written FBO holds the finished bloom texture
        bloomTexture = pingPongInput;
        Framebuffer::unbind();
    }

    // ─── Final composite: render to default framebuffer ─────────
    Framebuffer::unbind();
    glClear(GL_COLOR_BUFFER_BIT);

    if (settings.bloomEnabled && bloomTexture != 0) {
        // Bloom combine pass
        shaders.bloomCombine->use();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, currentTexture);
        shaders.bloomCombine->setInt("sceneTexture", 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, bloomTexture);
        shaders.bloomCombine->setInt("bloomTexture", 1);
        shaders.bloomCombine->setFloat("bloomIntensity",
                                        settings.bloomIntensity);
        quad.draw();
    } else {
        // No bloom — just passthrough
        shaders.passthrough->use();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, currentTexture);
        shaders.passthrough->setInt("screenTexture", 0);
        quad.draw();
    }

    // For the remaining effects, we would ideally chain through
    // additional FBOs. For simplicity, we apply them in the same
    // final pass by combining uniforms into a single "composite" shader.
    // Below is the alternative: a single combined final shader.
}
```

### The Combined Final Shader

In practice, you often merge the simpler effects (vignette, damage, colour grading) into a single shader to avoid extra full-screen passes. This is more efficient than chaining five separate passes.

```glsl
// assets/shaders/pp_final.frag
#version 460 core

in vec2 TexCoords;
out vec4 FragColour;

uniform sampler2D sceneTexture;
uniform sampler2D bloomTexture;

// Bloom
uniform bool  bloomEnabled;
uniform float bloomIntensity;

// Vignette
uniform bool  vignetteEnabled;
uniform float vignetteStrength;

// Damage flash
uniform float damageIntensity;

// Colour grading
uniform float contrast;
uniform float saturation;

void main() {
    vec3 colour = texture(sceneTexture, TexCoords).rgb;

    // ─── Bloom (additive blend) ─────────────────────────────────
    if (bloomEnabled) {
        vec3 bloom = texture(bloomTexture, TexCoords).rgb;
        colour += bloom * bloomIntensity;
    }

    // ─── Tone mapping (Reinhard) ────────────────────────────────
    colour = colour / (colour + vec3(1.0));

    // ─── Damage flash ───────────────────────────────────────────
    if (damageIntensity > 0.0) {
        vec3 redTint = vec3(0.8, 0.0, 0.0);
        colour = mix(colour, redTint, damageIntensity * 0.6);
        float lum = dot(colour, vec3(0.2126, 0.7152, 0.0722));
        colour = mix(colour, vec3(lum), damageIntensity * 0.3);
    }

    // ─── Contrast ───────────────────────────────────────────────
    colour = (colour - 0.5) * contrast + 0.5;

    // ─── Saturation ─────────────────────────────────────────────
    float lum = dot(colour, vec3(0.2126, 0.7152, 0.0722));
    colour = mix(vec3(lum), colour, saturation);

    // ─── Vignette ───────────────────────────────────────────────
    if (vignetteEnabled) {
        vec2 centreOffset = TexCoords - 0.5;
        float dist = length(centreOffset);
        float vignette = smoothstep(0.2, 0.9, dist * vignetteStrength);
        colour *= (1.0 - vignette);
    }

    colour = clamp(colour, 0.0, 1.0);
    FragColour = vec4(colour, 1.0);
}
```

This combined shader replaces the separate passthrough/vignette/damage/colour-grade passes with a single pass. Bloom still needs its own separate passes (bright extraction + blur), but the final composite and all simple effects happen in one draw call.

---

## Updated Render Order

Here is the complete render order for QEngine with post-processing. Compare this with the Chapter 24 render order — we have wrapped the scene in an FBO and added a post-processing pass before the HUD.

```
┌──────────────────────────────────────────────────┐
│  1. Bind scene FBO                               │
│  2. Clear colour + depth                         │
│  3. Skybox (GL_LEQUAL depth)                     │
│  4. Shadow pass (Chapter 29 — placeholder)       │
│  5. Opaque geometry (world, enemies, items)       │
│  6. Transparent geometry / particles              │
│  7. View model (weapon)                          │
│  8. Unbind scene FBO                             │
├──────────────────────────────────────────────────┤
│  9. Bloom bright-pass  (scene FBO -> bright FBO) │
│ 10. Bloom blur ping-pong (bright -> ping/pong)   │
│ 11. Final composite to default framebuffer       │
│     (scene + bloom + vignette + damage + grade)  │
├──────────────────────────────────────────────────┤
│ 12. HUD (rendered directly to default FBO)        │
│ 13. glfwSwapBuffers()                            │
└──────────────────────────────────────────────────┘
```

Steps 1-8 produce the scene texture. Steps 9-11 apply post-processing. Step 12 draws the HUD on top, unaffected by any post-processing. This separation is critical — you do not want bloom bleeding into your crosshair or health bar numbers.

---

## Initialisation: Putting It All Together

Here is how the PlayingState sets up all the framebuffers and shaders.

```cpp
// In src/game/states/playing_state.h (relevant members)

class PlayingState {
    // ...

    // Post-processing infrastructure
    Framebuffer   m_sceneFBO;       // The main off-screen render target
    Framebuffer   m_brightFBO;      // Bloom bright-pass
    Framebuffer   m_pingFBO;        // Bloom blur ping
    Framebuffer   m_pongFBO;        // Bloom blur pong
    ScreenQuad    m_screenQuad;

    // Shaders
    Shader m_ppFinalShader;
    Shader m_brightPassShader;
    Shader m_blurShader;
};
```

```cpp
// In src/game/states/playing_state.cpp — constructor

PlayingState::PlayingState(Window& window, entt::registry& registry)
    : m_window(window),
      m_registry(registry),
      m_sceneFBO(window.getWidth(), window.getHeight()),
      m_brightFBO(window.getWidth(), window.getHeight()),
      m_pingFBO(window.getWidth(), window.getHeight()),
      m_pongFBO(window.getWidth(), window.getHeight())
{
    // Load post-processing shaders
    m_ppFinalShader.load("assets/shaders/postprocess.vert",
                         "assets/shaders/pp_final.frag");
    m_brightPassShader.load("assets/shaders/postprocess.vert",
                            "assets/shaders/pp_bloom_bright.frag");
    m_blurShader.load("assets/shaders/postprocess.vert",
                      "assets/shaders/pp_blur.frag");

    // Create the post-process settings entity
    auto settingsEntity = m_registry.create();
    m_registry.emplace<PostProcessSettings>(settingsEntity);

    // ... rest of initialisation
}
```

### Handling Window Resize

When the window resizes, all framebuffers must be resized to match.

```cpp
// In the window resize callback or wherever you handle resize events
void PlayingState::onResize(int width, int height) {
    m_sceneFBO.resize(width, height);
    m_brightFBO.resize(width, height);
    m_pingFBO.resize(width, height);
    m_pongFBO.resize(width, height);
}
```

---

## Console Commands for Post-Processing

Tie the post-process settings into the developer console from Chapter 27.

```cpp
// In the command registration function

console.registerCommand("bloom", "Toggle bloom: bloom [on|off|threshold <f>]",
    [&registry, &console](const std::vector<std::string>& args) {
        auto view = registry.view<PostProcessSettings>();
        for (auto [entity, settings] : view.each()) {
            if (args.empty()) {
                settings.bloomEnabled = !settings.bloomEnabled;
                console.print(settings.bloomEnabled ? "Bloom ON" : "Bloom OFF");
            } else if (args[0] == "threshold" && args.size() > 1) {
                settings.bloomThreshold = std::stof(args[1]);
                console.print("Bloom threshold: " + args[1]);
            } else if (args[0] == "intensity" && args.size() > 1) {
                settings.bloomIntensity = std::stof(args[1]);
                console.print("Bloom intensity: " + args[1]);
            }
        }
    });

console.registerCommand("vignette", "Toggle vignette: vignette [strength <f>]",
    [&registry, &console](const std::vector<std::string>& args) {
        auto view = registry.view<PostProcessSettings>();
        for (auto [entity, settings] : view.each()) {
            if (args.empty()) {
                settings.vignetteEnabled = !settings.vignetteEnabled;
                console.print(settings.vignetteEnabled
                              ? "Vignette ON" : "Vignette OFF");
            } else if (args[0] == "strength" && args.size() > 1) {
                settings.vignetteStrength = std::stof(args[1]);
                console.print("Vignette strength: " + args[1]);
            }
        }
    });

console.registerCommand("contrast", "Set contrast: contrast <f>",
    [&registry, &console](const std::vector<std::string>& args) {
        if (args.empty()) { console.print("Usage: contrast <0.5-2.0>"); return; }
        auto view = registry.view<PostProcessSettings>();
        for (auto [entity, settings] : view.each()) {
            settings.contrast = std::clamp(std::stof(args[0]), 0.1f, 3.0f);
            console.print("Contrast: " + args[0]);
        }
    });
```

---

## C++ Concept: RAII for GPU Resources

The `Framebuffer` class in this chapter is a textbook example of **RAII** — Resource Acquisition Is Initialisation. It is the single most important C++ idiom for managing resources, and it applies perfectly to OpenGL objects.

### The Problem Without RAII

```cpp
// Manual resource management — fragile and error-prone
GLuint fbo, texture, rbo;
glGenFramebuffers(1, &fbo);
glGenTextures(1, &texture);
glGenRenderbuffers(1, &rbo);
// ... set them up ...

// Somewhere later, you must remember to clean up:
glDeleteTextures(1, &texture);
glDeleteRenderbuffers(1, &rbo);
glDeleteFramebuffers(1, &fbo);
// What if an exception is thrown before cleanup?
// What if you forget one of these calls?
// What if you clean up in the wrong order?
```

This style requires you to remember every cleanup call, handle every error path, and never forget a `glDelete`. In a large codebase, this is a guaranteed source of resource leaks.

### The RAII Solution

```cpp
// RAII: constructor acquires, destructor releases
{
    Framebuffer fbo(1920, 1080);  // Constructor: creates FBO, texture, RBO
    fbo.bind();
    // ... render into it ...
}  // Destructor runs here: deletes FBO, texture, RBO automatically
```

The rules are simple:
1. **Constructor** acquires the resource (allocates GPU objects)
2. **Destructor** releases the resource (deletes GPU objects)
3. The resource's lifetime is tied to the C++ object's lifetime

If an exception is thrown, if you return early, if you break out of a loop — the destructor still runs. You cannot leak.

### Deleted Copy, Enabled Move

GPU resources cannot be duplicated — there is no `glCopyFramebuffer`. If two `Framebuffer` objects held the same FBO ID, both destructors would try to delete it, causing a double-free crash. So we **delete** the copy constructor and copy assignment operator:

```cpp
// Prevent copying — two objects must not share the same GPU handle
Framebuffer(const Framebuffer&) = delete;
Framebuffer& operator=(const Framebuffer&) = delete;
```

But we still want to be able to transfer ownership — for example, returning a `Framebuffer` from a factory function, or storing them in a `std::vector`. This is what **move semantics** are for:

```cpp
// Move constructor: steal resources from the source
Framebuffer::Framebuffer(Framebuffer&& other) noexcept
    : m_fbo(other.m_fbo),
      m_colourTexture(other.m_colourTexture),
      m_depthStencilRBO(other.m_depthStencilRBO),
      m_width(other.m_width),
      m_height(other.m_height)
{
    // Zero out the source so its destructor does not delete our resources
    other.m_fbo             = 0;
    other.m_colourTexture   = 0;
    other.m_depthStencilRBO = 0;
}
```

After a move, the source object is left in a "zeroed" state. Its destructor will see all handles are 0 and skip the `glDelete` calls. The destination object now owns the GPU resources exclusively.

```
Before move:
  source:  { fbo=3, texture=7, rbo=5 }      (owns GPU resources)
  dest:    (does not exist yet)

After move:
  source:  { fbo=0, texture=0, rbo=0 }      (empty, safe to destroy)
  dest:    { fbo=3, texture=7, rbo=5 }      (now owns GPU resources)
```

This pattern applies to every GPU resource wrapper you write: textures, shaders, VAOs, VBOs, cubemaps. The `Skybox` class from Chapter 24 follows the exact same pattern. If you adopt this idiom consistently, you will never leak a GPU resource.

### The Rule of Five

When you define a destructor, copy constructor, copy assignment, move constructor, or move assignment — you should define all five. This is the **Rule of Five**. Our Framebuffer class does exactly this:

| Special Member | Framebuffer |
|---------------|-------------|
| Destructor | Calls `cleanup()` — deletes FBO, texture, RBO |
| Copy constructor | `= delete` |
| Copy assignment | `= delete` |
| Move constructor | Steals handles, zeros source |
| Move assignment | Cleans up self, steals handles, zeros source |

---

## What's Next

In **Chapter 29**, we will implement **shadow mapping** — rendering the scene from the light's perspective into a depth-only FBO, then sampling that depth texture during the main render pass to determine which fragments are in shadow. The framebuffer skills you learned in this chapter are the foundation: shadow mapping is just another render-to-texture technique, but writing depth instead of colour.
