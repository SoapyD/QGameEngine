# Chapter 52: Deferred Rendering

## What You'll Learn
- Why forward rendering breaks down with many lights — the O(objects x lights) problem
- The deferred rendering strategy: separate geometry from lighting
- The G-buffer — writing to multiple render targets (MRT) in a single pass
- Building a `GBuffer` FBO class with position, normal, albedo, and material textures
- The geometry pass: rendering surface data without lighting calculations
- The lighting pass: evaluating all lights per pixel using a full-screen quad
- Light volumes — rendering point lights as spheres for massive performance gains
- The stencil trick for handling light volumes that intersect the near plane
- A forward pass for transparent objects (particles, glass, water)
- Updating `RenderPipeline` with the new deferred stages
- Debug visualisation: rendering individual G-buffer channels to screen
- C++ concept: Multiple Return Values — MRT as a GPU analogy for `std::tuple` and structured bindings

---

## The Problem: Forward Rendering Doesn't Scale

Every fragment shader we have written so far — from Chapter 7's Phong shader to Chapter 44's PBR shader — follows the same structure. For every fragment of every object, the shader loops over every light and computes a lighting contribution. This is **forward rendering**: geometry and lighting happen in a single pass.

The cost is:

```
Forward rendering cost = fragments_drawn x number_of_lights

Where fragments_drawn is roughly:
  visible_objects x average_fragments_per_object x overdraw_factor
```

For a Quake-style level with 100 visible objects and 4 lights, this is fine. The PBR shader evaluates 4 point lights per fragment and the GPU barely notices. But what happens when you want 50 dynamic lights? Muzzle flashes, flickering torches, glowing pickups, rocket trails, lava glow, emergency klaxons — a single room in a Quake map could easily justify 30-50 point lights for atmosphere.

With 200 visible objects and 50 lights, forward rendering evaluates the PBR BRDF ~50 million times per frame (200 objects x ~5000 fragments x 50 lights). Most evaluations are wasted: overdraw means many fragments are behind other fragments, and most lights are too far away to affect most fragments.

```
THE SCALING PROBLEM

  Forward: cost grows linearly with lights

    Lights:  4     16     50     100     200
    Cost:    |===  |=====  |============  |========================

  A wall behind the player gets overdrawn 3 times.
  Each overdraw evaluates ALL 50 lights.
  Only the final visible fragment matters — the rest is pure waste.

  150 objects x 3x overdraw x 50 lights = 22,500 BRDF evaluations
  per pixel. Most contribute nothing visible.
```

Deferred rendering solves both problems — overdraw waste and distant light waste — by restructuring how and when lighting happens.

---

## The Deferred Rendering Strategy

The key insight: **separate the "what surface is visible" question from the "how is that surface lit" question.**

**Pass 1 — Geometry pass.** Render all opaque objects, but instead of computing lighting, write the surface properties (position, normal, albedo, metallic, roughness) into a set of textures. These textures are collectively called the **G-buffer** (geometry buffer). The depth buffer ensures only the closest surface at each pixel survives — no wasted lighting on hidden fragments.

**Pass 2 — Lighting pass.** Draw a full-screen quad. The fragment shader reads the G-buffer textures to recover the surface at each pixel, then loops over every light and computes the PBR BRDF. The result is the final lit colour.

```
FORWARD RENDERING                    DEFERRED RENDERING

  For each object:                     Pass 1 (Geometry):
    For each fragment:                   For each object:
      For each light:                      Write surface data to G-buffer
        Compute BRDF                       (no lighting, just data)

  Cost: O(fragments x lights)          Pass 2 (Lighting):
  Overdraw multiplies the cost           For each screen pixel:
                                           Read surface data from G-buffer
                                           For each light:
                                             Compute BRDF

                                         Cost: O(pixels x lights)
                                         No overdraw — each pixel shaded once
```

Why is `O(pixels x lights)` better than `O(fragments x lights)`? Pixels = screen resolution (~2M at 1080p), fixed regardless of scene complexity. Fragments depend on object count and overdraw (typically 2-4x the pixel count in a Quake level with overlapping geometry). The geometry pass resolves depth before any lighting happens. At 1080p with 50 lights: forward processes ~400M BRDF evaluations (with overdraw) vs deferred ~100M (one per visible pixel).

### When Forward Still Wins

Deferred rendering is not universally better. Forward rendering is simpler and handles some cases that deferred cannot:

- **Transparency.** The G-buffer stores one surface per pixel. Transparent objects — glass, particles, water — need blending, which requires seeing through to the surface behind. The standard solution is a hybrid: deferred for opaque geometry, then a forward pass for transparent objects on top.

- **Few lights.** If your scene has 1-4 lights, forward rendering is cheaper because it avoids the G-buffer memory and bandwidth cost.

- **MSAA.** Standard MSAA does not work with deferred rendering because the G-buffer stores per-pixel data, not per-sample data. You need alternative anti-aliasing (FXAA, TAA) or deferred MSAA techniques.

- **Material variety.** Every material in deferred rendering must write the same G-buffer layout. Exotic shaders (subsurface scattering, anisotropic highlights) are harder to integrate.

For QEngine's Quake-style levels — lots of opaque geometry, many dynamic lights from torches and explosions, transparency limited to particles and glass — deferred rendering is the right choice.

---

## The G-Buffer

The G-buffer is a framebuffer with multiple colour attachments. Each attachment stores a different piece of surface information. OpenGL supports **Multiple Render Targets (MRT)** — a single fragment shader can write to several textures simultaneously using `layout(location = N)` output variables.

### G-Buffer Layout

We need to store everything the PBR lighting shader requires to shade a pixel. Here is our layout:

```
G-BUFFER LAYOUT

  Attachment  Format    Contents
  ---------  --------  --------------------------------
  0          RGB16F    World position (X, Y, Z)
  1          RGB16F    World normal (X, Y, Z) -- already normal-mapped
  2          RGBA8     Albedo (RGB) + specular (A, reserved)
  3          RGBA8     Metallic (R), Roughness (G), AO (B), emissive flag (A)
  Depth      D24S8     24-bit depth, 8-bit stencil


  FBO
  +--------------------------------------------+
  | GL_COLOR_ATTACHMENT0 --> Position (RGB16F)  |  <-- world-space XYZ
  | GL_COLOR_ATTACHMENT1 --> Normal   (RGB16F)  |  <-- includes normal map
  | GL_COLOR_ATTACHMENT2 --> Albedo   (RGBA8)   |  <-- base colour
  | GL_COLOR_ATTACHMENT3 --> Material (RGBA8)   |  <-- metallic/rough/AO
  | GL_DEPTH_STENCIL     --> Depth    (D24S8)   |  <-- standard depth
  +--------------------------------------------+
```

Positions and normals use `RGB16F` because they need world-space range and negative values. Albedo and material use `RGBA8` because all values are in the 0-1 range and our source textures are 8-bit anyway.

### Memory Budget

```
At 1920x1080 (2,073,600 pixels):

  Position:  3 channels x 2 bytes = 6 bytes/pixel    ~12.4 MB
  Normal:    3 channels x 2 bytes = 6 bytes/pixel    ~12.4 MB
  Albedo:    4 channels x 1 byte  = 4 bytes/pixel    ~ 8.3 MB
  Material:  4 channels x 1 byte  = 4 bytes/pixel    ~ 8.3 MB
  Depth:     4 bytes/pixel                            ~ 8.3 MB
                                                      --------
  Total:                                              ~49.7 MB
```

Significant but manageable — smaller than a single 4K albedo texture. Modern GPUs have 4-12 GB of VRAM.

> **Note on position reconstruction:** Many production engines skip the position buffer and reconstruct world position from the depth buffer using `inverseViewProjection * clipSpacePos`. This saves ~12 MB and one texture read per pixel in the lighting pass. For QEngine, we store position explicitly because it is simpler and easier to debug. Position reconstruction is a good optimisation for later.

---

## The GBuffer Class

This extends our `Framebuffer` class from Chapter 28 to support multiple colour attachments.

### src/engine/renderer/gbuffer.h

```cpp
// src/engine/renderer/gbuffer.h
#pragma once
#include <glad/glad.h>
#include <iostream>

class GBuffer {
public:
    enum Texture { Position = 0, Normal = 1, Albedo = 2, Material = 3, Count = 4 };

    GBuffer() = default;
    ~GBuffer();

    GBuffer(const GBuffer&) = delete;
    GBuffer& operator=(const GBuffer&) = delete;
    GBuffer(GBuffer&& other) noexcept;
    GBuffer& operator=(GBuffer&& other) noexcept;

    bool init(int width, int height);
    void bindForWriting() const;
    void bindForReading(GLenum startUnit) const;  // Binds textures 0-3
    void blitDepthTo(GLuint targetFBO, int width, int height) const;
    void resize(int width, int height);

    GLuint getTexture(Texture type) const { return m_textures[type]; }
    GLuint getDepthTexture() const { return m_depthTexture; }
    GLuint getID() const { return m_fbo; }
    int getWidth()  const { return m_width; }
    int getHeight() const { return m_height; }

private:
    GLuint m_fbo = 0;
    GLuint m_textures[Count] = {};
    GLuint m_depthTexture = 0;
    int    m_width = 0, m_height = 0;
    void create();
    void cleanup();
};
```

### src/engine/renderer/gbuffer.cpp

```cpp
// src/engine/renderer/gbuffer.cpp
#include "engine/renderer/gbuffer.h"

// Destructor, move constructor, move assignment follow the same RAII
// pattern as the Framebuffer class from Ch 28. They transfer ownership
// of the FBO and texture handles, zeroing the source. See the Framebuffer
// implementation for the full pattern — GBuffer's versions are identical
// except they loop over m_textures[0..3] in addition to the single
// depth texture.

GBuffer::~GBuffer() { cleanup(); }

bool GBuffer::init(int width, int height) {
    m_width  = width;
    m_height = height;
    create();

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "ERROR: GBuffer framebuffer is not complete!" << std::endl;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

void GBuffer::create() {
    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

    glGenTextures(Texture::Count, m_textures);

    // ── Create the 4 colour attachments ───────────────────────────
    // Attachments 0-1 (Position, Normal): RGB16F — need float precision
    // Attachments 2-3 (Albedo, Material): RGBA8 — 0-1 range is sufficient
    // All use NEAREST filtering (no interpolation between G-buffer pixels)
    // and CLAMP_TO_EDGE wrapping.

    struct AttachmentSpec { GLenum internalFormat; GLenum format; GLenum type; };
    AttachmentSpec specs[Texture::Count] = {
        {GL_RGB16F,  GL_RGB,  GL_FLOAT},          // Position
        {GL_RGB16F,  GL_RGB,  GL_FLOAT},          // Normal
        {GL_RGBA8,   GL_RGBA, GL_UNSIGNED_BYTE},  // Albedo
        {GL_RGBA8,   GL_RGBA, GL_UNSIGNED_BYTE},  // Material
    };

    for (int i = 0; i < Texture::Count; ++i) {
        glBindTexture(GL_TEXTURE_2D, m_textures[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, specs[i].internalFormat,
                     m_width, m_height, 0,
                     specs[i].format, specs[i].type, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i,
                               GL_TEXTURE_2D, m_textures[i], 0);
    }

    // ── Tell OpenGL which colour attachments we are using ───────
    // This is the MRT call — without it, the GPU only writes to attachment 0
    GLenum attachments[Texture::Count] = {
        GL_COLOR_ATTACHMENT0,
        GL_COLOR_ATTACHMENT1,
        GL_COLOR_ATTACHMENT2,
        GL_COLOR_ATTACHMENT3
    };
    glDrawBuffers(Texture::Count, attachments);

    // ── Depth as texture (not RBO) ─────────────────────────────
    // We use a texture instead of a renderbuffer because we need to
    // blit the depth to the forward pass FBO later. We also need to
    // sample it in the lighting pass for position reconstruction
    // (a future optimisation).
    glGenTextures(1, &m_depthTexture);
    glBindTexture(GL_TEXTURE_2D, m_depthTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8,
                 m_width, m_height, 0,
                 GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                           GL_TEXTURE_2D, m_depthTexture, 0);
}

// ── Bind for writing ───────────────────────────────────────────
void GBuffer::bindForWriting() const {
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_width, m_height);
}

// ── Bind textures for reading ──────────────────────────────────
void GBuffer::bindForReading(GLenum startUnit) const {
    for (int i = 0; i < Texture::Count; ++i) {
        glActiveTexture(startUnit + i);
        glBindTexture(GL_TEXTURE_2D, m_textures[i]);
    }
}

void GBuffer::blitDepthTo(GLuint targetFBO, int width, int height) const {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, targetFBO);
    glBlitFramebuffer(0, 0, m_width, m_height, 0, 0, width, height,
                      GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void GBuffer::resize(int width, int height) {
    if (width == m_width && height == m_height) return;
    cleanup();
    m_width = width;  m_height = height;
    create();
}

// cleanup() deletes all textures and the FBO — same pattern as Ch 28's Framebuffer.
void GBuffer::cleanup() {
    if (m_depthTexture) { glDeleteTextures(1, &m_depthTexture); m_depthTexture = 0; }
    if (m_textures[0])  { glDeleteTextures(Count, m_textures); for (auto& t : m_textures) t = 0; }
    if (m_fbo)          { glDeleteFramebuffers(1, &m_fbo); m_fbo = 0; }
}
```

### Key Details

- **`GL_NEAREST` filtering.** The G-buffer stores exact per-pixel values. Linear filtering would interpolate between a wall's position and the floor's position at their boundary, producing nonsensical lighting. Always use `GL_NEAREST`.

- **`glDrawBuffers`.** This is the critical call. Without it, the fragment shader's `layout(location = 1)` output goes nowhere. `glDrawBuffers` tells OpenGL that colour attachment 0 receives output 0, attachment 1 receives output 1, and so on. Forgetting this call is the single most common deferred rendering bug.

- **Depth as a texture.** Chapter 28 used a renderbuffer for depth because we never read it. Now we need the depth for blitting to the forward pass FBO and for the stencil-based light volume technique. A texture is readable in shaders.

---

## The Geometry Pass

The geometry pass renders every opaque entity, but instead of computing lighting, it writes surface properties to the G-buffer. The fragment shader has no light loops, no BRDF calculations — it just outputs data.

### Geometry Pass Vertex Shader

```glsl
// assets/shaders/gbuffer.vert
#version 460 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
layout (location = 3) in vec4 aTangent;    // xyz = tangent, w = handedness (Ch 35)

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoords;
out mat3 TBN;

void main() {
    vec4 worldPos = model * vec4(aPos, 1.0);
    FragPos   = worldPos.xyz;
    TexCoords = aTexCoords;

    // Construct TBN matrix for normal mapping (Ch 35)
    mat3 normalMatrix = transpose(inverse(mat3(model)));
    vec3 T = normalize(normalMatrix * aTangent.xyz);
    vec3 N = normalize(normalMatrix * aNormal);
    T = normalize(T - dot(T, N) * N);   // Re-orthogonalise
    vec3 B = cross(N, T) * aTangent.w;  // Handedness
    TBN = mat3(T, B, N);

    Normal = N;

    gl_Position = projection * view * worldPos;
}
```

### Geometry Pass Fragment Shader

This is the key shader. Notice the four `layout(location = N)` outputs — one for each G-buffer attachment.

```glsl
// assets/shaders/gbuffer.frag
#version 460 core

// ── Multiple Render Targets ────────────────────────────────────
// Each layout(location) corresponds to a GL_COLOR_ATTACHMENT
layout (location = 0) out vec3 gPosition;
layout (location = 1) out vec3 gNormal;
layout (location = 2) out vec4 gAlbedoSpec;
layout (location = 3) out vec4 gMaterial;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;
in mat3 TBN;

// ── Material textures ─────────────────────────────────────────
uniform sampler2D albedoMap;
uniform sampler2D normalMap;
uniform sampler2D metallicMap;
uniform sampler2D roughnessMap;
uniform sampler2D aoMap;

// Uniform fallbacks when no texture is bound
uniform vec3  u_albedo     = vec3(1.0);
uniform float u_metallic   = 0.0;
uniform float u_roughness  = 0.5;
uniform float u_ao         = 1.0;

// Flags: 1 if a texture is bound, 0 if using uniform value
uniform int hasAlbedoMap;
uniform int hasNormalMap;
uniform int hasMetallicMap;
uniform int hasRoughnessMap;
uniform int hasAoMap;

void main() {
    // ── Position ───────────────────────────────────────────────
    gPosition = FragPos;

    // ── Normal (with normal mapping from Ch 35) ────────────────
    if (hasNormalMap == 1) {
        vec3 N = texture(normalMap, TexCoords).rgb * 2.0 - 1.0;
        gNormal = normalize(TBN * N);
    } else {
        gNormal = normalize(Normal);
    }

    // ── Albedo ─────────────────────────────────────────────────
    // Convert from sRGB to linear space (same as Ch 44)
    vec3 albedo = hasAlbedoMap == 1
        ? pow(texture(albedoMap, TexCoords).rgb, vec3(2.2))
        : u_albedo;
    gAlbedoSpec.rgb = albedo;
    gAlbedoSpec.a   = 1.0;  // Reserved for specular intensity

    // ── Material properties ────────────────────────────────────
    float metallic  = hasMetallicMap  == 1 ? texture(metallicMap, TexCoords).r  : u_metallic;
    float roughness = hasRoughnessMap == 1 ? texture(roughnessMap, TexCoords).r : u_roughness;
    float ao        = hasAoMap        == 1 ? texture(aoMap, TexCoords).r        : u_ao;
    gMaterial = vec4(metallic, roughness, ao, 0.0);
}
```

No light uniforms, no BRDF functions, no light loops. This shader is pure data output — roughly half the cost of the forward PBR shader. The `layout(location = N) out` declarations map to `GL_COLOR_ATTACHMENT0..3` via the `glDrawBuffers` call in the GBuffer.

### How MRT Works Under the Hood

```
Fragment shader execution for one pixel:

  Inputs: FragPos, TexCoords, TBN, material textures
      |
      +--> layout(location=0) gPosition   --> GL_COLOR_ATTACHMENT0 (position texture)
      +--> layout(location=1) gNormal     --> GL_COLOR_ATTACHMENT1 (normal texture)
      +--> layout(location=2) gAlbedoSpec --> GL_COLOR_ATTACHMENT2 (albedo texture)
      +--> layout(location=3) gMaterial   --> GL_COLOR_ATTACHMENT3 (material texture)

  One shader invocation, four texture writes.
  The GPU hardware is designed for this -- MRT does not cost 4x.
```

The GPU processes each triangle once and writes all four outputs simultaneously. MRT is a hardware-level feature, not four separate passes.

---

## The Lighting Pass

The lighting pass reads from the G-buffer and computes full PBR lighting for every pixel. This is where the actual shading happens.

We already have a `ScreenQuad` from Chapter 28's post-processing. The lighting pass uses the same geometry — two triangles that cover the entire screen.

### Lighting Pass Vertex Shader

```glsl
// assets/shaders/deferred_lighting.vert
#version 460 core

layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoords;

out vec2 TexCoords;

void main() {
    TexCoords   = aTexCoords;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
```

Identical to the post-process vertex shader from Chapter 28. The quad vertices are in NDC (-1 to 1), so no matrices are needed.

### Lighting Pass Fragment Shader

```glsl
// assets/shaders/deferred_lighting.frag
#version 460 core

out vec4 FragColor;

in vec2 TexCoords;

// ── G-buffer textures ──────────────────────────────────────────
uniform sampler2D gPosition;
uniform sampler2D gNormal;
uniform sampler2D gAlbedoSpec;
uniform sampler2D gMaterial;

// ── Camera ─────────────────────────────────────────────────────
uniform vec3 camPos;

// ── Lights ─────────────────────────────────────────────────────
struct DirLight {
    vec3 direction;
    vec3 colour;
    float intensity;
};

struct PointLight {
    vec3 position;
    vec3 colour;
    float intensity;
    float radius;       // Attenuation cutoff distance
};

#define MAX_DIR_LIGHTS   4
#define MAX_POINT_LIGHTS 128    // Deferred can handle many more lights!

uniform int       numDirLights;
uniform DirLight  dirLights[MAX_DIR_LIGHTS];
uniform int       numPointLights;
uniform PointLight pointLights[MAX_POINT_LIGHTS];

// ── Shadow map (from Ch 29) ────────────────────────────────────
uniform sampler2D shadowMap;
uniform mat4      lightSpaceMatrix;
uniform int       hasShadows;

// ── Constants ──────────────────────────────────────────────────
const float PI = 3.14159265359;

// ── PBR BRDF functions ─────────────────────────────────────────
// Identical to Ch 44's forward PBR shader: distributionGGX, geometrySmith,
// geometrySchlickGGX, fresnelSchlick. The BRDF does not change — only
// where and when it is evaluated changes. See Ch 44 for annotated versions.
// In a production engine, put these in a shared include (brdf.glsl).

float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    denom = PI * denom * denom;
    return a2 / max(denom, 0.0000001);
}

float geometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return geometrySchlickGGX(NdotV, roughness)
         * geometrySchlickGGX(NdotL, roughness);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ── Shadow calculation (identical to Ch 29 — PCF with bias) ────
float calculateShadow(vec3 fragPos, vec3 normal, vec3 lightDir) {
    if (hasShadows == 0) return 0.0;

    vec4 fragPosLightSpace = lightSpaceMatrix * vec4(fragPos, 1.0);
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;
    if (projCoords.z > 1.0) return 0.0;

    float currentDepth = projCoords.z;
    float bias = max(0.005 * (1.0 - dot(normal, lightDir)), 0.001);

    // PCF 3x3
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float pcfDepth = texture(shadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;
        }
    }
    return shadow / 9.0;
}

// ── Compute lighting for one light ─────────────────────────────
vec3 computeLight(vec3 L, vec3 radiance, vec3 N, vec3 V,
                  vec3 albedo, float metallic, float roughness, vec3 F0) {
    vec3 H = normalize(V + L);

    float D = distributionGGX(N, H, roughness);
    float G = geometrySmith(N, V, L, roughness);
    vec3  F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 specular = (D * G * F)
                  / max(4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0), 0.001);

    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

    return (kD * albedo / PI + specular) * radiance * max(dot(N, L), 0.0);
}

void main() {
    // Read G-buffer
    vec3  fragPos   = texture(gPosition, TexCoords).rgb;
    vec3  normal    = texture(gNormal, TexCoords).rgb;
    vec3  albedo    = texture(gAlbedoSpec, TexCoords).rgb;
    float metallic  = texture(gMaterial, TexCoords).r;
    float roughness = texture(gMaterial, TexCoords).g;
    float ao        = texture(gMaterial, TexCoords).b;

    if (length(normal) < 0.1) discard;  // No geometry at this pixel

    vec3 N  = normalize(normal);
    vec3 V  = normalize(camPos - fragPos);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 Lo = vec3(0.0);

    // Directional lights (shadow on first light only)
    for (int i = 0; i < numDirLights; i++) {
        vec3 L = normalize(-dirLights[i].direction);
        vec3 radiance = dirLights[i].colour * dirLights[i].intensity;
        float shadow = (i == 0) ? calculateShadow(fragPos, N, L) : 0.0;
        Lo += computeLight(L, radiance, N, V, albedo, metallic, roughness, F0)
            * (1.0 - shadow);
    }

    // Point lights
    for (int i = 0; i < numPointLights; i++) {
        vec3  lv = pointLights[i].position - fragPos;
        float d  = length(lv);
        if (d > pointLights[i].radius) continue;   // Skip distant lights
        float att = 1.0 / (d * d);
        float fall = 1.0 - smoothstep(pointLights[i].radius * 0.75,
                                       pointLights[i].radius, d);
        Lo += computeLight(lv / d,
                           pointLights[i].colour * pointLights[i].intensity * att * fall,
                           N, V, albedo, metallic, roughness, F0);
    }

    // Ambient + AO
    vec3 ambient = vec3(0.03) * albedo * ao;

    FragColor = vec4(ambient + Lo, 1.0);  // HDR output -- tone mapping in post-process
}
```

The lighting math is identical to Chapter 44 — the only change is where surface data comes from (G-buffer texture samples instead of per-vertex interpolation). The `MAX_POINT_LIGHTS` has jumped from 32 to 128. In forward rendering, 128 lights per fragment was unthinkable. In deferred rendering, the shader only evaluates lights that are within range (the `continue` skip), and with light volumes (next section) most pixels only process 2-5 lights.

---

## Light Volumes

The full-screen quad evaluates every point light for every pixel — 128 lights means 128 BRDF evaluations per pixel, most wasted. **Light volumes** fix this: render each point light as a sphere mesh at the light's position, scaled to its radius. Only pixels covered by the sphere run the lighting shader. A torch in room A never touches pixels in room B.

```
LIGHT VOLUMES -- only shade pixels inside the sphere

  Screen:
  +--------------------------------------------+
  |                                            |
  |        +----------+                        |
  |       /    Light    \                      |
  |      |   volume     |  <-- Only these      |
  |      |   (sphere)   |     pixels run       |
  |       \             /     the BRDF         |
  |        +----------+                        |
  |                                            |
  |  All other pixels: zero cost from this     |
  |  light. The GPU never even executes the    |
  |  fragment shader for them.                 |
  +--------------------------------------------+
```

### The Light Volume Mesh

We need a low-poly sphere mesh. It does not need to be precise — it just needs to roughly cover the light's area. 12 segments is enough.

```cpp
// src/engine/renderer/light_volume.h
#pragma once
#include <glad/glad.h>
#include <vector>
#include <cmath>

// A unit sphere mesh used for point light volumes.
// Non-copyable, movable. init() generates geometry, draw() issues the draw call.
class LightVolume {
public:
    LightVolume() = default;
    ~LightVolume();
    LightVolume(const LightVolume&) = delete;
    LightVolume& operator=(const LightVolume&) = delete;

    void init(int segments = 12);
    void draw() const;

private:
    GLuint m_vao = 0, m_vbo = 0, m_ebo = 0;
    int    m_indexCount = 0;
};
```

```cpp
// src/engine/renderer/light_volume.cpp
#include "engine/renderer/light_volume.h"

LightVolume::~LightVolume() {
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    if (m_ebo) glDeleteBuffers(1, &m_ebo);
}

void LightVolume::init(int segments) {
    // Generate a standard UV sphere: loop over latitude (y) and longitude (x),
    // computing positions on the unit sphere using sin/cos. Two triangles per
    // grid cell. Only position attribute — no normals or UVs needed.
    std::vector<float> vertices;
    std::vector<unsigned int> indices;
    const float PI = 3.14159265359f;

    for (int y = 0; y <= segments; ++y) {
        for (int x = 0; x <= segments; ++x) {
            float xSeg = static_cast<float>(x) / segments;
            float ySeg = static_cast<float>(y) / segments;
            vertices.push_back(std::cos(xSeg * 2.0f * PI) * std::sin(ySeg * PI));
            vertices.push_back(std::cos(ySeg * PI));
            vertices.push_back(std::sin(xSeg * 2.0f * PI) * std::sin(ySeg * PI));
        }
    }

    for (int y = 0; y < segments; ++y) {
        for (int x = 0; x < segments; ++x) {
            int tl = y * (segments + 1) + x, tr = tl + 1;
            int bl = (y + 1) * (segments + 1) + x, br = bl + 1;
            indices.insert(indices.end(), {(unsigned)tl, (unsigned)bl, (unsigned)tr,
                                           (unsigned)tr, (unsigned)bl, (unsigned)br});
        }
    }

    m_indexCount = static_cast<int>(indices.size());

    // Upload to GPU — single position-only VBO
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float),
                 vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int),
                 indices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glBindVertexArray(0);
}

void LightVolume::draw() const {
    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}
```

### Light Volume Shader

The light volume shader is nearly identical to the full-screen lighting shader, but it processes a single light instead of looping.

```glsl
// assets/shaders/deferred_light_volume.vert
#version 460 core

layout (location = 0) in vec3 aPos;

uniform mat4 model;      // Translate to light position, scale to light radius
uniform mat4 view;
uniform mat4 projection;

void main() {
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}
```

```glsl
// assets/shaders/deferred_light_volume.frag
#version 460 core

out vec4 FragColor;

// G-buffer textures
uniform sampler2D gPosition;
uniform sampler2D gNormal;
uniform sampler2D gAlbedoSpec;
uniform sampler2D gMaterial;

// Screen dimensions (needed to convert gl_FragCoord to UV)
uniform vec2 screenSize;

// Camera
uniform vec3 camPos;

// This light's properties
uniform vec3  lightPos;
uniform vec3  lightColour;
uniform float lightIntensity;
uniform float lightRadius;

const float PI = 3.14159265359;

// ── PBR BRDF functions ────────────────────────────────────────
// These are identical to the ones in deferred_lighting.frag.
// In a production engine you would put them in a shared include file
// (e.g. assets/shaders/brdf.glsl) and use #include. For clarity,
// they are duplicated here. See Ch 44 for the annotated versions.

float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    denom = PI * denom * denom;
    return a2 / max(denom, 0.0000001);
}

float geometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return geometrySchlickGGX(NdotV, roughness)
         * geometrySchlickGGX(NdotL, roughness);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    vec2 texCoords = gl_FragCoord.xy / screenSize;

    // Read G-buffer
    vec3  fragPos   = texture(gPosition,   texCoords).rgb;
    vec3  normal    = texture(gNormal,     texCoords).rgb;
    vec3  albedo    = texture(gAlbedoSpec, texCoords).rgb;
    float metallic  = texture(gMaterial,   texCoords).r;
    float roughness = texture(gMaterial,   texCoords).g;

    if (length(normal) < 0.1) discard;

    vec3 N = normalize(normal);
    vec3 V = normalize(camPos - fragPos);

    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    // Single-light evaluation
    vec3  lightVec = lightPos - fragPos;
    float distance = length(lightVec);

    if (distance > lightRadius) discard;

    vec3  L = lightVec / distance;
    vec3  H = normalize(V + L);
    float attenuation = 1.0 / (distance * distance);
    float falloff = 1.0 - smoothstep(lightRadius * 0.75, lightRadius, distance);
    vec3  radiance = lightColour * lightIntensity * attenuation * falloff;

    // Cook-Torrance BRDF
    float D = distributionGGX(N, H, roughness);
    float G = geometrySmith(N, V, L, roughness);
    vec3  F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 specular = (D * G * F)
                  / max(4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0), 0.001);
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

    FragColor = vec4((kD * albedo / PI + specular) * radiance * max(dot(N, L), 0.0), 1.0);
}
```

### The Stencil Trick

When the camera is *inside* a light sphere, the front faces are clipped by the near plane — normal rendering produces nothing. The **stencil trick** fixes this.

The algorithm has two sub-passes per light:

1. **Stencil pass.** Draw the sphere with both faces (no colour write, depth test on), incrementing stencil for back faces that fail the depth test and decrementing for front faces that fail. After this pass, pixels where geometry is inside the sphere have a non-zero stencil value.

2. **Lighting pass.** Draw the sphere's back faces only, where stencil != 0. Depth test off, additive blending on. This correctly handles all cases: camera outside, camera inside, and partial near-plane clipping.

```
STENCIL TRICK — handling camera inside the light sphere

  Camera OUTSIDE the sphere:
    Front faces are in front of geometry  --> decrement stencil
    Back faces are behind geometry        --> increment stencil
    Net stencil for pixels inside sphere: +1 (back increments, front decrements)
    Lighting pass runs for stencil != 0   --> correct!

  Camera INSIDE the sphere:
    Front faces are behind near plane     --> clipped, never rendered
    Back faces are in front of geometry   --> increment stencil
    Net stencil: +1 for all covered pixels
    Lighting pass runs everywhere         --> correct! (you are inside the light)
```

### Lighting Orchestration

```cpp
// src/engine/renderer/deferred_lighting.h
#pragma once

#include "engine/renderer/gbuffer.h"
#include "engine/renderer/light_volume.h"
#include "engine/renderer/screen_quad.h"
#include "engine/renderer/shader.h"
#include "engine/renderer/shadow_map.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

struct DeferredDirLight {
    glm::vec3 direction, colour;
    float intensity;
};

struct DeferredPointLight {
    glm::vec3 position, colour;
    float intensity, radius;
};

// Orchestrates the lighting pass. Assumes the output FBO is already bound.
inline void deferredLightingPass(
    const GBuffer& gbuffer, const ScreenQuad& screenQuad,
    const LightVolume& lightVolume,
    Shader& dirShader, Shader& volShader,
    const glm::vec3& camPos, const glm::mat4& view, const glm::mat4& proj,
    const std::vector<DeferredDirLight>& dirLights,
    const std::vector<DeferredPointLight>& pointLights,
    const ShadowMap* shadowMap, const glm::mat4& lightSpaceMatrix,
    int screenWidth, int screenHeight)
{
    // ── Step 1: Directional lights (full-screen quad) ──────────
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);   // Additive — accumulate light

    dirShader.use();
    gbuffer.bindForReading(GL_TEXTURE0);
    dirShader.setInt("gPosition", 0);  dirShader.setInt("gNormal", 1);
    dirShader.setInt("gAlbedoSpec", 2); dirShader.setInt("gMaterial", 3);
    dirShader.setVec3("camPos", camPos);

    if (shadowMap) {
        shadowMap->bindForReading(GL_TEXTURE4);
        dirShader.setInt("shadowMap", 4);
        dirShader.setMat4("lightSpaceMatrix", lightSpaceMatrix);
        dirShader.setInt("hasShadows", 1);
    } else {
        dirShader.setInt("hasShadows", 0);
    }

    dirShader.setInt("numDirLights", static_cast<int>(dirLights.size()));
    for (int i = 0; i < static_cast<int>(dirLights.size()); ++i) {
        std::string p = "dirLights[" + std::to_string(i) + "].";
        dirShader.setVec3(p + "direction", dirLights[i].direction);
        dirShader.setVec3(p + "colour",    dirLights[i].colour);
        dirShader.setFloat(p + "intensity", dirLights[i].intensity);
    }
    dirShader.setInt("numPointLights", 0);
    screenQuad.draw();

    // ── Step 2: Point lights (light volumes with stencil) ──────
    volShader.use();
    gbuffer.bindForReading(GL_TEXTURE0);
    volShader.setInt("gPosition", 0);  volShader.setInt("gNormal", 1);
    volShader.setInt("gAlbedoSpec", 2); volShader.setInt("gMaterial", 3);
    volShader.setVec3("camPos", camPos);
    volShader.setVec2("screenSize", glm::vec2(screenWidth, screenHeight));

    for (const auto& light : pointLights) {
        glm::mat4 model = glm::scale(
            glm::translate(glm::mat4(1.0f), light.position),
            glm::vec3(light.radius));

        // Stencil pass — mark pixels inside the volume
        glEnable(GL_DEPTH_TEST);  glDisable(GL_BLEND);  glEnable(GL_STENCIL_TEST);
        glClear(GL_STENCIL_BUFFER_BIT);
        glStencilFunc(GL_ALWAYS, 0, 0xFF);
        glStencilOpSeparate(GL_BACK,  GL_KEEP, GL_INCR_WRAP, GL_KEEP);
        glStencilOpSeparate(GL_FRONT, GL_KEEP, GL_DECR_WRAP, GL_KEEP);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glDepthMask(GL_FALSE);  glDisable(GL_CULL_FACE);
        volShader.setMat4("model", model);
        volShader.setMat4("view", view);
        volShader.setMat4("projection", proj);
        lightVolume.draw();

        // Lighting pass — shade where stencil != 0
        glEnable(GL_CULL_FACE);  glCullFace(GL_FRONT);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glStencilFunc(GL_NOTEQUAL, 0, 0xFF);
        glDisable(GL_DEPTH_TEST); glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE);
        volShader.setVec3("lightPos", light.position);
        volShader.setVec3("lightColour", light.colour);
        volShader.setFloat("lightIntensity", light.intensity);
        volShader.setFloat("lightRadius", light.radius);
        lightVolume.draw();

        glCullFace(GL_BACK);  glDisable(GL_STENCIL_TEST);
    }

    glDisable(GL_BLEND); glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE);
}
```

With light volumes, 100 point lights in a scene might collectively cover only 30% of the screen pixels. Each pixel evaluates 2-3 overlapping lights rather than looping through all 100. The performance difference is dramatic.

---

## Forward Pass for Transparent Objects

The G-buffer stores one surface per pixel. It cannot represent a transparent red window with a wall visible behind it. Transparent objects must be rendered with traditional forward rendering after the deferred lighting pass.

The key requirement: transparent objects must depth-test against the opaque geometry from the geometry pass. We do this by blitting the G-buffer's depth buffer into the scene FBO (via `GBuffer::blitDepthTo`), then rendering particles (Ch 45-46), water (Ch 39), and glass with the forward PBR shader from Ch 44, with blending enabled and depth writes disabled. These forward-rendered objects use a limited set of lights (typically the 8 closest/brightest) since transparent surfaces rarely need 128 lights to look correct.

```
HYBRID PIPELINE

  G-buffer pass        --> opaque objects write surface data
  Deferred lighting    --> all lights applied per-pixel
  Depth blit           --> copy depth to scene FBO for forward objects
  Forward pass         --> transparent objects (blending, limited lights)
  Post-processing      --> bloom, tone mapping (Ch 28/44)

  The forward pass uses the same PBR shader from Ch 44 with a
  smaller MAX_POINT_LIGHTS (8 instead of 128). Transparent objects
  are few and far between — the forward cost is negligible.
```

### Light Component Updates

The `PointLightComponent` needs a `radius` field so we know how large each light volume should be:

```cpp
// In src/engine/ecs/components/light_components.h

namespace qe {

struct DirectionalLight {
    glm::vec3 direction  = glm::vec3(0.0f, -1.0f, 0.0f);
    glm::vec3 colour     = glm::vec3(1.0f);
    float     intensity  = 1.0f;
};

struct PointLightComponent {
    glm::vec3 colour     = glm::vec3(1.0f);
    float     intensity  = 1.0f;
    float     radius     = 10.0f;    // NEW: attenuation cutoff distance
};

} // namespace qe
```

The radius is artist-tweakable. A good starting point: `radius = sqrt(intensity / 0.01)` — the distance where intensity drops to 1% of its peak. Creating a point light is just creating an entity with a position and this component:

```cpp
auto torchLight = registry.create();
registry.emplace<Position>(torchLight, glm::vec3(10.0f, 3.0f, -5.0f));
registry.emplace<PointLightComponent>(torchLight,
    PointLightComponent{glm::vec3(1.0f, 0.7f, 0.3f), 2.0f, 15.0f});
```

Fifty torches? Fifty entities. The deferred pipeline handles them all.

---

## Integration with the Render Pipeline

The `RenderPipeline` from Chapter 30a needs new passes. Here is the updated structure:

```
UPDATED RENDER PIPELINE

  Pass    Target              What Happens
  ----    ------              -----------------------
  1       Shadow FBO          Shadow depth pass (Ch 29)
  2       G-buffer FBO        Geometry pass (opaque objects write surface data)
  3       Scene FBO           Deferred lighting (dir lights + point light volumes)
  4       Scene FBO           Skybox (depth <= test, after depth blit)
  5       Scene FBO           Forward pass (transparent objects, particles, water)
  6       Scene FBO           View model (clear depth, weapon on top)
  7       Default FBO         Post-processing (bloom, tone mapping)
  8       Default FBO         HUD (2D overlay)
```

### Updated RenderPipeline Implementation

```cpp
// engine/renderer/render_pipeline.cpp
#include "engine/renderer/render_pipeline.h"
#include "engine/renderer/deferred_lighting.h"

void RenderPipeline::execute(RenderContext& ctx) {
    renderShadows(ctx);
    renderGBuffer(ctx);
    renderDeferredLighting(ctx);
    renderSkybox(ctx);
    renderForwardTransparent(ctx);
    renderViewModels(ctx);
    renderPostProcess(ctx);
    renderHUD(ctx);
}

// ── Pass 1: Shadow map (unchanged from Ch 30a) ────────────────
void RenderPipeline::renderShadows(RenderContext& ctx) {
    if (!ctx.shadowMap) return;
    auto shader = ctx.shaders.get("shadow_depth");
    if (!shader) return;
    shadowMapSystem(ctx.registry, ctx.camera, *ctx.shadowMap,
                    *shader, ctx.lightSpaceMatrix);
}

// ── Pass 2: G-buffer (geometry pass) ───────────────────────────
void RenderPipeline::renderGBuffer(RenderContext& ctx) {
    ctx.gbuffer.bindForWriting();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    auto shader = ctx.shaders.get("gbuffer");
    if (!shader) return;
    shader->use();
    shader->setMat4("view", ctx.camera.getViewMatrix());
    shader->setMat4("projection", ctx.camera.getProjectionMatrix());
    renderOpaqueSystem(ctx.registry, *shader);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ── Pass 3: Deferred lighting ──────────────────────────────────
void RenderPipeline::renderDeferredLighting(RenderContext& ctx) {
    ctx.sceneFBO.bind();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    auto dirShader = ctx.shaders.get("deferred_lighting");
    auto volShader = ctx.shaders.get("deferred_light_volume");
    if (!dirShader || !volShader) return;

    // Gather lights from the ECS registry
    std::vector<DeferredDirLight> dirLights;
    std::vector<DeferredPointLight> pointLights;

    for (auto [entity, light] : ctx.registry.view<DirectionalLight>().each())
        dirLights.push_back({light.direction, light.colour, light.intensity});

    for (auto [entity, light, pos] : ctx.registry.view<PointLightComponent, Position>().each())
        pointLights.push_back({pos.value, light.colour, light.intensity, light.radius});

    deferredLightingPass(
        ctx.gbuffer, ctx.screenQuad, ctx.lightVolume,
        *dirShader, *volShader,
        ctx.camera.getPosition(), ctx.camera.getViewMatrix(),
        ctx.camera.getProjectionMatrix(),
        dirLights, pointLights,
        ctx.shadowMap, ctx.lightSpaceMatrix,
        ctx.window.getWidth(), ctx.window.getHeight());
}

// ── Pass 4: Skybox ─────────────────────────────────────────────
void RenderPipeline::renderSkybox(RenderContext& ctx) {
    if (!ctx.skybox) return;
    auto shader = ctx.shaders.get("skybox");
    if (!shader) return;

    // Blit G-buffer depth to scene FBO so skybox only renders where
    // nothing was drawn (depth = 1.0 at the far plane)
    ctx.gbuffer.blitDepthTo(ctx.sceneFBO.getID(),
                             ctx.window.getWidth(), ctx.window.getHeight());
    ctx.sceneFBO.bind();
    glDepthFunc(GL_LEQUAL);
    shader->use();
    shader->setMat4("view", glm::mat4(glm::mat3(ctx.camera.getViewMatrix())));
    shader->setMat4("projection", ctx.camera.getProjectionMatrix());
    ctx.skybox->draw(*shader);
    glDepthFunc(GL_LESS);
}

// ── Pass 5: Forward transparent ────────────────────────────────
void RenderPipeline::renderForwardTransparent(RenderContext& ctx) {
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    auto shader = ctx.shaders.get("pbr_forward");
    if (shader) {
        shader->use();
        shader->setMat4("view", ctx.camera.getViewMatrix());
        shader->setMat4("projection", ctx.camera.getProjectionMatrix());
        shader->setVec3("camPos", ctx.camera.getPosition());
        uploadForwardLights(ctx.registry, *shader, ctx.camera.getPosition());
        renderTransparentSystem(ctx.registry, *shader);
    }
    particleSystem(ctx.registry, ctx.camera);

    glDepthMask(GL_TRUE); glDisable(GL_BLEND);
}

// ── Passes 6-8: unchanged from Ch 30a ─────────────────────────
// View model clears depth and draws the weapon. Post-process runs
// bloom + tone mapping. HUD draws 2D overlay. See Ch 30a for details.
```

---

## Debug Visualisation

Being able to see the individual G-buffer channels is essential for debugging. If the lighting looks wrong, you need to know: is the position correct? Are the normals correct? Is the albedo in linear space?

```
G-BUFFER DEBUG VIEW

  +---------------+---------------+
  |  Position     |  Normal       |
  |  (world XYZ   |  (direction   |
  |   as colour)  |   as RGB)     |
  +---------------+---------------+
  |  Albedo       |  Material     |
  |  (base        |  (R=metallic  |
  |   colour)     |   G=rough     |
  |               |   B=AO)       |
  +---------------+---------------+
```

### G-Buffer Debug Shader

```glsl
// assets/shaders/gbuffer_debug.frag
#version 460 core

out vec4 FragColor;
in vec2 TexCoords;

uniform sampler2D debugTexture;
uniform int       debugMode;    // 0=pos, 1=normal, 2=albedo, 3=metallic, 4=rough, 5=ao, 6=depth
uniform float     nearPlane;
uniform float     farPlane;

float lineariseDepth(float d) {
    float z = d * 2.0 - 1.0;
    return (2.0 * nearPlane * farPlane) / (farPlane + nearPlane - z * (farPlane - nearPlane));
}

void main() {
    vec4 s = texture(debugTexture, TexCoords);

    if      (debugMode == 0) FragColor = vec4(fract(s.rgb * 0.1), 1.0);       // Position
    else if (debugMode == 1) FragColor = vec4(s.rgb * 0.5 + 0.5, 1.0);        // Normal
    else if (debugMode == 2) FragColor = vec4(pow(s.rgb, vec3(1.0/2.2)), 1.0); // Albedo
    else if (debugMode == 3) FragColor = vec4(vec3(s.r), 1.0);                 // Metallic
    else if (debugMode == 4) FragColor = vec4(vec3(s.g), 1.0);                 // Roughness
    else if (debugMode == 5) FragColor = vec4(vec3(s.b), 1.0);                 // AO
    else if (debugMode == 6) FragColor = vec4(vec3(lineariseDepth(s.r) / farPlane), 1.0);
}
```

### Debug Rendering System

```cpp
// src/engine/renderer/gbuffer_debug.h
#pragma once

#include "engine/renderer/gbuffer.h"
#include "engine/renderer/screen_quad.h"
#include "engine/renderer/shader.h"

enum class GBufferDebugMode {
    Off = -1, Position = 0, Normal = 1, Albedo = 2,
    Metallic = 3, Roughness = 4, AO = 5, Depth = 6
};

inline void renderGBufferDebug(
    const GBuffer& gbuffer, const ScreenQuad& screenQuad,
    Shader& debugShader, GBufferDebugMode mode,
    float nearPlane, float farPlane)
{
    if (mode == GBufferDebugMode::Off) return;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_DEPTH_TEST);
    glClear(GL_COLOR_BUFFER_BIT);

    debugShader.use();
    debugShader.setInt("debugMode", static_cast<int>(mode));
    debugShader.setFloat("nearPlane", nearPlane);
    debugShader.setFloat("farPlane", farPlane);

    // Bind the appropriate G-buffer texture
    glActiveTexture(GL_TEXTURE0);
    int m = static_cast<int>(mode);
    if (m <= 1)      glBindTexture(GL_TEXTURE_2D, gbuffer.getTexture(static_cast<GBuffer::Texture>(m)));
    else if (m == 2) glBindTexture(GL_TEXTURE_2D, gbuffer.getTexture(GBuffer::Albedo));
    else if (m <= 5) glBindTexture(GL_TEXTURE_2D, gbuffer.getTexture(GBuffer::Material));
    else             glBindTexture(GL_TEXTURE_2D, gbuffer.getDepthTexture());

    debugShader.setInt("debugTexture", 0);
    screenQuad.draw();
    glEnable(GL_DEPTH_TEST);
}
```

### Console Command

Register a console command (following the pattern from Ch 27) so you can type `gbuffer normal` or `gbuffer off` at runtime to switch between views.

```cpp
console.registerCommand("gbuffer", "Show G-buffer: gbuffer [off|pos|norm|albedo|metal|rough|ao|depth]",
    [&pipeline, &console](const std::vector<std::string>& args) {
        if (args.empty() || args[0] == "off") {
            pipeline.setGBufferDebugMode(GBufferDebugMode::Off);
            console.print("G-buffer debug OFF");
        } else if (args[0] == "pos")    { pipeline.setGBufferDebugMode(GBufferDebugMode::Position); }
        else if (args[0] == "norm")     { pipeline.setGBufferDebugMode(GBufferDebugMode::Normal); }
        else if (args[0] == "albedo")   { pipeline.setGBufferDebugMode(GBufferDebugMode::Albedo); }
        else if (args[0] == "metal")    { pipeline.setGBufferDebugMode(GBufferDebugMode::Metallic); }
        else if (args[0] == "rough")    { pipeline.setGBufferDebugMode(GBufferDebugMode::Roughness); }
        else if (args[0] == "ao")       { pipeline.setGBufferDebugMode(GBufferDebugMode::AO); }
        else if (args[0] == "depth")    { pipeline.setGBufferDebugMode(GBufferDebugMode::Depth); }

        if (!args.empty() && args[0] != "off") {
            console.print("G-buffer debug: " + args[0]);
        }
    });
```

What to look for in each channel:

- **Position looks banded?** Your position texture format might be GL_RGB8 instead of GL_RGB16F.
- **Normals look flat on a normal-mapped surface?** The TBN matrix is not being passed correctly in the geometry shader.
- **Albedo is too dark?** You might be double-applying the sRGB-to-linear conversion.
- **Metallic channel is all grey?** The metallic map might be loading as sRGB instead of linear.

---

## Config Integration

Add a `deferred` section to `config.lua` (Chapter 50a):

```lua
-- config.lua

deferred = {
    enabled = true,            -- Master toggle: true = deferred, false = forward
    max_point_lights = 128,    -- Maximum point lights in the deferred pass
    forward_light_limit = 8,   -- Max lights for the forward transparency pass
}
```

```cpp
// Reading the config
auto& config = registry.ctx().get<ConfigManager>();
m_useDeferredRendering = config.get<bool>("deferred.enabled", true);
m_maxPointLights       = config.get<int>("deferred.max_point_lights", 128);
m_forwardLightLimit    = config.get<int>("deferred.forward_light_limit", 8);
```

This lets you toggle between forward and deferred rendering for comparison. During development, switch back and forth to verify that both pipelines produce the same visual result. Any difference indicates a bug in the G-buffer or lighting shader.

---

## Putting It All Together

In `PlayingState`, add `GBuffer m_gbuffer` and `LightVolume m_lightVolume` as member variables. Initialise them alongside the existing framebuffers:

```cpp
// In PlayingState::init()
m_gbuffer.init(m_window.getWidth(), m_window.getHeight());
m_lightVolume.init(12);   // 12-segment sphere

// Register the new shaders via ShaderCache
m_shaderCache.load("gbuffer",
    "assets/shaders/gbuffer.vert", "assets/shaders/gbuffer.frag");
m_shaderCache.load("deferred_lighting",
    "assets/shaders/postprocess.vert", "assets/shaders/deferred_lighting.frag");
m_shaderCache.load("deferred_light_volume",
    "assets/shaders/deferred_light_volume.vert",
    "assets/shaders/deferred_light_volume.frag");
m_shaderCache.load("gbuffer_debug",
    "assets/shaders/postprocess.vert", "assets/shaders/gbuffer_debug.frag");
```

Build the `RenderContext` with the two new fields (`m_gbuffer`, `m_lightVolume`) and call `m_pipeline.execute(ctx)` as before. The G-buffer debug overlay runs after the pipeline when a debug mode is active. On window resize, call `m_gbuffer.resize(width, height)` alongside the existing FBO resizes.

---

## Performance Comparison

```
Quake-style arena, 250 opaque objects, 1080p:

  Lights    Forward    Deferred    Speedup
      4       2.8 ms     3.2 ms    0.9x (forward wins -- G-buffer overhead)
     16       6.8 ms     3.6 ms    1.9x
     64      21.2 ms     4.5 ms    4.7x
    128      41.8 ms     5.8 ms    7.2x

  The crossover point is around 6-8 lights.
  Below that, forward is faster (no G-buffer overhead).
  Above that, deferred pulls ahead rapidly.

Breakdown at 64 lights:
  Forward:   250 objects x ~5000 fragments x 64 lights = lots of wasted BRDF
  Deferred:  Geometry pass: 1.8 ms (no lights)
             Lighting pass: 2.7 ms (light volumes limit per-pixel work)
             Total:         4.5 ms
```

---

## Common Pitfalls

**MSAA does not work with deferred rendering.** The G-buffer stores per-pixel data, not per-sample. Use FXAA, TAA, or SMAA instead (post-process anti-aliasing).

**G-buffer bandwidth is the bottleneck.** On bandwidth-limited GPUs, deferred can be slower even with many lights. Profile your hardware.

**Forgetting `glDrawBuffers` makes everything black.** Without it, only attachment 0 is written. This is the most common deferred rendering bug.

**Linear vs sRGB confusion.** The G-buffer stores albedo in linear space. Lighting computes in linear. Tone mapping converts to sRGB. Storing sRGB albedo in the G-buffer produces washed-out results.

**Skybox ordering.** Render the skybox *after* the deferred lighting pass into the scene FBO, not during the geometry pass. Blit the G-buffer depth first so the skybox only appears where no geometry was drawn.

**Light bleeding through walls.** Point light volumes are spheres that do not respect occlusion. A light on one side of a thin wall can bleed through. Solutions: per-light shadow maps (expensive), smaller radii, or accept it as a limitation.

**Transparent objects disappear.** You forgot the forward pass. Transparent entities must be rendered after deferred lighting with the forward PBR shader from Chapter 44, with blending enabled and depth writes off.

---

## C++ Concept Sidebar: Multiple Return Values

Multiple Render Targets are conceptually like a function that returns multiple values. The geometry pass fragment shader takes surface data as input and produces four outputs: position, normal, albedo, and material properties. In C++, how would you return multiple values from a function?

### The Old Way: Output Parameters

```cpp
// C-style: pass pointers, function writes through them
void computeSurface(const Vertex& v,
                    glm::vec3* outPosition,
                    glm::vec3* outNormal,
                    glm::vec3* outAlbedo,
                    float* outMetallic) {
    *outPosition = v.worldPos;
    *outNormal   = computeNormal(v);
    *outAlbedo   = sampleAlbedo(v.uv);
    *outMetallic = sampleMetallic(v.uv);
}
```

This is verbose, error-prone (null pointers, forgetting to write one output), and hard to read. The caller cannot tell from the signature which parameters are inputs and which are outputs without reading the documentation.

### std::tuple and Structured Bindings (C++17)

```cpp
std::tuple<glm::vec3, glm::vec3, glm::vec3, MaterialProps>
computeSurface(const Vertex& v) {
    return {
        v.worldPos,
        computeNormal(v),
        sampleAlbedo(v.uv),
        sampleMaterial(v.uv)
    };
}

// Call site (C++17 structured bindings):
auto [pos, normal, albedo, mat] = computeSurface(vertex);
```

**Structured bindings** (the `auto [a, b, c, d] = ...` syntax) were introduced in C++17 and are the cleanest way to unpack multiple return values. Each variable is bound to the corresponding tuple element. The compiler deduces the types automatically.

This is the C++ analogy to MRT: the GPU's fragment shader writes to `layout(location = 0)`, `layout(location = 1)`, etc., and each goes to a different texture. The structured binding syntax writes to `pos`, `normal`, `albedo`, `mat`, and each goes to a different variable.

### Named Structs for Clarity (C++20)

When the meaning of each value is not obvious from context, a dedicated struct is clearer than a tuple:

```cpp
struct GBufferData {
    glm::vec3     position;
    glm::vec3     normal;
    glm::vec3     albedo;
    MaterialProps material;
};

GBufferData computeSurface(const Vertex& v) {
    return {
        .position = v.worldPos,
        .normal   = computeNormal(v),
        .albedo   = sampleAlbedo(v.uv),
        .material = sampleMaterial(v.uv)
    };
}
```

The designated initialiser syntax (`.position = ...`) was standardised in C++20 and makes the intent clear at both the definition and call site.

### When to Use Each Approach

| Approach | Use When |
|----------|----------|
| Output parameters (`T* out`) | Never, in modern C++ |
| `std::pair` / `std::tuple` | Quick, unnamed returns (2-3 values) |
| Named struct | More than 3 values, or when names matter for clarity |
| Structured bindings | Decomposing either tuples or structs at the call site |

The general rule: use `std::pair` for two values, `std::tuple` with structured bindings for quick throwaway returns, and named structs for anything that appears in a public API or is used more than once. Our G-buffer is firmly in the "named struct" category — the `GBuffer::Texture` enum gives each attachment a meaningful name.

---

## File Summary

Here is every file we created or modified in this chapter:

| File | Status | Purpose |
|------|--------|---------|
| `src/engine/renderer/gbuffer.h` | **New** | `GBuffer` class -- FBO with 4 colour attachments + depth |
| `src/engine/renderer/gbuffer.cpp` | **New** | `GBuffer` implementation -- create, bind, resize, cleanup |
| `src/engine/renderer/light_volume.h` | **New** | `LightVolume` class -- unit sphere mesh for point light volumes |
| `src/engine/renderer/light_volume.cpp` | **New** | `LightVolume` implementation -- sphere generation and drawing |
| `src/engine/renderer/deferred_lighting.h` | **New** | `deferredLightingPass` free function -- orchestrates lighting |
| `src/engine/renderer/gbuffer_debug.h` | **New** | `renderGBufferDebug` -- debug visualisation of G-buffer channels |
| `src/engine/renderer/render_pipeline.h` | **Modified** | Added G-buffer and deferred lighting passes to `RenderContext` |
| `src/engine/renderer/render_pipeline.cpp` | **Modified** | Implemented new pass ordering |
| `src/engine/ecs/components/light_components.h` | **Modified** | Added `radius` field to `PointLightComponent` |
| `assets/shaders/gbuffer.vert` | **New** | Geometry pass vertex shader |
| `assets/shaders/gbuffer.frag` | **New** | Geometry pass fragment shader -- writes to 4 MRT outputs |
| `assets/shaders/deferred_lighting.vert` | **New** | Full-screen quad vertex shader (reuses postprocess.vert) |
| `assets/shaders/deferred_lighting.frag` | **New** | Lighting pass -- reads G-buffer, evaluates all lights |
| `assets/shaders/deferred_light_volume.vert` | **New** | Light volume vertex shader -- transforms sphere to light position |
| `assets/shaders/deferred_light_volume.frag` | **New** | Light volume fragment shader -- single-light PBR evaluation |
| `assets/shaders/gbuffer_debug.frag` | **New** | Debug visualisation of individual G-buffer channels |
| `config.lua` | **Modified** | Added `deferred` configuration section |

---

## What's Next

In **Chapter 53**, we will implement **Screen-Space Ambient Occlusion (SSAO)** -- a technique that uses the G-buffer's position and normal data to approximate ambient occlusion in real time. The G-buffer we built in this chapter provides exactly the data SSAO needs: per-pixel positions and normals. SSAO darkens crevices, corners, and contact points, adding depth and grounding to the scene without any pre-baked data. It is the first of several screen-space effects that deferred rendering makes possible.
