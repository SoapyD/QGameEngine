# Chapter 44: Physically-Based Rendering (PBR) Materials

## What You'll Learn
- Why the Phong lighting model breaks down and how PBR fixes it with physics
- The metallic-roughness workflow: albedo, metallic, roughness, and AO texture maps
- Microfacet theory: modelling surfaces as millions of tiny mirrors
- The three BRDF functions: Normal Distribution (GGX), Geometry (Smith-Schlick), Fresnel (Schlick)
- The Cook-Torrance specular BRDF and Lambertian diffuse, combined with energy conservation
- Writing a complete PBR fragment shader with support for directional and point lights
- HDR rendering with floating-point framebuffers and tone mapping
- Image-Based Lighting (IBL): irradiance maps, pre-filtered environment maps, and the BRDF LUT
- The `PBRMaterial` ECS component and how it replaces the old Phong material
- Migrating from Phong to PBR without rewriting the engine
- C++ concepts: linear colour space, floating-point FBOs, and pre-computation techniques

---

## Why PBR?

In Chapter 7 we built Phong lighting. It works: ambient provides a baseline, diffuse responds to light direction, specular adds highlights. The problem is that Phong is an empirical approximation invented in 1975. It has no basis in physics, and it shows.

Consider a polished metal sphere under two different lighting conditions — one with a single strong directional light, another with many soft area lights. Under Phong, the same material parameters produce wildly different visual results. A surface that looks like brushed steel in one scene looks like dull plastic in another. The artist tweaks the specular exponent and ambient term until it "looks right," ships the game, and then a cutscene with different lighting makes everything look wrong again.

```
PHONG vs PBR — Same material, different lighting

  PHONG:                                PBR:
  Scene A         Scene B               Scene A         Scene B
  (strong)        (soft)                (strong)        (soft)
  ┌──────────┐    ┌──────────┐          ┌──────────┐    ┌──────────┐
  │  .-""-.  │    │  .-""-.  │          │  .-""-.  │    │  .-""-.  │
  │ / ●    \ │    │ /      \ │          │ / ●    \ │    │ / ·  · \ │
  │ \      / │    │ \      / │          │ \   ·  / │    │ \ ·  · / │
  │  '-__-'  │    │  '-__-'  │          │  '-__-'  │    │  '-__-'  │
  │  Shiny   │    │  Flat    │          │  Shiny   │    │  Still   │
  │  metal   │    │  plastic │          │  metal   │    │  metal   │
  └──────────┘    └──────────┘          └──────────┘    └──────────┘
  Same settings, different look.        Physical properties, consistent look.
```

The fundamental issues with Phong:

1. **No energy conservation.** Phong's diffuse and specular terms are added independently. Nothing prevents the outgoing light from exceeding the incoming light. Physics says a surface cannot reflect more energy than it receives.

2. **No Fresnel effect.** Every real surface becomes more reflective at grazing angles. Look at a wooden table from a steep angle — the far edge reflects the room like a mirror. Phong has no mechanism for this.

3. **No metallic distinction.** Metals and non-metals reflect light in fundamentally different ways. Metals have coloured reflections and no diffuse component. Phong treats everything the same.

PBR solves all three. It is built on physics: microfacet theory models how tiny surface imperfections scatter light, energy conservation ensures outgoing light never exceeds incoming light, and the Fresnel equations correctly handle the angle-dependent reflectance of all materials. An artist authors a material once — setting its base colour, how metallic it is, and how rough its surface is — and it looks correct under any lighting condition.

Every modern engine uses PBR: Unreal Engine, Unity, Godot, id Tech. It is the industry standard. This chapter brings QEngine up to that standard.

---

## The Metallic-Roughness Workflow

PBR materials are defined by a set of texture maps. The most widely used workflow — and the one we will implement — is the **metallic-roughness** workflow, standardised by glTF 2.0 and used by Unreal, Godot, and most PBR asset libraries.

### The Texture Stack

```
PBR MATERIAL TEXTURE STACK

  ┌─────────────────┐
  │   Albedo Map    │  Base colour (no baked lighting)
  │   (RGB)         │  sRGB space — gamma-corrected
  ├─────────────────┤
  │  Metallic Map   │  0.0 = dielectric, 1.0 = metal
  │   (Greyscale)   │  Linear space
  ├─────────────────┤
  │  Roughness Map  │  0.0 = mirror smooth, 1.0 = completely rough
  │   (Greyscale)   │  Linear space
  ├─────────────────┤
  │     AO Map      │  Pre-baked ambient occlusion
  │   (Greyscale)   │  Linear space
  ├─────────────────┤
  │   Normal Map    │  Per-pixel surface normals (from Ch 35)
  │   (RGB)         │  Linear space
  ├─────────────────┤
  │  Emissive Map   │  Self-illumination (optional)
  │   (RGB)         │  sRGB space
  └─────────────────┘
```

### What Each Map Encodes

**Albedo (Base Colour)** — The colour of the surface with all lighting information removed. No baked shadows, no highlights, no ambient occlusion. For a red brick wall, this is just the red-brown colour of the bricks. This replaces the "diffuse texture" from our Phong pipeline. Important: albedo textures are authored in sRGB space and must be converted to linear space in the shader (or by loading with `GL_SRGB8_ALPHA8`).

**Metallic** — A greyscale value from 0.0 to 1.0 that defines whether the surface is a dielectric (non-metal) or a conductor (metal). Most of the map is either 0.0 or 1.0 — surfaces are rarely "partly metal." The in-between values exist for transitions like paint scratched off a metal surface, where the edge pixels blend between metal and paint.

- 0.0 = dielectric: plastic, wood, stone, skin, fabric, rubber
- 1.0 = metal: gold, iron, copper, aluminium, steel

**Roughness** — A greyscale value from 0.0 to 1.0 that controls how rough the surface is at a microscopic level. This directly controls the shape and size of specular highlights.

- 0.0 = perfectly smooth (mirror-like reflections, tiny sharp highlights)
- 1.0 = completely rough (no visible reflection, broad diffuse scattering)

**Ambient Occlusion (AO)** — A greyscale map that darkens areas where ambient light would be occluded: crevices, corners, areas between objects in close contact. This is pre-baked (not computed at runtime) and provides cheap, convincing contact shadows.

**Normal Map** — Per-pixel surface normals encoded in tangent space. We built this in Chapter 35 — it works unchanged in the PBR pipeline.

**Emissive** — An optional RGB map for surfaces that emit light: glowing runes, LED panels, lava, neon signs. Emissive values are added after all lighting calculations and can exceed 1.0 for HDR bloom.

### Sourcing PBR Textures

Free PBR texture libraries: **ambientcg.com** (CC0 PBR materials), **polyhaven.com** (HDR environments + PBR textures), and **Quixel Megascans** (photogrammetry-scanned materials). For a Quake-style game, you can also hand-paint stylised albedo textures and use uniform metallic/roughness values instead of full texture maps.

---

## Microfacet Theory

Phong models surfaces as perfectly smooth at the microscopic level. PBR takes a different view: every surface is covered in millions of tiny flat mirrors called **microfacets**. Each microfacet reflects light like a perfect mirror, but because they are oriented randomly, the aggregate effect is what we see as rough or smooth surfaces.

```
MICROFACET SURFACE MODEL

  Smooth surface (low roughness):
  Microfacets are mostly aligned — light reflects coherently

    Incoming light      Reflected light
         \  \  \  \        /  /  /  /
          \ \ \ \        / / / /
    ──────────────────────────────────
     _____ _____ _____ _____ _____        <-- microfacets nearly flat
    Tight, sharp specular highlight. Mirror-like reflection.


  Rough surface (high roughness):
  Microfacets are randomly oriented — light scatters in many directions

    Incoming light       Reflected light scatters
         \  \  \  \       ↗  ↑  ↗  →  ↑  ↗
          \ \ \ \       ↗ ↑ → ↗ ↑ →
    ──────────────────────────────────
     _/ \__/ \_/\  /\_ \__/ \_/\        <-- microfacets randomly tilted
    Broad, blurry specular highlight. Matte appearance.
```

The roughness parameter controls the statistical distribution of microfacet orientations. Low roughness means most microfacets point in the same direction (the surface normal), producing a tight specular reflection. High roughness means the microfacets are scattered in many directions, producing a broad, diffuse-looking highlight.

### The Specular BRDF

A BRDF (Bidirectional Reflectance Distribution Function) describes how light reflects off a surface. The microfacet specular BRDF used in PBR has three components:

1. **D — Normal Distribution Function (NDF):** How many microfacets are oriented to reflect light toward the viewer? This determines the shape and intensity of the specular highlight.

2. **G — Geometry Function:** How many microfacets are blocked by neighbouring microfacets? At grazing angles, some microfacets shadow each other, reducing the reflected light.

3. **F — Fresnel Equation:** How much light is reflected vs absorbed at the surface? All surfaces reflect more light at shallow (grazing) angles.

These combine into the Cook-Torrance specular BRDF:

```
                    D(h) * G(n,v,l) * F(h,v)
  f_specular  =  ─────────────────────────────
                  4 * dot(n,v) * dot(n,l)

  Where:
    n = surface normal
    v = view direction (from surface to camera)
    l = light direction (from surface to light)
    h = halfway vector = normalize(v + l)
```

The denominator normalises the result to ensure energy conservation. The complete BRDF combines this specular term with a Lambertian diffuse term:

```
  f = kD * (albedo / PI) + kS * f_specular

  Where:
    kS = Fresnel term F (specular contribution)
    kD = (1 - kS) * (1 - metallic)    (diffuse contribution)

  Metals have no diffuse: kD = 0 when metallic = 1.0
  Energy conservation: kD + kS <= 1.0
```

---

## The Three BRDF Functions

### Normal Distribution Function — GGX/Trowbridge-Reitz

The NDF determines what proportion of the microfacets are aligned with the halfway vector `h`. Only microfacets pointing in the direction of `h` can reflect light from `l` toward `v`. The GGX distribution (also called Trowbridge-Reitz) is the industry standard because it produces a realistic highlight with a long tail that falls off gradually.

**Formula:**

```
                        a^2
  D(h) = ──────────────────────────────────
          PI * (dot(n,h)^2 * (a^2 - 1) + 1)^2

  Where a = roughness^2  (squaring roughness gives more perceptually linear control)
```

When roughness is low (smooth surface), D produces a sharp spike centred on the reflection direction. When roughness is high, D spreads out, producing a broad, dim highlight.

```glsl
float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;

    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    denom = 3.14159265 * denom * denom;

    return a2 / max(denom, 0.0000001);
}
```

### Geometry Function — Smith's Method with Schlick-GGX

The geometry function accounts for microfacets blocking each other. There are two cases: **geometry obstruction** (microfacets block the view direction) and **geometry shadowing** (microfacets block the light direction). Smith's method computes both independently and multiplies them.

Each is computed using the Schlick-GGX approximation:

**Formula:**

```
                    dot(n, v)
  G_SchlickGGX = ────────────────────────────
                  dot(n,v) * (1 - k) + k

  Where k = (roughness + 1)^2 / 8   (for direct lighting)

  Smith's method:  G(n,v,l) = G_SchlickGGX(n,v) * G_SchlickGGX(n,l)
```

At low roughness, G is close to 1.0 — smooth surfaces have little self-shadowing. At high roughness and grazing angles, G drops significantly, dimming the specular highlight where microfacets block each other.

```glsl
float geometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;

    return NdotV / (NdotV * (1.0 - k) + k);
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);

    float ggx1 = geometrySchlickGGX(NdotV, roughness);
    float ggx2 = geometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}
```

### Fresnel Equation — Schlick's Approximation

The Fresnel equations describe how much light is reflected at a surface boundary. At normal incidence (looking straight at the surface), the amount reflected depends on the material. At grazing angles (looking nearly parallel to the surface), almost all light is reflected — this is why a lake looks like a mirror when you look across it at a shallow angle, but you can see through the water when looking straight down.

**F0** is the base reflectivity at normal incidence:
- **Dielectrics** (non-metals): F0 is approximately 0.04 for most materials (plastic, wood, stone, skin). The reflected light is white/grey.
- **Metals**: F0 is the albedo colour itself. Metals have coloured reflections — gold reflects yellow, copper reflects orange.

**Formula:**

```
  F(h,v) = F0 + (1 - F0) * (1 - dot(h,v))^5
```

At normal incidence (`dot(h,v) = 1`), F = F0. At grazing angles (`dot(h,v) -> 0`), F approaches 1.0. Every surface becomes a perfect mirror at shallow enough angles.

```glsl
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}
```

For IBL (covered later), we need a roughness-aware variant that prevents rough surfaces from having mirror-sharp Fresnel reflections at grazing angles:

```glsl
vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0)
             * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}
```

---

## The PBR Fragment Shader

This is the complete PBR fragment shader supporting both directional and point lights, with normal mapping from Chapter 35.

### Vertex Shader

```glsl
// In assets/shaders/pbr.vert

#version 460 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
layout (location = 3) in vec4 aTangent;    // xyz = tangent, w = handedness (Ch 35)

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform vec3 camPos;

out vec3 FragPos;
out vec2 TexCoords;
out vec3 CamPos;
out mat3 TBN;

void main() {
    vec4 worldPos = model * vec4(aPos, 1.0);
    FragPos   = worldPos.xyz;
    TexCoords = aTexCoords;
    CamPos    = camPos;

    // Construct TBN matrix for normal mapping (Ch 35)
    mat3 normalMatrix = transpose(inverse(mat3(model)));
    vec3 T = normalize(normalMatrix * aTangent.xyz);
    vec3 N = normalize(normalMatrix * aNormal);
    // Re-orthogonalise T with respect to N
    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T) * aTangent.w;  // Handedness
    TBN = mat3(T, B, N);

    gl_Position = projection * view * worldPos;
}
```

### Fragment Shader

```glsl
// In assets/shaders/pbr.frag

#version 460 core

out vec4 FragColor;

in vec3 FragPos;
in vec2 TexCoords;
in vec3 CamPos;
in mat3 TBN;

// ─── Material textures ─────────────────────────────────────────
uniform sampler2D albedoMap;
uniform sampler2D normalMap;
uniform sampler2D metallicMap;
uniform sampler2D roughnessMap;
uniform sampler2D aoMap;
uniform sampler2D emissiveMap;

// Uniform fallbacks when no texture is bound
uniform vec3  u_albedo     = vec3(1.0);
uniform float u_metallic   = 0.0;
uniform float u_roughness  = 0.5;
uniform float u_ao         = 1.0;
uniform vec3  u_emissive   = vec3(0.0);

// Flags: 1 if a texture is bound, 0 if using uniform value
uniform int hasAlbedoMap;
uniform int hasNormalMap;
uniform int hasMetallicMap;
uniform int hasRoughnessMap;
uniform int hasAoMap;
uniform int hasEmissiveMap;

// ─── Lights ─────────────────────────────────────────────────────
struct DirLight {
    vec3 direction;
    vec3 colour;
    float intensity;
};

struct PointLight {
    vec3 position;
    vec3 colour;
    float intensity;
};

#define MAX_DIR_LIGHTS   4
#define MAX_POINT_LIGHTS 32

uniform int       numDirLights;
uniform DirLight  dirLights[MAX_DIR_LIGHTS];
uniform int       numPointLights;
uniform PointLight pointLights[MAX_POINT_LIGHTS];

// ─── Constants ──────────────────────────────────────────────────
const float PI = 3.14159265359;

// ─── BRDF Functions ─────────────────────────────────────────────

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

// ─── Main ───────────────────────────────────────────────────────

void main() {
    // Sample material properties
    vec3  albedo    = hasAlbedoMap    == 1 ? pow(texture(albedoMap, TexCoords).rgb, vec3(2.2))
                                           : u_albedo;
    float metallic  = hasMetallicMap  == 1 ? texture(metallicMap, TexCoords).r
                                           : u_metallic;
    float roughness = hasRoughnessMap == 1 ? texture(roughnessMap, TexCoords).r
                                           : u_roughness;
    float ao        = hasAoMap        == 1 ? texture(aoMap, TexCoords).r
                                           : u_ao;
    vec3  emissive  = hasEmissiveMap  == 1 ? pow(texture(emissiveMap, TexCoords).rgb, vec3(2.2))
                                           : u_emissive;

    // Normal mapping (Ch 35)
    vec3 N;
    if (hasNormalMap == 1) {
        N = texture(normalMap, TexCoords).rgb * 2.0 - 1.0;
        N = normalize(TBN * N);
    } else {
        N = normalize(TBN[2]);  // Geometric normal (third column of TBN)
    }

    vec3 V = normalize(CamPos - FragPos);

    // F0: base reflectivity
    // Dielectrics: 0.04 (average for most non-metals)
    // Metals: use the albedo colour as F0
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);

    // ─── Accumulate light contributions ──────────────────────────
    vec3 Lo = vec3(0.0);

    // Directional lights
    for (int i = 0; i < numDirLights; i++) {
        vec3 L = normalize(-dirLights[i].direction);
        vec3 H = normalize(V + L);
        vec3 radiance = dirLights[i].colour * dirLights[i].intensity;

        // Cook-Torrance specular BRDF
        float D = distributionGGX(N, H, roughness);
        float G = geometrySmith(N, V, L, roughness);
        vec3  F = fresnelSchlick(max(dot(H, V), 0.0), F0);

        vec3 specular = (D * G * F)
                      / max(4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0), 0.001);

        // Energy conservation: kD + kS <= 1, metals have no diffuse
        vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

        Lo += (kD * albedo / PI + specular) * radiance * max(dot(N, L), 0.0);
    }

    // Point lights — same BRDF, but with distance attenuation
    for (int i = 0; i < numPointLights; i++) {
        vec3 L = normalize(pointLights[i].position - FragPos);
        vec3 H = normalize(V + L);

        float distance    = length(pointLights[i].position - FragPos);
        float attenuation = 1.0 / (distance * distance);  // Inverse square law
        vec3  radiance    = pointLights[i].colour * pointLights[i].intensity
                          * attenuation;

        float D = distributionGGX(N, H, roughness);
        float G = geometrySmith(N, V, L, roughness);
        vec3  F = fresnelSchlick(max(dot(H, V), 0.0), F0);

        vec3 specular = (D * G * F)
                      / max(4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0), 0.001);

        vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

        Lo += (kD * albedo / PI + specular) * radiance * max(dot(N, L), 0.0);
    }

    // ─── Ambient term (placeholder — IBL replaces this) ──────────
    vec3 ambient = vec3(0.03) * albedo * ao;

    vec3 colour = ambient + Lo + emissive;

    // Output HDR colour — tone mapping happens in post-processing
    FragColor = vec4(colour, 1.0);
}
```

Key points: (1) Albedo is converted from sRGB to linear with `pow(colour, vec3(2.2))` — all lighting math must be in linear space. (2) F0 interpolates between 0.04 (dielectric) and the albedo colour (metal). (3) kD is zeroed for metals — they have no diffuse. (4) Point lights use physically correct inverse-square attenuation. (5) Output is HDR — values can exceed 1.0 and are tone-mapped in post-processing.

---

## HDR and Tone Mapping

PBR produces colour values that exceed the 0.0-1.0 range. A bright specular highlight from a strong light source might produce values of 5.0 or higher. If we render to a standard 8-bit framebuffer, everything above 1.0 gets clamped to white, destroying all highlight detail. We need a floating-point framebuffer that can store HDR values, and a tone mapping pass that compresses the range back to displayable values.

### Floating-Point Framebuffer

In Chapter 28's `Framebuffer::create()`, change one line — the internal format of the colour attachment:

```cpp
// Before (Ch 28):
glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_width, m_height, 0,
             GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

// After (PBR):
glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, m_width, m_height, 0,
             GL_RGBA, GL_FLOAT, nullptr);
```

Everything else in the Framebuffer class stays the same. `GL_RGBA16F` stores 16-bit floats per channel — values can far exceed 1.0 without clamping, at double the memory cost of `GL_RGBA8`.

### Tone Mapping Shader

The tone mapping pass runs as part of the post-processing pipeline from Chapter 28. It takes the HDR texture and compresses it to the displayable [0, 1] range. We also apply gamma correction here, converting from linear space back to sRGB for the monitor.

```glsl
// In assets/shaders/tonemap.frag

#version 460 core

out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D hdrBuffer;
uniform float     exposure = 1.0;
uniform int       tonemapMode = 0;   // 0 = Reinhard, 1 = ACES

// ─── Tone mapping operators ──────────────────────────────────────

// Reinhard: simple, good for even scenes
vec3 reinhardTonemap(vec3 colour) {
    return colour / (colour + vec3(1.0));
}

// ACES filmic: matches cinema look, better contrast and saturation
vec3 acesTonemap(vec3 colour) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((colour * (a * colour + b)) /
                 (colour * (c * colour + d) + e), 0.0, 1.0);
}

void main() {
    vec3 hdrColour = texture(hdrBuffer, TexCoords).rgb;

    // Apply exposure
    hdrColour *= exposure;

    // Tone map
    vec3 mapped;
    if (tonemapMode == 0) {
        mapped = reinhardTonemap(hdrColour);
    } else {
        mapped = acesTonemap(hdrColour);
    }

    // Gamma correction: linear space -> sRGB
    mapped = pow(mapped, vec3(1.0 / 2.2));

    FragColor = vec4(mapped, 1.0);
}
```

### Integration with the Post-Processing Pipeline

The rendering pipeline from Chapter 28 now looks like this:

```
PBR RENDERING PIPELINE

  ┌────────────────────────┐
  │  Scene Pass             │
  │  Render all geometry    │
  │  with pbr.vert/frag    │──────> HDR Framebuffer (GL_RGBA16F)
  │  Output is HDR          │
  └────────────────────────┘
              │
              ▼
  ┌────────────────────────┐
  │  Bloom Pass (optional)  │
  │  Extract bright pixels  │
  │  Gaussian blur          │──────> Bloom texture
  │  (Ch 28 ping-pong)     │
  └────────────────────────┘
              │
              ▼
  ┌────────────────────────┐
  │  Tone Map Pass          │
  │  Exposure adjustment    │
  │  Reinhard or ACES       │──────> Default framebuffer (screen)
  │  Gamma correction       │
  │  + add bloom            │
  └────────────────────────┘
```

Bloom benefits greatly from HDR. Bright areas naturally stand out because their values far exceed 1.0. Extract, blur, and add back — the result is physically motivated glow around specular highlights and emissive surfaces.

---

## Image-Based Lighting (IBL)

The PBR shader above handles direct lights — directional and point lights. But in the real world, light comes from everywhere: the sky, the ground, bouncing off walls. This indirect lighting is what makes objects look like they belong in an environment rather than floating in a void.

**Image-Based Lighting** uses the environment map (from Chapter 24's skybox) as a light source. Every surface samples the cubemap for ambient lighting: a character in a red room picks up red light; a weapon outdoors reflects the sky.

### The Split-Sum Approximation

Computing IBL analytically requires integrating the lighting equation over the entire hemisphere for every pixel — far too expensive for real-time. The **split-sum approximation** breaks this into two pre-computed parts: (1) a **pre-filtered environment map** that blurs the environment by the specular lobe at each roughness level, stored in cubemap mip levels; and (2) a **BRDF integration LUT** that pre-computes the BRDF integral itself into a 2D texture indexed by `(NdotV, roughness)`.

At runtime, the lookup is three texture samples:

```glsl
prefilteredColour = textureLod(prefilterMap, R, roughness * maxMipLevel);
brdf              = texture(brdfLUT, vec2(NdotV, roughness));
specularIBL       = prefilteredColour * (F * brdf.x + brdf.y);
```

### The Three IBL Textures

**1. Irradiance Cubemap (Diffuse IBL)** — Stores the average incoming light from a hemisphere centred on each direction. A heavily blurred version of the environment map, sampled with the surface normal for diffuse ambient lighting.

**2. Pre-filtered Environment Map (Specular IBL)** — The environment convolved with the GGX specular lobe at increasing roughness levels. Mip 0 is the sharp original; higher mips are progressively blurrier. Sampled with the reflection vector and roughness.

**3. BRDF Integration LUT** — A 2D texture (x = NdotV, y = roughness) storing scale and bias values that approximate the BRDF integral. Material-independent — compute once, reuse everywhere.

```
IBL TEXTURE PIPELINE

  HDR equirectangular image
          │
          ▼
  Equirect → Cubemap conversion ──> Environment Cubemap (512x512)
          │
     ┌────┴────┐
     ▼         ▼
  Convolve   Pre-filter at
  diffuse    each mip level
     │         │
     ▼         ▼
  Irradiance Pre-filtered Env Map   BRDF LUT
  (32x32)    (128x128 + mips)       (512x512, computed separately)
```

### Generating IBL Textures

IBL textures are pre-computed at load time. Here is the C++ code.

```cpp
// In src/engine/renderer/ibl.h

#pragma once

#include <glad/glad.h>
#include <string>

struct IBLTextures {
    GLuint envCubemap     = 0;   // Environment cubemap (for skybox)
    GLuint irradianceMap  = 0;   // Diffuse IBL (32x32 per face)
    GLuint prefilterMap   = 0;   // Specular IBL (128x128 + mips)
    GLuint brdfLUT        = 0;   // 2D BRDF integration texture (512x512)
};

// Load an HDR equirectangular image and generate all IBL textures.
// This is an expensive operation — call once at level load, not per frame.
IBLTextures generateIBL(const std::string& hdrPath);
```

```cpp
// In src/engine/renderer/ibl.cpp

#include "engine/renderer/ibl.h"
#include "engine/renderer/shader.h"
#include <stb_image.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

// Six view matrices for rendering into each cubemap face
static const glm::mat4 captureProjection =
    glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);

static const glm::mat4 captureViews[] = {
    glm::lookAt(glm::vec3(0), glm::vec3( 1, 0, 0), glm::vec3(0,-1, 0)),
    glm::lookAt(glm::vec3(0), glm::vec3(-1, 0, 0), glm::vec3(0,-1, 0)),
    glm::lookAt(glm::vec3(0), glm::vec3( 0, 1, 0), glm::vec3(0, 0, 1)),
    glm::lookAt(glm::vec3(0), glm::vec3( 0,-1, 0), glm::vec3(0, 0,-1)),
    glm::lookAt(glm::vec3(0), glm::vec3( 0, 0, 1), glm::vec3(0,-1, 0)),
    glm::lookAt(glm::vec3(0), glm::vec3( 0, 0,-1), glm::vec3(0,-1, 0)),
};

// Helper: create a cubemap, allocate faces, set standard filtering
static GLuint createCubemap(int size, GLenum minFilter) {
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_CUBE_MAP, tex);
    for (int i = 0; i < 6; i++) {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F,
                     size, size, 0, GL_RGB, GL_FLOAT, nullptr);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, minFilter);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    return tex;
}

// Helper: render a shader into all 6 faces of a cubemap at a given mip level
static void renderToCubemap(Shader& shader, GLuint cubemap, GLuint fbo,
                            int size, int mipLevel = 0) {
    glViewport(0, 0, size, size);
    for (int i = 0; i < 6; i++) {
        shader.setMat4("view", captureViews[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_CUBE_MAP_POSITIVE_X + i,
                               cubemap, mipLevel);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        renderCube();  // Unit cube — same geometry as Ch 24's skybox
    }
}

IBLTextures generateIBL(const std::string& hdrPath) {
    IBLTextures ibl;

    // ─── Step 1: Load HDR environment map ────────────────────────
    stbi_set_flip_vertically_on_load(true);
    int width, height, channels;
    float* data = stbi_loadf(hdrPath.c_str(), &width, &height, &channels, 0);
    if (!data) {
        std::cerr << "Failed to load HDR image: " << hdrPath << "\n";
        return ibl;
    }

    GLuint hdrTexture;
    glGenTextures(1, &hdrTexture);
    glBindTexture(GL_TEXTURE_2D, hdrTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0,
                 GL_RGB, GL_FLOAT, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    stbi_image_free(data);

    // Create a capture FBO for rendering into cubemap faces
    GLuint captureFBO, captureRBO;
    glGenFramebuffers(1, &captureFBO);
    glGenRenderbuffers(1, &captureRBO);
    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);

    // ─── Step 2: Convert equirectangular to cubemap ──────────────
    const int envSize = 512;
    ibl.envCubemap = createCubemap(envSize, GL_LINEAR_MIPMAP_LINEAR);

    Shader equiToCubeShader("assets/shaders/equi_to_cube.vert",
                            "assets/shaders/equi_to_cube.frag");
    equiToCubeShader.use();
    equiToCubeShader.setInt("equirectangularMap", 0);
    equiToCubeShader.setMat4("projection", captureProjection);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, hdrTexture);

    glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, envSize, envSize);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, captureRBO);

    renderToCubemap(equiToCubeShader, ibl.envCubemap, captureFBO, envSize);
    glBindTexture(GL_TEXTURE_CUBE_MAP, ibl.envCubemap);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

    // ─── Step 3: Generate irradiance cubemap (diffuse IBL) ───────
    const int irradianceSize = 32;
    ibl.irradianceMap = createCubemap(irradianceSize, GL_LINEAR);

    Shader irradianceShader("assets/shaders/irradiance.vert",
                            "assets/shaders/irradiance.frag");
    irradianceShader.use();
    irradianceShader.setInt("environmentMap", 0);
    irradianceShader.setMat4("projection", captureProjection);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, ibl.envCubemap);

    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                          irradianceSize, irradianceSize);
    renderToCubemap(irradianceShader, ibl.irradianceMap,
                    captureFBO, irradianceSize);

    // ─── Step 4: Pre-filter environment map (specular IBL) ───────
    const int prefilterSize = 128;
    const int maxMipLevels  = 5;
    ibl.prefilterMap = createCubemap(prefilterSize, GL_LINEAR_MIPMAP_LINEAR);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

    Shader prefilterShader("assets/shaders/prefilter.vert",
                           "assets/shaders/prefilter.frag");
    prefilterShader.use();
    prefilterShader.setInt("environmentMap", 0);
    prefilterShader.setMat4("projection", captureProjection);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, ibl.envCubemap);

    // Render at each mip level with increasing roughness
    for (int mip = 0; mip < maxMipLevels; mip++) {
        int mipSize = static_cast<int>(prefilterSize * std::pow(0.5, mip));
        float roughness = static_cast<float>(mip)
                        / static_cast<float>(maxMipLevels - 1);

        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                              mipSize, mipSize);
        prefilterShader.setFloat("roughness", roughness);
        renderToCubemap(prefilterShader, ibl.prefilterMap,
                        captureFBO, mipSize, mip);
    }

    // ─── Step 5: Generate BRDF integration LUT ───────────────────
    const int brdfSize = 512;
    glGenTextures(1, &ibl.brdfLUT);
    glBindTexture(GL_TEXTURE_2D, ibl.brdfLUT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, brdfSize, brdfSize, 0,
                 GL_RG, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    Shader brdfShader("assets/shaders/brdf.vert", "assets/shaders/brdf.frag");
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, ibl.brdfLUT, 0);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                          brdfSize, brdfSize);
    glViewport(0, 0, brdfSize, brdfSize);
    brdfShader.use();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    renderQuad();  // Full-screen quad

    // ─── Cleanup ─────────────────────────────────────────────────
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &captureFBO);
    glDeleteRenderbuffers(1, &captureRBO);
    glDeleteTextures(1, &hdrTexture);

    return ibl;
}
```

### Adding IBL to the Fragment Shader

With the three IBL textures generated, we add ambient lighting to the PBR fragment shader. This replaces the placeholder `vec3 ambient = vec3(0.03) * albedo * ao` with proper environment lighting.

```glsl
// Add these uniforms to pbr.frag:

uniform samplerCube irradianceMap;
uniform samplerCube prefilterMap;
uniform sampler2D   brdfLUT;
uniform float       maxPrefilterLOD = 4.0;

// Roughness-aware Fresnel for IBL
vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0)
             * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Replace the ambient calculation in main() with:

void main() {
    // ... (all the material sampling and direct lighting from before) ...

    // ─── Indirect lighting (IBL) ─────────────────────────────────
    vec3 F = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
    vec3 kS = F;
    vec3 kD = (1.0 - kS) * (1.0 - metallic);

    // Diffuse IBL: sample irradiance map with the surface normal
    vec3 irradiance = texture(irradianceMap, N).rgb;
    vec3 diffuseIBL = irradiance * albedo;

    // Specular IBL: sample pre-filtered environment with reflection vector
    vec3 R = reflect(-V, N);
    vec3 prefilteredColour = textureLod(prefilterMap, R,
                                        roughness * maxPrefilterLOD).rgb;
    vec2 brdf = texture(brdfLUT, vec2(max(dot(N, V), 0.0), roughness)).rg;
    vec3 specularIBL = prefilteredColour * (F * brdf.x + brdf.y);

    // Combine
    vec3 ambient = (kD * diffuseIBL + specularIBL) * ao;

    vec3 colour = ambient + Lo + emissive;
    FragColor = vec4(colour, 1.0);
}
```

This reuses the cubemap infrastructure from Chapter 24. Change the skybox and the IBL lighting updates to match.

---

## PBRMaterial Component

Following QEngine's ECS rule — components hold data, systems hold behaviour — we define a `PBRMaterial` component.

### src/engine/components/pbr_material.h

```cpp
// In src/engine/components/pbr_material.h

#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>

struct PBRMaterial {
    // Texture IDs (0 = no texture bound, use uniform fallback)
    GLuint albedoMap    = 0;
    GLuint normalMap    = 0;
    GLuint metallicMap  = 0;
    GLuint roughnessMap = 0;
    GLuint aoMap        = 0;
    GLuint emissiveMap  = 0;

    // Uniform fallback values (used when texture ID is 0)
    glm::vec3 albedo    = glm::vec3(1.0f);     // White
    float     metallic  = 0.0f;                  // Dielectric
    float     roughness = 0.5f;                  // Mid-rough
    float     ao        = 1.0f;                  // No occlusion
    glm::vec3 emissive  = glm::vec3(0.0f);      // No emission
};
```

No methods, no constructors with logic, no behaviour. Just data. The render system reads this component and sets shader uniforms accordingly.

### Binding the Material in the Render System

```cpp
// In src/engine/systems/render_system.cpp

// Helper: bind a texture if present, or set the uniform fallback
static void bindMapOrUniform(const Shader& shader, const char* mapName,
                             const char* hasMapName, GLuint texID,
                             int unit) {
    shader.setInt(hasMapName, texID != 0 ? 1 : 0);
    if (texID != 0) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, texID);
        shader.setInt(mapName, unit);
    }
}

void bindPBRMaterial(const Shader& shader, const PBRMaterial& mat) {
    // Material textures: units 0-5
    bindMapOrUniform(shader, "albedoMap",    "hasAlbedoMap",    mat.albedoMap,    0);
    bindMapOrUniform(shader, "normalMap",    "hasNormalMap",    mat.normalMap,    1);
    bindMapOrUniform(shader, "metallicMap",  "hasMetallicMap",  mat.metallicMap,  2);
    bindMapOrUniform(shader, "roughnessMap", "hasRoughnessMap", mat.roughnessMap, 3);
    bindMapOrUniform(shader, "aoMap",        "hasAoMap",        mat.aoMap,        4);
    bindMapOrUniform(shader, "emissiveMap",  "hasEmissiveMap",  mat.emissiveMap,  5);

    // Uniform fallbacks for textures not bound
    if (mat.albedoMap    == 0) shader.setVec3("u_albedo",    mat.albedo);
    if (mat.metallicMap  == 0) shader.setFloat("u_metallic",  mat.metallic);
    if (mat.roughnessMap == 0) shader.setFloat("u_roughness", mat.roughness);
    if (mat.aoMap        == 0) shader.setFloat("u_ao",        mat.ao);
    if (mat.emissiveMap  == 0) shader.setVec3("u_emissive",  mat.emissive);

    // IBL textures: units 6-8
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_CUBE_MAP, iblTextures.irradianceMap);
    shader.setInt("irradianceMap", 6);

    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_CUBE_MAP, iblTextures.prefilterMap);
    shader.setInt("prefilterMap", 7);

    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_2D, iblTextures.brdfLUT);
    shader.setInt("brdfLUT", 8);
}
```

### Using PBRMaterial in the Render System

```cpp
void renderSystem(entt::registry& registry, const Shader& pbrShader) {
    pbrShader.use();
    // Set view/projection uniforms, light uniforms ...

    auto view = registry.view<Transform, Mesh, PBRMaterial>();

    for (auto entity : view) {
        auto& transform = view.get<Transform>(entity);
        auto& mesh      = view.get<Mesh>(entity);
        auto& material  = view.get<PBRMaterial>(entity);

        pbrShader.setMat4("model", transform.getMatrix());
        bindPBRMaterial(pbrShader, material);
        mesh.draw();
    }
}
```

---

## Migration Path

You do not need to rewrite the engine to switch from Phong to PBR. The migration is incremental.

### Step 1: Swap the Shader

Replace the Phong shader with the PBR shader in the render system. Use uniform fallback values for all material properties:

```cpp
// Before (Phong):
auto view = registry.view<Transform, Mesh, Material>();

// After (PBR):
auto view = registry.view<Transform, Mesh, PBRMaterial>();
```

Entities that still have the old `Material` component will not be rendered by the new system. Add `PBRMaterial` to them gradually.

### Step 2: Convert Existing Textures

Old diffuse textures become albedo maps — the shader converts them from sRGB to linear with `pow(colour, vec3(2.2))`. Normal maps from Chapter 35 work unchanged (they should always be loaded in linear space, not sRGB).

### Step 3: Start with Uniform Values, Add Textures Later

```cpp
auto& mat = registry.emplace<PBRMaterial>(entity);
mat.albedoMap  = existingDiffuseTexture;  // Reuse the old diffuse map
mat.normalMap  = existingNormalMap;        // From Ch 35
mat.metallic   = 0.0f;                    // Non-metal
mat.roughness  = 0.7f;                    // Somewhat rough
mat.ao         = 1.0f;                    // No AO
```

This already looks better than Phong because of energy conservation and correct Fresnel. As you source PBR texture sets, assign metallic/roughness/AO maps incrementally. Use `false` for the sRGB parameter when loading these data textures.

### Step 4: Add HDR and IBL

Switch the FBO to `GL_RGBA16F`, add the tone mapping pass, then generate IBL textures from an HDR environment map. Each step is independent and testable in isolation.

---

## C++ Concepts

### Linear Colour Space vs sRGB

Monitors display colours in sRGB, a non-linear colour space with a gamma curve of approximately 2.2. Lighting math must happen in **linear space** where the relationship between values and brightness is proportional. If you skip the conversion, colours appear washed out and light falloff looks wrong.

The pipeline: (1) convert sRGB inputs to linear with `pow(colour, vec3(2.2))` when sampling albedo/emissive textures, (2) do all lighting math in linear space, (3) convert back to sRGB with `pow(colour, vec3(1.0/2.2))` in the tone mapping pass. Data textures (normal, metallic, roughness, AO) encode numeric values, not colours — never apply gamma correction to these.

### Floating-Point Framebuffers

Standard `GL_RGBA8` framebuffers clamp everything to [0, 1], destroying HDR information. `GL_RGBA16F` stores IEEE half-floats: values can far exceed 1.0 with no clamping, at double the memory cost (8 bytes/pixel vs 4). `GL_RGBA32F` offers full precision at 16 bytes/pixel but is overkill for rendering. `GL_RGBA16F` is the standard choice for PBR.

### Pre-Computation Techniques

IBL exemplifies a core engine pattern: trade offline computation for runtime performance. The irradiance map, pre-filtered environment map, and BRDF LUT are expensive integrals computed once (at build time or level load) and sampled cheaply at runtime with a single texture lookup. The same principle appears in shadow maps, light probes, and baked AO. The general rule: if a computation is expensive and its inputs do not change every frame, pre-compute it into a texture.

---

## What's Next — Series Complete

This chapter completes the QEngine tutorial series.

Over 44 chapters, we built a complete 3D game engine from the ground up — from a blank window to a feature set that rivals the foundations of commercial engines:

- **Foundation (Ch 1-6):** Window, OpenGL context, game loop, shaders, textures, camera
- **ECS Architecture (Ch 8-12):** EnTT, component/system split, transforms, hierarchies
- **Lighting & Rendering (Ch 7, 28-29, 35, 44):** Phong, post-processing, shadows, normal maps, PBR + HDR + IBL
- **Physics & Collision (Ch 9-11):** Detection, resolution, raycasting
- **Gameplay (Ch 13-20):** Movement, weapons, projectiles, enemies, health, pickups, doors, triggers
- **UI & Menus (Ch 21-23, 27, 30):** State machine, menus, save/load, dev console, fonts
- **Visual Effects (Ch 24-26, 31, 38-39):** Skybox, view models, decals, instancing, water
- **Animation (Ch 33, 40-43):** Skeletal animation, events, ragdoll, layers, IK
- **Advanced (Ch 32, 34, 36-37):** Frustum culling, level transitions, model loading, pathfinding

The engine follows two core principles throughout: the ECS architecture (components have no behaviour, systems have no state) and RAII resource management (GPU resources are owned by C++ objects with proper move semantics). Every system is a free function. Every component is a plain data struct. The result is an engine that is modular, testable, and straightforward to extend.

From here, QEngine is yours. Add volumetric fog, screen-space reflections, global illumination, multiplayer networking, or a level editor. The foundation is solid. Build something great.
