# Chapter 39: Water & Liquid Rendering

## What You'll Learn
- Why water is one of the most visually rewarding features in a first-person game
- Building simple animated water with UV scrolling and alpha blending
- Designing an ECS `WaterPlane` component that stores all water parameters as pure data
- Displacing vertices with overlapping sine waves to create surface motion
- Rendering planar reflections by flipping the camera below the water plane
- Rendering refraction with a clipping plane and distortion via a dudv map
- Blending reflection and refraction with the Fresnel effect (Schlick approximation)
- Writing a complete water fragment shader that combines all techniques
- Detecting when the camera is underwater and applying post-processing effects
- Building swimming mechanics with buoyancy, oxygen, and trigger volumes
- Understanding the full render order when water is in the pipeline
- `glm::reflect` and the mathematics of reflection vectors

---

## Water in Games

Water has been a defining feature of first-person games since the 1990s. Quake had murky brown pools you could swim through. Half-Life had reactive water surfaces that rippled when you shot them. Half-Life 2 introduced real-time reflections and refractions that set a new standard.

What makes water visually interesting is that it combines several rendering techniques into one surface: animation (waves and ripples), reflection (sky and geometry mirrored in the surface), refraction (distorted view of what is below), transparency that changes with viewing angle, and colour tint from light absorption. We will build these up in layers, starting with a simple scrolling texture, then adding reflection, refraction, and Fresnel blending.

---

## Simple Water — Animated UV Scrolling

The cheapest approach: place a flat quad at the water height, scroll its UV coordinates over time, and render it semi-transparent with a blue-green tint.

```cpp
// In src/engine/renderer/water_mesh.h
#pragma once
#include <glad/glad.h>

struct WaterMesh {
    GLuint vao = 0;
    GLuint vbo = 0;
    int vertexCount = 6;
};

inline WaterMesh createWaterMesh(float size) {
    float vertices[] = {
        // position (x, y, z)       // uv (u, v)
        -size, 0.0f, -size,         0.0f, 0.0f,
         size, 0.0f, -size,         1.0f, 0.0f,
         size, 0.0f,  size,         1.0f, 1.0f,
        -size, 0.0f, -size,         0.0f, 0.0f,
         size, 0.0f,  size,         1.0f, 1.0f,
        -size, 0.0f,  size,         0.0f, 1.0f,
    };

    WaterMesh mesh;
    glGenVertexArrays(1, &mesh.vao);
    glGenBuffers(1, &mesh.vbo);
    glBindVertexArray(mesh.vao);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    return mesh;
}
```

### Simple Water Shaders

The vertex shader scrolls UVs by adding a time-based offset. The fragment shader samples a water texture, applies a colour tint, and outputs with partial transparency.

```glsl
// In assets/shaders/water_simple.vert
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aUV;

uniform mat4 uProjection;
uniform mat4 uView;
uniform mat4 uModel;
uniform float uTime;
uniform float uScrollSpeed;
uniform float uUVScale;

out vec2 vUV;

void main() {
    vUV = aUV * uUVScale + vec2(uScrollSpeed * uTime, uScrollSpeed * uTime * 0.7);
    gl_Position = uProjection * uView * uModel * vec4(aPos, 1.0);
}
```

```glsl
// In assets/shaders/water_simple.frag
#version 330 core
in vec2 vUV;

uniform sampler2D uWaterTexture;
uniform vec3 uWaterColour;
uniform float uAlpha;

out vec4 FragColour;

void main() {
    vec4 texColour = texture(uWaterTexture, vUV);
    vec3 tinted = mix(texColour.rgb, uWaterColour, 0.5);
    FragColour = vec4(tinted, uAlpha);
}
```

This is very cheap — one quad, one texture sample, no extra render passes. For a retro aesthetic, it is often enough on its own.

---

## The Water Component

Following QEngine's ECS rules: components have no behaviour, systems have no state. The `WaterPlane` component is pure data describing a single water surface.

```cpp
// In src/engine/ecs/components.h

struct WaterPlane {
    float height;                  // Y position of water surface
    float uvScale = 8.0f;         // Tiling of water texture
    float scrollSpeed = 0.03f;    // UV scroll speed
    float waveAmplitude = 0.1f;   // Vertex displacement height
    float waveFrequency = 2.0f;   // Wave oscillation frequency
    glm::vec3 colour = glm::vec3(0.0f, 0.3f, 0.5f);  // Blue-green tint
    float alpha = 0.6f;           // Transparency (1.0 = opaque)
    bool reflectionEnabled = true;
    bool refractionEnabled = true;
};
```

A murky swamp might have `alpha = 0.85f`, low `uvScale`, and `reflectionEnabled = false`. A clear lake might have `alpha = 0.4f` with both reflection and refraction enabled. To create a water entity:

```cpp
auto water = registry.create();
registry.emplace<WaterPlane>(water, WaterPlane{ .height = -2.0f });
registry.emplace<Transform>(water, Transform{ .position = glm::vec3(0.0f, -2.0f, 0.0f) });
```

---

## Vertex Displacement Waves

A flat plane scrolling its UVs only fakes motion in the texture. For the surface itself to move, we displace vertices in the vertex shader using sine waves. The trick is layering multiple sine waves at different frequencies, amplitudes, and directions. A single sine wave looks mechanical — three overlapping waves produce natural motion.

```glsl
// In assets/shaders/water.vert
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aUV;

uniform mat4 uProjection;
uniform mat4 uView;
uniform mat4 uModel;
uniform float uTime;
uniform float uScrollSpeed;
uniform float uUVScale;
uniform float uWaveAmplitude;
uniform float uWaveFrequency;
uniform vec4 uClipPlane;

out vec2 vUV;
out vec3 vWorldPos;
out vec3 vNormal;
out vec4 vClipSpace;

float waveHeight(vec2 xz, float time) {
    float h = 0.0;
    h += sin(xz.x * uWaveFrequency + time * 1.2) * uWaveAmplitude;
    h += sin((xz.x * 0.7 + xz.y * 0.7) * uWaveFrequency * 1.3 + time * 0.9)
         * uWaveAmplitude * 0.5;
    h += sin(xz.y * uWaveFrequency * 0.8 + time * 1.5) * uWaveAmplitude * 0.3;
    return h;
}

void main() {
    vec4 worldPos4 = uModel * vec4(aPos, 1.0);
    vec2 xz = worldPos4.xz;

    // Displace Y by the wave function
    worldPos4.y += waveHeight(xz, uTime);

    // Compute normal from partial derivatives (finite differences)
    float eps = 0.1;
    float hL = waveHeight(xz - vec2(eps, 0.0), uTime);
    float hR = waveHeight(xz + vec2(eps, 0.0), uTime);
    float hD = waveHeight(xz - vec2(0.0, eps), uTime);
    float hU = waveHeight(xz + vec2(0.0, eps), uTime);
    vec3 normal = normalize(vec3(hL - hR, 2.0 * eps, hD - hU));

    vUV = aUV * uUVScale + vec2(uScrollSpeed * uTime, uScrollSpeed * uTime * 0.7);
    vWorldPos = worldPos4.xyz;
    vNormal = normal;
    vClipSpace = uProjection * uView * worldPos4;
    gl_ClipDistance[0] = dot(worldPos4, uClipPlane);
    gl_Position = vClipSpace;
}
```

The normal is computed by sampling wave height at four nearby points and constructing a slope vector. This gives correct lighting on the moving surface without recomputing a mesh normal buffer every frame.

---

## Planar Reflection

This is where water goes from "decent" to "impressive". We render the entire scene from a camera reflected below the water plane, capture the result into an FBO (reusing the `Framebuffer` class from Ch 28), and map it onto the water surface.

```
        Real Camera (C)
              |  \
              |   \  view direction
              |    \
              |     v
    ~~~~~~~~~~|~~~~~~~~~~~~~~~~~  Water Plane (y = waterHeight)
              |     ^
              |    /
              |   /  reflected view direction
              |  /
        Reflected Camera (C')

    C' is the mirror of C across the water plane.
    Position: same X, Z. Y reflected: y' = 2 * waterHeight - cameraY
    Pitch is negated. Yaw stays the same.
```

We use `glEnable(GL_CLIP_DISTANCE0)` to discard geometry below the water in the reflection pass. Each scene vertex shader needs one extra line: `gl_ClipDistance[0] = dot(worldPos, uClipPlane);`

```cpp
// In src/engine/renderer/water_renderer.h
#pragma once
#include "engine/renderer/framebuffer.h"
#include "engine/renderer/shader.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <entt/entt.hpp>

inline glm::mat4 computeReflectionViewMatrix(const glm::vec3& cameraPos,
                                              float cameraPitch,
                                              float cameraYaw,
                                              float waterHeight)
{
    glm::vec3 reflectedPos = cameraPos;
    reflectedPos.y = 2.0f * waterHeight - cameraPos.y;
    float reflectedPitch = -cameraPitch;

    glm::vec3 front;
    front.x = cos(glm::radians(reflectedPitch)) * cos(glm::radians(cameraYaw));
    front.y = sin(glm::radians(reflectedPitch));
    front.z = cos(glm::radians(reflectedPitch)) * sin(glm::radians(cameraYaw));
    front = glm::normalize(front);

    return glm::lookAt(reflectedPos, reflectedPos + front, glm::vec3(0, 1, 0));
}

inline void renderReflectionPass(Framebuffer& reflectionFBO,
                                 const glm::mat4& projection,
                                 const glm::vec3& cameraPos,
                                 float cameraPitch, float cameraYaw,
                                 float waterHeight,
                                 Shader& sceneShader, Shader& skyboxShader)
{
    reflectionFBO.bind();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glm::mat4 reflView = computeReflectionViewMatrix(
        cameraPos, cameraPitch, cameraYaw, waterHeight);

    glEnable(GL_CLIP_DISTANCE0);

    // Skybox with reflected view (strip translation — see Ch 24)
    skyboxShader.use();
    skyboxShader.setMat4("uView", glm::mat4(glm::mat3(reflView)));
    skyboxShader.setMat4("uProjection", projection);
    // ... draw skybox ...

    // Scene geometry with clip plane: keep only above water
    sceneShader.use();
    sceneShader.setMat4("uView", reflView);
    sceneShader.setMat4("uProjection", projection);
    sceneShader.setVec4("uClipPlane", glm::vec4(0, 1, 0, -waterHeight));
    // ... draw scene entities ...

    glDisable(GL_CLIP_DISTANCE0);
    Framebuffer::unbind();
}
```

When `GL_CLIP_DISTANCE0` is disabled (normal rendering), the clip distance is ignored. When enabled, any vertex with a negative clip distance is discarded.

---

## Refraction

Refraction is what you see *through* the water — the submerged world, distorted and tinted. The clip plane is inverted: we render only what is **below** the water surface.

```cpp
// In src/engine/renderer/water_renderer.h (continued)

inline void renderRefractionPass(Framebuffer& refractionFBO,
                                  const glm::mat4& projection,
                                  const glm::mat4& viewMatrix,
                                  float waterHeight,
                                  Shader& sceneShader)
{
    refractionFBO.bind();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_CLIP_DISTANCE0);

    sceneShader.use();
    sceneShader.setMat4("uView", viewMatrix);
    sceneShader.setMat4("uProjection", projection);
    sceneShader.setVec4("uClipPlane", glm::vec4(0, -1, 0, waterHeight));
    // ... draw scene geometry ...

    glDisable(GL_CLIP_DISTANCE0);
    Framebuffer::unbind();
}
```

### The DuDv Map

A **dudv map** is a texture that stores 2D offset values in its red and green channels. When sampling the refraction or reflection texture, we add these offsets to the UV coordinates, creating the wobbly distortion that makes underwater objects look like they are seen through moving liquid.

```
DuDv Map:
┌──────────────────────────────┐
│  Each pixel stores (dU, dV)   │
│  in R and G channels.         │
│  Values around 0.5 = no       │
│  distortion. Above/below      │
│  0.5 = push UVs.              │
└──────────────────────────────┘
```

---

## Fresnel Effect

The Fresnel effect determines how much reflection versus refraction you see. It depends on the angle between your view direction and the water normal.

```
    Looking straight down:              Looking at a glancing angle:

         Eye                                Eye ----___
          |                                             ---___
          |  (steep angle)                                    ----> view
          v
    ~~~~~~~~~~~~~~                     ~~~~~~~~~~~~~~~~~~~~~~~~

    Mostly REFRACTION                   Mostly REFLECTION
    (see the riverbed)                  (see the sky)
    Fresnel ~ 0.1                       Fresnel ~ 0.9
```

The **Schlick approximation** gives us a good result with a simple formula:

```
fresnel = pow(1.0 - dot(viewDir, normal), fresnelPower)
```

A `fresnelPower` of 2.0 to 5.0 works well for water.

---

## The Water Fragment Shader

This combines everything: distortion, reflection, refraction, Fresnel blending, and specular highlights.

```glsl
// In assets/shaders/water.frag
#version 330 core

in vec2 vUV;
in vec3 vWorldPos;
in vec3 vNormal;
in vec4 vClipSpace;

uniform sampler2D uReflectionTexture;
uniform sampler2D uRefractionTexture;
uniform sampler2D uDudvMap;
uniform sampler2D uNormalMap;
uniform float uTime;
uniform float uScrollSpeed;
uniform vec3 uCameraPos;
uniform vec3 uLightDir;
uniform vec3 uWaterColour;
uniform float uAlpha;
uniform float uFresnelPower;
uniform float uDistortionStrength;

out vec4 FragColour;

void main() {
    // Step 1: clip-space to screen-space UVs for FBO sampling
    vec2 ndc = vClipSpace.xy / vClipSpace.w;
    vec2 screenUV = ndc * 0.5 + 0.5;
    vec2 reflectUV = vec2(screenUV.x, 1.0 - screenUV.y);
    vec2 refractUV = screenUV;

    // Step 2: animated dudv distortion
    vec2 distortedUV = texture(uDudvMap, vec2(vUV.x + uTime * uScrollSpeed,
                                               vUV.y)).rg * 0.1;
    distortedUV = vUV + vec2(distortedUV.x, distortedUV.y + uTime * uScrollSpeed);
    vec2 dudv = (texture(uDudvMap, distortedUV).rg * 2.0 - 1.0)
                * uDistortionStrength;

    reflectUV = clamp(reflectUV + dudv, 0.001, 0.999);
    refractUV = clamp(refractUV + dudv, 0.001, 0.999);

    // Step 3: sample reflection and refraction
    vec3 reflectionColour = texture(uReflectionTexture, reflectUV).rgb;
    vec3 refractionColour = texture(uRefractionTexture, refractUV).rgb;
    refractionColour = mix(refractionColour, uWaterColour, 0.3);

    // Step 4: normal map for surface detail
    vec3 normalMap = texture(uNormalMap, distortedUV).rgb;
    normalMap = normalize(vec3(normalMap.r * 2.0 - 1.0,
                               normalMap.b * 3.0,
                               normalMap.g * 2.0 - 1.0));

    // Step 5: Fresnel
    vec3 viewDir = normalize(uCameraPos - vWorldPos);
    float fresnel = pow(1.0 - clamp(dot(viewDir, vec3(0.0, 1.0, 0.0)), 0.0, 1.0),
                        uFresnelPower);

    // Step 6: blend
    vec3 colour = mix(refractionColour, reflectionColour, fresnel);

    // Step 7: specular highlight (sun glint)
    vec3 reflectedLight = reflect(-normalize(uLightDir), normalMap);
    float specular = pow(max(dot(reflectedLight, viewDir), 0.0), 64.0);
    colour += vec3(1.0) * specular * 0.6;

    // Step 8: subtle tint
    colour = mix(colour, uWaterColour, 0.15);

    FragColour = vec4(colour, uAlpha);
}
```

---

## Underwater Effects

When the camera dips below the water surface, the world should look blue-green, murky, and distorted.

### Detecting Submersion

```cpp
// In src/engine/systems/water_system.h

inline std::optional<float> getCameraWaterDepth(entt::registry& registry,
                                                 const glm::vec3& cameraPos)
{
    std::optional<float> result;
    auto view = registry.view<WaterPlane>();
    for (auto entity : view) {
        const auto& water = view.get<WaterPlane>(entity);
        if (cameraPos.y < water.height) {
            if (!result || water.height < *result)
                result = water.height;
        }
    }
    return result;
}
```

### Underwater Post-Processing

We reuse the post-processing pipeline from Chapter 28, swapping in an underwater shader when submerged.

```glsl
// In assets/shaders/underwater_post.frag
#version 330 core
in vec2 vUV;

uniform sampler2D uSceneTexture;
uniform float uTime;
uniform float uDepth;

out vec4 FragColour;

void main() {
    vec2 uv = vUV;
    uv.x += sin(uv.y * 20.0 + uTime * 3.0) * 0.003;
    uv.y += cos(uv.x * 15.0 + uTime * 2.5) * 0.003;

    vec3 colour = texture(uSceneTexture, uv).rgb;

    vec3 waterTint = vec3(0.0, 0.3, 0.5);
    float tintStrength = clamp(uDepth * 0.2, 0.1, 0.6);
    colour = mix(colour, waterTint, tintStrength);

    float fogFactor = clamp(uDepth * 0.1, 0.0, 0.5);
    colour = mix(colour, waterTint * 0.3, fogFactor);
    colour *= 0.85;

    FragColour = vec4(colour, 1.0);
}
```

```
Underwater Rendering Pipeline:

    Scene renders into FBO
                |
                v
    ┌───────────────────────┐
    │ Is camera underwater? │
    └───────┬───────┬───────┘
         No │       │ Yes
            v       v
    Normal post    Underwater post
    processing     (tint + fog + wobble)
            │       │
            v       v
         Screen  Screen
```

```cpp
// In your main render loop
inline void applyPostProcessing(entt::registry& registry,
                                 Framebuffer& sceneFBO,
                                 Shader& normalPost,
                                 Shader& underwaterPost,
                                 const glm::vec3& cameraPos,
                                 float time)
{
    auto depth = getCameraWaterDepth(registry, cameraPos);
    if (depth) {
        underwaterPost.use();
        underwaterPost.setFloat("uTime", time);
        underwaterPost.setFloat("uDepth", *depth - cameraPos.y);
    } else {
        normalPost.use();
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneFBO.getColourTexture());
    // Draw full-screen quad
}
```

You could also muffle audio when underwater (reduce volume, apply a low-pass filter concept from Ch 16) for a more immersive effect.

---

## Swimming Mechanics

Water is not just visual. The player needs to interact with it: floating, swimming, running out of air.

### Components

```cpp
// In src/engine/ecs/components.h

struct WaterVolume {
    float surfaceHeight;       // Y of the water surface
    float floorHeight;         // Y of the bottom
};

struct InWater {
    float depth;               // How deep the player is below the surface
    float oxygenTimer;         // Seconds of air remaining
    float maxOxygen = 30.0f;   // Total breath capacity
};
```

`InWater` is attached to the player when they enter a `WaterVolume` trigger (Ch 11) and removed when they leave. Presence of the component indicates state — the standard ECS pattern.

### The Water System

A free function with no stored state:

```cpp
// In src/engine/systems/water_system.h
#pragma once
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include "engine/ecs/components.h"

inline void updateWaterSystem(entt::registry& registry, float dt)
{
    auto view = registry.view<InWater, Transform, Velocity>();
    for (auto entity : view) {
        auto& inWater  = view.get<InWater>(entity);
        auto& velocity = view.get<Velocity>(entity);

        // Buoyancy: upward force, drag on all axes
        velocity.y += 4.0f * dt;
        velocity.x *= 0.92f;
        velocity.y *= 0.92f;
        velocity.z *= 0.92f;

        // Oxygen: deplete when head is submerged, replenish when above
        bool headSubmerged = (inWater.depth > 1.5f);
        if (headSubmerged) {
            inWater.oxygenTimer -= dt;
            if (inWater.oxygenTimer <= 0.0f) {
                inWater.oxygenTimer = 0.0f;
                if (registry.any_of<Health>(entity))
                    registry.get<Health>(entity).current -= 10.0f * dt;
            }
        } else {
            inWater.oxygenTimer = glm::min(inWater.oxygenTimer + dt * 5.0f,
                                            inWater.maxOxygen);
        }
    }
}

inline void onEnterWater(entt::registry& registry, entt::entity player,
                          float surfaceHeight)
{
    if (!registry.any_of<InWater>(player)) {
        float depth = surfaceHeight - registry.get<Transform>(player).position.y;
        registry.emplace<InWater>(player, InWater{
            .depth = depth, .oxygenTimer = 30.0f, .maxOxygen = 30.0f
        });
    }
}

inline void onExitWater(entt::registry& registry, entt::entity player)
{
    registry.remove<InWater>(player);
}
```

The movement system checks for `InWater` and reduces speed:

```cpp
float moveSpeed = baseSpeed;
if (registry.any_of<InWater>(entity))
    moveSpeed *= 0.5f;
```

---

## Render Order

With water in the pipeline, the reflection and refraction passes must happen before the main scene draws the water surface, because the water shader needs those textures as input.

```
Complete Render Order with Water:

     1.  Shadow pass              (depth-only, from light's perspective)
     2.  Reflection pass          (scene above water, flipped camera, clip plane)
     3.  Refraction pass          (scene below water, normal camera, clip plane)
     4.  Bind scene FBO
     5.  Skybox                   (rendered first at infinite depth)
     6.  Opaque geometry          (level, enemies, props)
     7.  Water surface            (alpha blended, binds reflection + refraction textures)
     8.  Decals                   (Ch 31, alpha blended onto surfaces)
     9.  Transparent / particles  (sorted back-to-front)
    10.  View model               (Ch 25, rendered on top)
    11.  Unbind scene FBO
    12.  Post-processing          (+ underwater effects if camera submerged)
    13.  HUD                      (health, ammo, oxygen bar if swimming)
```

Steps 2 and 3 each render the scene an extra time, effectively tripling draw calls. Mitigate the cost by rendering reflection/refraction at half resolution, skipping expensive effects in the reflection pass, or updating reflection every other frame:

```cpp
int halfWidth  = windowWidth / 2;
int halfHeight = windowHeight / 2;
Framebuffer reflectionFBO(halfWidth, halfHeight);
Framebuffer refractionFBO(halfWidth, halfHeight);
```

---

## C++ Concept: `glm::reflect` and Reflection Math

Throughout this chapter, we reflected a camera across a water plane. The GLM library provides `glm::reflect` for this, and the underlying maths is useful far beyond water.

### The Reflection Formula

Given an **incident** vector `I` and a unit surface **normal** `N`, the reflected vector is:

```
R = I - 2 * dot(I, N) * N
```

```
        I (incident)       N (normal)       R (reflected)
         \                 |                /
          \                |               /
           \               |              /
            v              |             ^
    ─────────────────────────────────────────── Surface
```

The `dot(I, N)` measures how much of `I` points into the surface. Subtracting twice that component along `N` flips the perpendicular part while preserving the parallel part — exactly what a mirror does.

### Practical Examples

**Water reflection camera** — reflect the forward vector across `(0, 1, 0)`:

```cpp
glm::vec3 cameraForward = glm::vec3(0.3f, -0.5f, 0.8f);
glm::vec3 waterNormal   = glm::vec3(0.0f,  1.0f, 0.0f);
glm::vec3 reflected = glm::reflect(cameraForward, waterNormal);
// Result: (0.3, 0.5, 0.8) — Y flipped, X and Z unchanged
```

**Projectile bounce** — a bullet ricochets off a wall:

```cpp
glm::vec3 bulletDir  = glm::vec3(0.7f, 0.0f, -0.7f);
glm::vec3 wallNormal = glm::vec3(0.0f, 0.0f,  1.0f);
glm::vec3 bounceDir  = glm::reflect(bulletDir, wallNormal);
// Result: (0.7, 0.0, 0.7) — Z flipped, bullet bounces back
```

**Specular lighting** — the GLSL `reflect` in our water shader:

```glsl
vec3 reflectedLight = reflect(-lightDir, surfaceNormal);
float specular = pow(max(dot(reflectedLight, viewDir), 0.0), shininess);
```

The formula `R = I - 2 * dot(I, N) * N` appears everywhere in graphics: Phong specular, environment mapping, mirror rendering, physics bounces, and water. Once you internalise it, you will see it everywhere.

---

## Where to Go From Here

This is the final chapter of the QEngine tutorial series.

Over 40 chapters (0 through 39), you have built a complete first-person game engine from nothing. You started with an empty window and OpenGL context. You added a camera, loaded textures, built an ECS architecture with EnTT, implemented physics, collision, audio, AI, weapons, HUD, save/load, shadows, skeletal animation, post-processing, and now — water with real-time reflections and Fresnel blending. That is a real engine. It renders, it simulates, it plays.

But an engine is not a game. The hardest and most rewarding step is still ahead: building something with it. Take QEngine and make a level — then two, then five. Add a story. Add a boss at the end. Playtest it. Ship it. That is where the real learning happens, because a finished game teaches you things that no tutorial can.

If you want to keep expanding your skills, there are three companion roadmaps that pick up where this series leaves off:

- **TrenchBroom Level Design** — learn to build professional-quality levels with the TrenchBroom editor and import them into QEngine
- **Top-Down Shooter** — apply everything you know to a different genre, with a new camera perspective and different design challenges
- **Multiplayer Infrastructure** — add networking, client-server architecture, and real-time synchronisation to your engine

Each roadmap assumes you have completed this series and builds on the same foundations.

Thank you for following along. Now go build a game.
