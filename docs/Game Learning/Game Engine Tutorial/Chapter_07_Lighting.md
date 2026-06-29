# Chapter 7: Lighting

## What You'll Learn
- The Phong lighting model — ambient, diffuse, and specular
- How normals create the illusion of depth
- Directional lights (sun) and point lights (torches, lamps)
- Writing a lit shader in GLSL
- Adding light entities to the ECS
- Lightmaps — Quake's approach to static lighting

---

## Why Lighting Matters

Without lighting, every surface is the same brightness. A wall facing the player and a wall facing away look identical — there's no sense of depth. Lighting gives surfaces shape.

Quake used a combination of **lightmaps** (pre-baked static lighting) and simple **dynamic lights** (rockets, muzzle flash). We'll implement both approaches.

---

## The Phong Lighting Model

Phong is the classic real-time lighting model. It combines three components:

### 1. Ambient Light

A flat, constant brightness applied everywhere. Simulates indirect light bouncing around the environment. Without it, unlit surfaces would be pure black.

```
ambient = ambientStrength * lightColor
```

### 2. Diffuse Light

Brightness based on the angle between the surface normal and the light direction. Surfaces facing the light are bright; surfaces angled away are dark.

```
diffuse = max(dot(normal, lightDir), 0.0) * lightColor
```

The **dot product** of two normalised vectors gives the cosine of the angle between them:
- Facing the light (0°): `dot = 1.0` → full brightness
- 90° to the light: `dot = 0.0` → no light
- Facing away (>90°): `dot < 0` → clamped to 0

This is why normals matter. They define which direction each surface faces.

### 3. Specular Light

The shiny highlight you see on glossy surfaces. Depends on the angle between the reflected light direction and the viewer's direction.

```
reflectDir = reflect(-lightDir, normal)
specular = pow(max(dot(viewDir, reflectDir), 0.0), shininess) * lightColor
```

`shininess` controls how tight the highlight is:
- Low value (8): wide, dull highlight (rough surface)
- High value (128): tight, sharp highlight (shiny surface)

### Combined

```
finalColor = (ambient + diffuse + specular) * objectColor
```

---

## The Lit Shader

### assets/shaders/lit.vert

```glsl
#version 460 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;

out vec3 FragPos;       // World-space position
out vec3 Normal;        // Transformed normal
out vec2 TexCoord;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main() {
    FragPos = vec3(model * vec4(aPos, 1.0));

    // Normal matrix: ensures normals stay correct under non-uniform scaling
    // transpose(inverse(model)) removes translation and corrects for scale
    Normal = mat3(transpose(inverse(model))) * aNormal;

    TexCoord = aTexCoord;
    gl_Position = projection * view * vec4(FragPos, 1.0);
}
```

### The Normal Matrix Explained

If you scale an object non-uniformly (e.g. stretch it along X), the normals get skewed:

```
Before scaling:        After scaling X by 2:
    ▲ normal              ╱ normal (wrong!)
    │                    ╱
────┼────            ────────────
  surface              stretched surface
```

The normal matrix (`mat3(transpose(inverse(model)))`) counteracts this distortion. For uniform scaling and rotation only, you could use `mat3(model)` instead — but the full normal matrix is always correct.

> **Performance note:** Computing `inverse()` per vertex in the shader is expensive. In production, you'd compute the normal matrix on the CPU once and pass it as a uniform. For learning, the shader approach is clearer.

### assets/shaders/lit.frag

Each light type gets its own set of uniforms so they don't overwrite each other. The shader computes each light's contribution independently and sums them — this is how real-time lighting works: **light contributions are additive**.

```glsl
#version 460 core

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoord;

out vec4 FragColor;

// Material
uniform sampler2D textureSampler;
uniform float shininess;          // 32.0 is a reasonable default

// Directional light
uniform vec3 dirLightDir;         // Direction the light points
uniform vec3 dirLightColor;       // Color/intensity
uniform float dirLightAmbient;    // Ambient strength
uniform bool hasDirLight;         // Is there a directional light?

// Point light
uniform vec3 pointLightPos;       // World-space position
uniform vec3 pointLightColor;     // Color/intensity
uniform float pointLightAmbient;  // Ambient strength
uniform bool hasPointLight;       // Is there a point light?

// Camera
uniform vec3 viewPos;             // Camera position (for specular)

void main() {
    vec3 texColor = texture(textureSampler, TexCoord).rgb;
    vec3 norm = normalize(Normal);
    vec3 viewDir = normalize(viewPos - FragPos);

    vec3 result = vec3(0.0);

    // ─── Directional light contribution ──────────────────────────
    if (hasDirLight) {
        vec3 lDir = normalize(-dirLightDir);

        vec3 ambient = dirLightAmbient * dirLightColor;

        float diff = max(dot(norm, lDir), 0.0);
        vec3 diffuse = diff * dirLightColor;

        vec3 reflectDir = reflect(-lDir, norm);
        float spec = pow(max(dot(viewDir, reflectDir), 0.0), shininess);
        vec3 specular = spec * dirLightColor;

        result += (ambient + diffuse + specular) * texColor;
    }

    // ─── Point light contribution ────────────────────────────────
    if (hasPointLight) {
        vec3 lDir = normalize(pointLightPos - FragPos);

        float distance = length(pointLightPos - FragPos);
        float attenuation = 1.0 / (1.0 + 0.09 * distance + 0.032 * distance * distance);

        vec3 ambient = pointLightAmbient * pointLightColor;

        float diff = max(dot(norm, lDir), 0.0);
        vec3 diffuse = diff * pointLightColor;

        vec3 reflectDir = reflect(-lDir, norm);
        float spec = pow(max(dot(viewDir, reflectDir), 0.0), shininess);
        vec3 specular = spec * pointLightColor;

        result += (ambient + (diffuse + specular) * attenuation) * texColor;
    }

    FragColor = vec4(result, 1.0);
}
```

Notice how each light block computes its own ambient + diffuse + specular and adds to `result`. If both a sun and a torch are present, you get both contributions. If only one exists, the other block is skipped. This is a fundamental principle: **lighting is additive** — each light source contributes independently.

---

## Light Types

### Directional Light (Sun)

All rays are parallel — like sunlight. Has a direction, no position. No attenuation.

```
    ↓  ↓  ↓  ↓  ↓     Parallel rays
  ─────────────────
       surface
```

Good for outdoor scenes, global illumination direction.

### Point Light (Torch, Lamp)

Radiates in all directions from a position. Gets weaker with distance (attenuation).

```
         * light source
       ╱ │ ╲
      ╱  │  ╲     Radiating in all directions
     ╱   │   ╲
  ──────────────
      surface
```

**Attenuation formula:**
```
attenuation = 1.0 / (constant + linear * d + quadratic * d²)
```

The constants control how quickly light falls off:

| Constant | Linear | Quadratic | Range (approximate) |
|----------|--------|-----------|---------------------|
| 1.0 | 0.35 | 0.44 | ~7 units |
| 1.0 | 0.14 | 0.07 | ~20 units |
| 1.0 | 0.09 | 0.032 | ~32 units |
| 1.0 | 0.045 | 0.0075 | ~65 units |
| 1.0 | 0.007 | 0.0002 | ~600 units |

---

## Light Components

Add to `components.h`:

```cpp
struct DirectionalLight {
    glm::vec3 direction = glm::vec3(-0.2f, -1.0f, -0.3f);
    glm::vec3 color = glm::vec3(1.0f);
    float ambientStrength = 0.1f;
};

struct PointLight {
    glm::vec3 color = glm::vec3(1.0f);
    float ambientStrength = 0.05f;
    float linear = 0.09f;
    float quadratic = 0.032f;
};
```

A directional light entity needs `DirectionalLight`. A point light entity needs `Position` + `PointLight`. The position determines where the light is in the world.

---

## Updating the Render System for Lighting

The render system now needs to find lights and pass them to the shader. Here's a simplified version that handles one directional light and one point light:

```cpp
void renderSystem(entt::registry& registry, const Camera& camera,
                  float aspectRatio) {

    glm::mat4 view = camera.getViewMatrix();
    glm::mat4 projection = camera.getProjectionMatrix(aspectRatio);

    // ─── Find lights ─────────────────────────────────────────────
    // Directional light (use first one found)
    glm::vec3 dirLightDir(0.0f);
    glm::vec3 dirLightColor(0.0f);
    float dirAmbient = 0.1f;
    bool hasDirLight = false;

    auto dirView = registry.view<DirectionalLight>();
    for (auto [entity, light] : dirView.each()) {
        dirLightDir = light.direction;
        dirLightColor = light.color;
        dirAmbient = light.ambientStrength;
        hasDirLight = true;
        break;  // Only use the first one
    }

    // Point light (use first one found)
    glm::vec3 pointLightPos(0.0f);
    glm::vec3 pointLightColor(0.0f);
    float pointAmbient = 0.05f;
    bool hasPointLight = false;

    auto pointView = registry.view<Position, PointLight>();
    for (auto [entity, pos, light] : pointView.each()) {
        pointLightPos = pos.value;
        pointLightColor = light.color;
        pointAmbient = light.ambientStrength;
        hasPointLight = true;
        break;
    }

    // ─── Draw meshes ─────────────────────────────────────────────
    auto meshView = registry.view<Position, MeshRenderer>();

    for (auto [entity, pos, mesh] : meshView.each()) {
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, pos.value);

        if (registry.all_of<Rotation>(entity)) {
            auto& rot = registry.get<Rotation>(entity);
            model = glm::rotate(model, glm::radians(rot.euler.y), glm::vec3(0, 1, 0));
            model = glm::rotate(model, glm::radians(rot.euler.x), glm::vec3(1, 0, 0));
            model = glm::rotate(model, glm::radians(rot.euler.z), glm::vec3(0, 0, 1));
        }

        if (registry.all_of<Scale>(entity)) {
            auto& scl = registry.get<Scale>(entity);
            model = glm::scale(model, scl.value);
        }

        glUseProgram(mesh.shaderId);

        // Transform uniforms
        GLint loc;
        loc = glGetUniformLocation(mesh.shaderId, "model");
        glUniformMatrix4fv(loc, 1, GL_FALSE, &model[0][0]);
        loc = glGetUniformLocation(mesh.shaderId, "view");
        glUniformMatrix4fv(loc, 1, GL_FALSE, &view[0][0]);
        loc = glGetUniformLocation(mesh.shaderId, "projection");
        glUniformMatrix4fv(loc, 1, GL_FALSE, &projection[0][0]);

        // Camera position (for specular)
        loc = glGetUniformLocation(mesh.shaderId, "viewPos");
        glUniform3fv(loc, 1, &camera.getPosition()[0]);

        // Material
        loc = glGetUniformLocation(mesh.shaderId, "shininess");
        glUniform1f(loc, 32.0f);

        // Light uniforms — each light type has its own uniform names
        loc = glGetUniformLocation(mesh.shaderId, "hasDirLight");
        glUniform1i(loc, hasDirLight ? 1 : 0);
        loc = glGetUniformLocation(mesh.shaderId, "hasPointLight");
        glUniform1i(loc, hasPointLight ? 1 : 0);

        if (hasDirLight) {
            loc = glGetUniformLocation(mesh.shaderId, "dirLightDir");
            glUniform3fv(loc, 1, &dirLightDir[0]);
            loc = glGetUniformLocation(mesh.shaderId, "dirLightColor");
            glUniform3fv(loc, 1, &dirLightColor[0]);
            loc = glGetUniformLocation(mesh.shaderId, "dirLightAmbient");
            glUniform1f(loc, dirAmbient);
        }

        if (hasPointLight) {
            loc = glGetUniformLocation(mesh.shaderId, "pointLightPos");
            glUniform3fv(loc, 1, &pointLightPos[0]);
            loc = glGetUniformLocation(mesh.shaderId, "pointLightColor");
            glUniform3fv(loc, 1, &pointLightColor[0]);
            loc = glGetUniformLocation(mesh.shaderId, "pointLightAmbient");
            glUniform1f(loc, pointAmbient);
        }

        // Bind texture
        if (mesh.textureId != 0) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, mesh.textureId);
            loc = glGetUniformLocation(mesh.shaderId, "textureSampler");
            glUniform1i(loc, 0);
        }

        glBindVertexArray(mesh.vao);
        if (mesh.useIndices) {
            glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, 0);
        } else {
            glDrawArrays(GL_TRIANGLES, 0, mesh.vertexCount);
        }
    }
}
```

> **Note:** This approach handles one directional light + one point light simultaneously. Each light type has its own uniform names so they don't overwrite each other. For multiple point lights, you'd pass an array of light data to the shader or do multi-pass rendering — we'll improve this in later chapters.

---

## Loading the Lit Shader

The lit shader files exist on disk, but the engine won't use them unless we load them. Add the lit shader to `main.cpp` alongside the existing shader loads:

### Updated main.cpp (resource loading section)

```cpp
    // ─── Load resources ──────────────────────────────────────
    auto basicShader = resources.getShader("basic",
        "assets/shaders/basic.vert",
        "assets/shaders/basic.frag");

    auto texturedShader = resources.getShader("textured",
        "assets/shaders/textured.vert",
        "assets/shaders/textured.frag");

    auto litShader = resources.getShader("lit",               // NEW
        "assets/shaders/lit.vert",
        "assets/shaders/lit.frag");

    auto wallTexture = resources.getTexture("wall", "assets/textures/wall.png");

    auto cubeMesh = resources.getMesh("cube", "assets/models/cube.obj");
```

Nothing else in `main.cpp` changes — the render system already sends light uniforms to whatever shader is active. The key is that entities using lighting need to be assigned the `lit` shader instead of `basic` or `textured`.

---

## Updating scene_setup.cpp

Now switch both entities to use the `lit` shader so they receive lighting. Both cubes use the wall texture — this lets us see the torch's effect clearly. The cube further from the torch is mostly sun-lit, while the wall cube near the torch picks up its warm orange tint.

### Updated src/engine/ecs/scene_setup.cpp

```cpp
#include "engine/ecs/scene_setup.h"
#include "engine/ecs/components.h"

void setupScene(entt::registry& registry, ResourceManager& resources)
{
    auto litShader = resources.getShader("lit");
    auto wallTexture = resources.getTexture("wall");
    auto cubeMesh = resources.getMesh("cube");

    // Create a cube (lit — far from torch, mostly sun-lit)
    auto cube = registry.create();
    registry.emplace<Position>(cube, glm::vec3(-3.0f, 0.0f, -3.0f));
    registry.emplace<MeshRenderer>(cube,
        cubeMesh->getVAO(), 0u,
        litShader->getId(),
        wallTexture->getId(),
        true, cubeMesh->getIndexCount());

    // Create a wall cube (lit — close to torch, gets warm orange tint)
    auto wall = registry.create();
    registry.emplace<Position>(wall, glm::vec3(2.0f, 0.0f, -3.0f));
    registry.emplace<MeshRenderer>(wall,
        cubeMesh->getVAO(), 0u,
        litShader->getId(),
        wallTexture->getId(),
        true, cubeMesh->getIndexCount());

    // Sun light
    auto sun = registry.create();
    registry.emplace<DirectionalLight>(sun,
        glm::vec3(-0.2f, -1.0f, -0.3f),  // direction (angled down)
        glm::vec3(1.0f, 0.95f, 0.8f),    // warm white colour
        0.1f                               // ambient strength
    );

    // A torch in the level (close to the wall cube)
    auto torch = registry.create();
    registry.emplace<Position>(torch, glm::vec3(3.0f, 2.0f, -1.0f));
    registry.emplace<PointLight>(torch,
        glm::vec3(2.0f, 1.4f, 0.6f),     // bright warm orange
        0.15f, 0.045f, 0.0075f           // ambient, linear, quadratic (~65 unit range)
    );
}
```

The lights are entities — just data in the ECS. You can create, move, destroy, or modify them at runtime like anything else. Want a flickering torch? Write a system that randomly varies the `PointLight::color` each frame.

Both cubes use the `lit` shader now. The `textured` shader (which doesn't do lighting) is still available for UI elements or other things that shouldn't be affected by lights. Comparing the two cubes side-by-side shows the torch's effect — the wall cube closer to the torch has a visible warm orange tint, while the far cube is lit mainly by the sun.

---

## Lightmaps — Quake's Static Lighting

Quake didn't calculate lighting per frame for static geometry. Instead, it **pre-baked** lighting into textures called **lightmaps** during level compilation. At runtime, the lightmap is blended with the surface texture.

### How Lightmaps Work

1. A tool traces light rays through the level at build time
2. The resulting brightness for each surface is stored in a low-resolution texture (the lightmap)
3. At runtime, the renderer multiplies the surface texture by the lightmap

```glsl
// In the fragment shader:
vec3 texColor = texture(surfaceTexture, TexCoord).rgb;
vec3 lightValue = texture(lightmap, LightmapCoord).rgb;
FragColor = vec4(texColor * lightValue, 1.0);
```

### Why Lightmaps?

- **No per-pixel lighting cost** for static geometry
- **Soft shadows** and **light bouncing** can be pre-computed (would be too expensive in real-time)
- Quake's lightmaps were 16x16 blocks — very low resolution but surprisingly effective

### Adding Lightmap Support (Conceptual)

Each surface in the level would have a second set of UV coordinates pointing into the lightmap atlas:

```cpp
struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoords;       // Surface texture UVs
    glm::vec2 lightmapCoords;  // Lightmap UVs
};
```

The fragment shader would use two texture units:

```glsl
uniform sampler2D surfaceTexture;   // Bound to unit 0
uniform sampler2D lightmapTexture;  // Bound to unit 1

void main() {
    vec3 surface = texture(surfaceTexture, TexCoord).rgb;
    vec3 light = texture(lightmapTexture, LightmapCoord).rgb;
    FragColor = vec4(surface * light, 1.0);
}
```

We'll implement actual lightmap generation in the level/BSP chapter. For now, the per-pixel Phong lighting gives us good-enough results for development.

---

## Multiple Lights (Future Improvement)

The current shader handles one directional + one point light. For a Quake-like game with many torches and dynamic rocket lights, you'll want one of these approaches:

| Approach | Pros | Cons |
|----------|------|------|
| **Uniform array** (send N lights to shader) | Simple, one draw call per mesh | Limited light count, all lights affect all pixels |
| **Multi-pass** (draw each mesh once per light, blend results) | Unlimited lights | Many draw calls, expensive |
| **Deferred rendering** | Lights are decoupled from geometry count | More complex, requires MRT (multiple render targets) |

For this tutorial, we'll use the uniform array approach — it's the simplest and sufficient for a Quake-scale game. We'll expand the shader to accept an array of point lights in a later chapter.

---

## Expected Result

A 3D scene where surfaces facing the light are bright and surfaces facing away are dark. If you have a point light, objects near it glow warmly, and the effect fades with distance. The specular highlight creates a small bright spot on surfaces at the right angle.

This is the moment the engine starts looking like a real 3D game.

---

## What's Next

In **Chapter 8**, we'll build the game world — level geometry using BSP concepts, level loading from a custom format, and the beginnings of a walkable environment. The lighting we just built will make those level surfaces come alive.
