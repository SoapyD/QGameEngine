# Chapter 29: Shadow Mapping

## What You'll Learn
- The two-pass shadow mapping algorithm: depth from light, then lit/shadow test
- Creating a depth-only FBO (the shadow map) with RAII resource management
- Computing the light space matrix for directional lights using orthographic projection
- Writing the depth pass shader (minimal vertex-only work)
- Sampling the shadow map in the main fragment shader to determine shadow
- Fixing shadow acne, peter-panning, and hard edges (PCF)
- ECS integration: `ShadowCaster` component and `shadowMapSystem` free function
- Where the shadow pass fits in QEngine's render loop

---

## How Shadow Mapping Works

Shadow mapping is a **two-pass algorithm**:

**Pass 1 -- Depth from the light.** Place a virtual camera at the light, render the scene into a depth-only FBO. Each texel stores the depth of the closest surface the light can see.

**Pass 2 -- Render normally.** For each fragment, transform its world position into light space. Compare its depth against the shadow map. If the fragment is farther than what the light recorded, something blocks the light -- the fragment is in shadow.

```
Pass 1: Render depth from light
                                          Shadow Map (depth texture)
    Light ----+                           ┌───────────────────┐
              |   frustum                 │ 0.3  0.3  0.8  0.9│
              |  /       \                │ 0.3  0.4  0.8  0.9│
              | /  scene  \               │ 0.7  0.7  0.7  0.9│
              |/___________\              │ 0.9  0.9  0.9  0.9│
                                          └───────────────────┘

Pass 2: Render scene, test against shadow map

    Camera -----> Fragment at world pos P
                  1. Transform P into light space  -> lightSpacePos
                  2. Perspective divide             -> projCoords (NDC)
                  3. Sample shadow map at projCoords.xy -> closestDepth
                  4. Compare: projCoords.z > closestDepth?
                     YES -> in shadow    NO -> lit
```

```
Depth comparison:

    Light
      |     depth = 0.3 (stored in shadow map)
      v       v
    ┌─────────────┐
    │   Box (A)   │  depth 0.3 -- lit (0.3 is NOT > 0.3)
    └─────────────┘
              :
              :  depth = 0.7
              v       v
        ┌─────────────────┐
        │   Floor (B)     │  depth 0.7 -- shadow (0.7 > 0.3)
        └─────────────────┘
```

---

## Shadow Map FBO

In Chapter 28 you built FBOs with colour and depth attachments. A shadow map only needs a **depth attachment** -- no colour buffer at all.

### src/engine/renderer/shadow_map.h

```cpp
// In src/engine/renderer/shadow_map.h
#pragma once
#include <glad/glad.h>

class ShadowMap {
public:
    ShadowMap() = default;
    ~ShadowMap();

    ShadowMap(const ShadowMap&) = delete;
    ShadowMap& operator=(const ShadowMap&) = delete;
    ShadowMap(ShadowMap&& other) noexcept;
    ShadowMap& operator=(ShadowMap&& other) noexcept;

    bool init(int resolution);
    void bindForWriting() const;
    void bindForReading(GLenum textureUnit) const;

    GLuint getDepthTexture() const { return m_depthTexture; }
    int    getResolution()   const { return m_resolution; }

private:
    GLuint m_fbo          = 0;
    GLuint m_depthTexture = 0;
    int    m_resolution   = 0;
    void cleanup();
};
```

### src/engine/renderer/shadow_map.cpp

```cpp
// In src/engine/renderer/shadow_map.cpp
#include "engine/renderer/shadow_map.h"
#include <iostream>

ShadowMap::~ShadowMap() { cleanup(); }

ShadowMap::ShadowMap(ShadowMap&& other) noexcept
    : m_fbo(other.m_fbo), m_depthTexture(other.m_depthTexture),
      m_resolution(other.m_resolution) {
    other.m_fbo = 0; other.m_depthTexture = 0; other.m_resolution = 0;
}

ShadowMap& ShadowMap::operator=(ShadowMap&& other) noexcept {
    if (this != &other) {
        cleanup();
        m_fbo = other.m_fbo; m_depthTexture = other.m_depthTexture;
        m_resolution = other.m_resolution;
        other.m_fbo = 0; other.m_depthTexture = 0; other.m_resolution = 0;
    }
    return *this;
}

void ShadowMap::cleanup() {
    if (m_depthTexture) { glDeleteTextures(1, &m_depthTexture); m_depthTexture = 0; }
    if (m_fbo)          { glDeleteFramebuffers(1, &m_fbo); m_fbo = 0; }
    m_resolution = 0;
}

bool ShadowMap::init(int resolution) {
    cleanup();
    m_resolution = resolution;

    // ── Depth texture ───────────────────────────────────────────
    glGenTextures(1, &m_depthTexture);
    glBindTexture(GL_TEXTURE_2D, m_depthTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT,
                 resolution, resolution, 0,
                 GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // Fragments outside the map sample 1.0 = fully lit
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float borderColour[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColour);

    // ── FBO (depth-only, no colour) ─────────────────────────────
    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                           GL_TEXTURE_2D, m_depthTexture, 0);
    glDrawBuffer(GL_NONE);   // No colour attachment
    glReadBuffer(GL_NONE);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "ERROR: Shadow map FBO is not complete!" << std::endl;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        cleanup();
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

void ShadowMap::bindForWriting() const {
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_resolution, m_resolution);
    glClear(GL_DEPTH_BUFFER_BIT);
}

void ShadowMap::bindForReading(GLenum textureUnit) const {
    glActiveTexture(textureUnit);
    glBindTexture(GL_TEXTURE_2D, m_depthTexture);
}
```

**GL_NONE** tells OpenGL there is no colour attachment -- without this call the FBO is incomplete. **GL_CLAMP_TO_BORDER** with depth 1.0 ensures everything outside the shadow map is lit. 2048x2048 is a solid default resolution.

---

## Light Space Matrix

For a directional light (parallel rays), we use an orthographic projection:

```
    Directional light rays (parallel)
    ↓   ↓   ↓   ↓   ↓   ↓   ↓   ↓
    ┌───────────────────────────────┐
    │     Orthographic frustum      │  rectangular box, not a pyramid
    │   ┌───┐        ┌──┐          │
    │   │ A │   ┌──┐ │B │          │
    │   └───┘   │C │ └──┘          │
    │           └──┘               │
    └───────────────────────────────┘
    near plane                far plane
```

```cpp
// Computing the light space matrix
glm::mat4 computeLightSpaceMatrix(const glm::vec3& direction,
                                   float orthoSize,
                                   float nearPlane, float farPlane) {
    float distance = (farPlane - nearPlane) * 0.5f;
    glm::vec3 lightPos = -glm::normalize(direction) * distance;

    // Handle edge case: light pointing straight down
    glm::vec3 up = (std::abs(glm::dot(glm::normalize(direction),
                    glm::vec3(0.0f, 1.0f, 0.0f))) > 0.99f)
                   ? glm::vec3(0.0f, 0.0f, 1.0f)
                   : glm::vec3(0.0f, 1.0f, 0.0f);

    glm::mat4 lightView = glm::lookAt(lightPos, glm::vec3(0.0f), up);

    glm::mat4 lightProjection = glm::ortho(
        -orthoSize, orthoSize,     // Left, Right
        -orthoSize, orthoSize,     // Bottom, Top
        nearPlane, farPlane        // Near, Far
    );

    return lightProjection * lightView;
}
```

`orthoSize` controls coverage: too small and distant shadows vanish, too large and shadow quality drops. 30--50 works for room-sized areas.

---

## Depth Pass Shader

The shadow pass only writes depth -- the fragment shader is empty:

```glsl
// In assets/shaders/shadow_depth.vert
#version 460 core
layout (location = 0) in vec3 aPos;

uniform mat4 lightSpaceMatrix;
uniform mat4 model;

void main() {
    gl_Position = lightSpaceMatrix * model * vec4(aPos, 1.0);
}
```

```glsl
// In assets/shaders/shadow_depth.frag
#version 460 core

void main() {
    // Depth is written automatically by the GPU.
    // OpenGL requires a fragment shader, but it does no work.
}
```

---

## Sampling the Shadow Map

In the main vertex shader, pass each fragment's position in light space:

```glsl
// In assets/shaders/lit.vert (relevant additions)
#version 460 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoords;
out vec4 FragPosLightSpace;

uniform mat4 model, view, projection, lightSpaceMatrix;

void main() {
    vec4 worldPos = model * vec4(aPos, 1.0);
    FragPos       = worldPos.xyz;
    Normal        = mat3(transpose(inverse(model))) * aNormal;
    TexCoords     = aTexCoords;
    FragPosLightSpace = lightSpaceMatrix * worldPos;
    gl_Position   = projection * view * worldPos;
}
```

In the fragment shader, compare against the shadow map:

```glsl
// In assets/shaders/lit.frag
#version 460 core

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;
in vec4 FragPosLightSpace;

out vec4 FragColour;

uniform sampler2D shadowMap;
uniform sampler2D diffuseTexture;
uniform vec3  lightDir, lightColour, viewPos;
uniform float shadowBias;

float calculateShadow(vec4 fragPosLS, vec3 normal, vec3 lightDirection) {
    vec3 projCoords = fragPosLS.xyz / fragPosLS.w;  // perspective divide
    projCoords = projCoords * 0.5 + 0.5;            // NDC -> [0,1]

    if (projCoords.z > 1.0) return 0.0;             // outside map = lit

    float closestDepth = texture(shadowMap, projCoords.xy).r;
    float currentDepth = projCoords.z;
    float bias = max(shadowBias * (1.0 - dot(normal, lightDirection)), 0.001);

    return currentDepth - bias > closestDepth ? 1.0 : 0.0;
}

void main() {
    vec3 colour  = texture(diffuseTexture, TexCoords).rgb;
    vec3 norm    = normalize(Normal);
    vec3 ambient = 0.15 * colour;

    float diff   = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * lightColour * colour;

    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 halfDir = normalize(lightDir + viewDir);
    float spec   = pow(max(dot(norm, halfDir), 0.0), 32.0);
    vec3 specular = spec * lightColour;

    float shadow = calculateShadow(FragPosLightSpace, norm, lightDir);

    // Ambient unaffected; diffuse + specular multiplied by shadow factor
    FragColour = vec4(ambient + (1.0 - shadow) * (diffuse + specular), 1.0);
}
```

---

## Fixing Shadow Artefacts

### Shadow Acne

A zebra-stripe pattern on surfaces that should be uniformly lit:

```
    ┌────────────────────────────────────┐
    │ ████    ████    ████    ████       │
    │     ████    ████    ████    ████   │
    │ ████    ████    ████    ████       │
    └────────────────────────────────────┘
    Flat floor with alternating lit/shadow stripes

Why:       Light
              \   Shadow map texel
               \  ┌───────────────────┐
                \ │ Stored depth: 0.500│
                 \│                   │
    ──────────────*─────────────────── Surface
                  │ ↑ Fragments here at 0.501
                  │   falsely shadowed
```

**Fix 1 -- Shader bias** (shown above in `calculateShadow`): scales with surface angle.

**Fix 2 -- glPolygonOffset**: hardware-level depth offset during the shadow pass:

```cpp
glEnable(GL_POLYGON_OFFSET_FILL);
glPolygonOffset(2.0f, 4.0f);
// ... render shadow geometry ...
glDisable(GL_POLYGON_OFFSET_FILL);
```

### Peter-Panning

Too much bias causes shadows to detach from their casters:

```
        ┌─────┐
        │ Box │
        └─────┘
           :        <-- gap!
        ███████     <-- shadow on floor
```

**Fix**: reduce bias, and cull front faces during the shadow pass so the depth map stores back-face depths, providing a natural offset:

```cpp
glCullFace(GL_FRONT);   // During shadow pass
// ... render ...
glCullFace(GL_BACK);    // Restore
```

### Percentage-Closer Filtering (PCF)

Without PCF, shadow edges are hard and jagged. PCF samples a grid of neighbours and averages the results:

```
    ┌───┬───┬───┐
    │ x │ x │ x │     9 samples, each does its own
    ├───┼───┼───┤     depth comparison. Average the
    │ x │ * │ x │     results for soft edges.
    ├───┼───┼───┤
    │ x │ x │ x │     5/9 in shadow -> factor = 0.56
    └───┴───┴───┘
```

```glsl
// Replace calculateShadow with this PCF version
float calculateShadowPCF(vec4 fragPosLS, vec3 normal, vec3 lightDirection) {
    vec3 projCoords = fragPosLS.xyz / fragPosLS.w;
    projCoords = projCoords * 0.5 + 0.5;
    if (projCoords.z > 1.0) return 0.0;

    float currentDepth = projCoords.z;
    float bias = max(0.005 * (1.0 - dot(normal, lightDirection)), 0.001);
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);

    float shadow = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float pcfDepth = texture(shadowMap,
                projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;
        }
    }
    return shadow / 9.0;
}
```

---

## Shadow Component -- ECS Integration

Components have no behaviour. The `ShadowCaster` component holds configuration and a handle to the GPU resource:

```cpp
// In src/engine/ecs/components_shadow.h
#pragma once
#include "engine/renderer/shadow_map.h"
#include <glm/glm.hpp>
#include <memory>

struct ShadowCaster {
    int   mapResolution = 2048;
    float orthoSize     = 30.0f;     // Half-width of orthographic projection
    float nearPlane     = 0.1f;
    float farPlane      = 100.0f;
    float bias          = 0.005f;

    // shared_ptr keeps the component trivially movable (EnTT requirement)
    std::shared_ptr<ShadowMap> shadowMap = nullptr;

    // Computed each frame by shadowMapSystem
    glm::mat4 lightSpaceMatrix = glm::mat4(1.0f);
};
```

To make a light cast shadows, attach both components:

```cpp
auto sun = registry.create();
registry.emplace<DirectionalLight>(sun,
    glm::vec3(-0.3f, -1.0f, -0.5f), glm::vec3(1.0f, 1.0f, 0.9f), 1.0f);
registry.emplace<ShadowCaster>(sun);
```

---

## shadowMapSystem -- The Shadow Pass

A free function (no state) that finds shadow-casting lights and renders the depth pass:

```cpp
// In src/engine/ecs/systems/shadow_map_system.h
#pragma once
#include <entt/entt.hpp>
class Shader;
struct WindowSize;

void shadowMapSystem(entt::registry& registry,
                     Shader& depthShader,
                     const WindowSize& windowSize);
```

```cpp
// In src/engine/ecs/systems/shadow_map_system.cpp
#include "engine/ecs/systems/shadow_map_system.h"
#include "engine/ecs/components.h"
#include "engine/ecs/components_shadow.h"
#include "engine/renderer/shader.h"
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

void shadowMapSystem(entt::registry& registry,
                     Shader& depthShader,
                     const WindowSize& windowSize) {

    auto lightView = registry.view<DirectionalLight, ShadowCaster>();

    for (auto [entity, light, shadow] : lightView.each()) {

        // Lazy-init the shadow map
        if (!shadow.shadowMap) {
            shadow.shadowMap = std::make_shared<ShadowMap>();
            shadow.shadowMap->init(shadow.mapResolution);
        }

        // Compute light space matrix
        glm::vec3 dir = glm::normalize(light.direction);
        float distance = (shadow.farPlane - shadow.nearPlane) * 0.5f;
        glm::vec3 lightPos = -dir * distance;

        glm::vec3 up = (std::abs(glm::dot(dir, glm::vec3(0.f, 1.f, 0.f))) > 0.99f)
                       ? glm::vec3(0.f, 0.f, 1.f) : glm::vec3(0.f, 1.f, 0.f);

        glm::mat4 lv = glm::lookAt(lightPos, glm::vec3(0.0f), up);
        glm::mat4 lp = glm::ortho(-shadow.orthoSize, shadow.orthoSize,
                                   -shadow.orthoSize, shadow.orthoSize,
                                   shadow.nearPlane, shadow.farPlane);
        shadow.lightSpaceMatrix = lp * lv;

        // Bind shadow FBO
        shadow.shadowMap->bindForWriting();
        glEnable(GL_DEPTH_TEST);
        glCullFace(GL_FRONT);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(2.0f, 4.0f);

        depthShader.use();
        depthShader.setMat4("lightSpaceMatrix", shadow.lightSpaceMatrix);

        // Render all meshes into the shadow map
        auto meshView = registry.view<Position, MeshRenderer>();
        for (auto [meshEntity, pos, mesh] : meshView.each()) {
            glm::mat4 model = glm::translate(glm::mat4(1.0f), pos.value);

            if (registry.all_of<Rotation>(meshEntity)) {
                const auto& rot = registry.get<Rotation>(meshEntity);
                model = glm::rotate(model, rot.angle, rot.axis);
            }
            if (registry.all_of<Scale>(meshEntity)) {
                const auto& scl = registry.get<Scale>(meshEntity);
                model = glm::scale(model, scl.value);
            }

            depthShader.setMat4("model", model);
            mesh.mesh->draw();
        }

        // Restore state
        glDisable(GL_POLYGON_OFFSET_FILL);
        glCullFace(GL_BACK);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, windowSize.width, windowSize.height);
    }
}
```

---

## Updated Render Loop

```
    ┌─────────────────────────────────────────────┐
    │ 1. Shadow pass (depth from light's POV)     │  shadowMapSystem()
    ├─────────────────────────────────────────────┤
    │ 2. Bind scene FBO (post-processing)         │
    ├─────────────────────────────────────────────┤
    │ 3. Skybox                                   │
    ├─────────────────────────────────────────────┤
    │ 4. Opaque geometry (shadow map on unit 1)   │  renderSystem()
    ├─────────────────────────────────────────────┤
    │ 5. Transparent / particles                  │
    ├─────────────────────────────────────────────┤
    │ 6. View model (weapon)                      │
    ├─────────────────────────────────────────────┤
    │ 7. Unbind scene FBO                         │
    ├─────────────────────────────────────────────┤
    │ 8. Post-processing                          │
    ├─────────────────────────────────────────────┤
    │ 9. HUD / UI                                 │
    └─────────────────────────────────────────────┘
```

```cpp
// In PlayingState::render()
void PlayingState::render() {
    // 1. Shadow pass
    shadowMapSystem(m_registry, m_depthShader, m_windowSize);

    // 2. Scene FBO
    m_sceneFBO.bind();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glm::mat4 view = m_camera.getViewMatrix();
    glm::mat4 proj = m_camera.getProjectionMatrix();

    // 3. Skybox
    m_skybox.render(m_skyboxShader, view, proj);

    // 4. Opaque geometry with shadows
    m_litShader.use();
    m_litShader.setMat4("view", view);
    m_litShader.setMat4("projection", proj);
    m_litShader.setVec3("viewPos", m_camera.getPosition());

    auto shadowView = m_registry.view<DirectionalLight, ShadowCaster>();
    for (auto [entity, light, shadow] : shadowView.each()) {
        m_litShader.setVec3("lightDir", -glm::normalize(light.direction));
        m_litShader.setVec3("lightColour", light.colour * light.intensity);
        m_litShader.setMat4("lightSpaceMatrix", shadow.lightSpaceMatrix);
        m_litShader.setFloat("shadowBias", shadow.bias);
        shadow.shadowMap->bindForReading(GL_TEXTURE1);
        m_litShader.setInt("shadowMap", 1);
    }
    renderSystem(m_registry, m_litShader);

    // 5-9. Particles, view model, unbind FBO, post-process, HUD
    particleSystem(m_registry, m_camera);
    viewModelSystem(m_registry, m_camera, m_viewModelShader);
    m_sceneFBO.unbind();
    postProcessSystem(m_sceneFBO, m_postProcessShader, m_screenQuad);
    hudSystem(m_registry, m_window);
}
```

---

## Optional: Cascaded Shadow Maps (CSM)

A single shadow map covering a large outdoor scene spreads its resolution too thin. A 2048x2048 map over 200 metres means each texel covers ~10 cm -- shadows near the camera look blocky.

**Cascaded Shadow Maps** split the camera frustum into depth slices (typically 3-4). Each slice gets its own shadow map with tighter orthographic bounds. The near slice covers a small area at high effective resolution; the far slice covers more area but the player is too distant to notice the lower quality.

```
    Camera ─────┬─────────┬──────────────┬──────────────────────
                │ Near    │ Mid          │ Far
                │ (0-10m) │ (10-30m)     │ (30-100m)
                │ tight   │ medium       │ wide bounds
                │ Sharp   │ Good         │ Acceptable
    ────────────┴─────────┴──────────────┴──────────────────────
```

In the fragment shader, select the cascade based on view-space depth and sample the corresponding map. CSM is the industry standard for outdoor scenes. The single shadow map from this chapter is the building block -- CSM runs the same process multiple times with different projection bounds.

---

## C++ Concept: `const` Correctness

The shadow system uses `const` in three important ways.

**const member functions** promise not to modify the object:

```cpp
class ShadowMap {
public:
    GLuint getDepthTexture() const { return m_depthTexture; } // reads only
    int    getResolution()   const { return m_resolution; }   // reads only
    void   bindForReading(GLenum unit) const;                 // GPU-side only
    bool   init(int resolution);                              // modifies -- NOT const
};
```

**const references** avoid copies while preventing mutation:

```cpp
glm::mat4 computeLightSpaceMatrix(const glm::vec3& direction,
                                   float orthoSize,
                                   float nearPlane, float farPlane);
```

**Why it matters** -- the compiler enforces the contract:

```cpp
void goodFunction(const ShadowMap& map) {
    GLuint tex = map.getDepthTexture();  // OK -- const function
    map.init(1024);                       // COMPILE ERROR -- not const
}
```

Mark everything `const` that can be. It catches bugs at compile time, documents intent, and enables compiler optimisations.

---

## What's Next

In **Chapter 30**, we will add font rendering -- loading TrueType fonts with FreeType, rasterising glyphs into a texture atlas, and drawing scalable, anti-aliased text on screen.
