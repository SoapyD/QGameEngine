# Chapter 35: Normal Mapping

## What You'll Learn
- Why flat-shaded polygons look unconvincing even with Phong lighting
- What a normal map is and how RGB encodes surface normals in tangent space
- Tangent space: the per-vertex coordinate system that makes normal maps portable
- Computing tangent and bitangent vectors from triangle geometry and UVs
- Updating the vertex format with a packed `vec4` tangent (xyz + handedness)
- Writing a complete normal-mapped Phong shader (vertex + fragment)
- Adding normal map support to the `Material` component
- Loading normal maps correctly (no gamma correction)
- A brief look at parallax mapping for even more surface depth
- How normal mapping integrates with shadow mapping (Ch 29)
- C++ concept: `glm::vec4` packing and the tangent handedness trick

---

## Why Normal Mapping

Look at a brick wall rendered with Phong lighting. Every polygon is perfectly flat. You can add a diffuse texture so the surface *looks* like brick, but the lighting gives it away -- light falls evenly across the entire face because the surface normal is the same at every pixel.

```
Before: Flat polygon with diffuse texture only

    Light
      \
       \        Normal is the same everywhere (N)
        \       |   |   |   |   |   |
    ┌───v───v───v───v───v───v───v───────┐
    │                                   │
    │   Brick texture looks painted on  │
    │   No bumps, no grooves, no depth  │
    │                                   │
    └───────────────────────────────────┘
    Lighting is uniform across the surface.


After: Same polygon with a normal map applied

    Light
      \
       \        Normals vary per pixel
        \       /   |   \   /   |   \
    ┌───v───v───v───v───v───v───v───────┐
    │                                   │
    │   Bricks catch light on edges     │
    │   Grooves are darker, tops lit    │
    │   Looks 3D despite being flat     │
    │                                   │
    └───────────────────────────────────┘
    Lighting varies per pixel -- the surface looks bumpy.
```

Normal mapping achieves this by replacing the geometric surface normal with a per-pixel normal sampled from a texture. The mesh stays the same -- zero extra triangles -- but the lighting calculation uses a different normal at every fragment. The result is a massive visual upgrade for almost no performance cost.

---

## What Is a Normal Map

A normal map is a texture where each texel stores a surface normal direction encoded as a colour:

- **Red** channel = X component of the normal
- **Green** channel = Y component of the normal
- **Blue** channel = Z component of the normal

Because most surface normals point roughly "outward" from the surface (i.e. along the +Z axis in tangent space), the dominant value is Z. A normal pointing straight out is `(0, 0, 1)`. When encoded into the `[0, 1]` colour range, that becomes `(0.5, 0.5, 1.0)` -- which is why normal maps look blue-purple.

```
Normal Map Colour Encoding

    Normal direction    Tangent-space value     Encoded RGB colour
    ─────────────────   ───────────────────     ──────────────────
    Straight out (+Z)   ( 0.0,  0.0,  1.0)     (128, 128, 255)  blue
    Tilted right (+X)   ( 1.0,  0.0,  0.0)     (255, 128, 128)  red-ish
    Tilted up    (+Y)   ( 0.0,  1.0,  0.0)     (128, 255, 128)  green-ish
    Tilted left  (-X)   (-1.0,  0.0,  0.0)     (  0, 128, 128)  dark cyan

    Encoding formula:   colour = normal * 0.5 + 0.5
    Decoding formula:   normal = colour * 2.0 - 1.0
```

In the shader, you decode the sampled colour back into a direction vector:

```glsl
vec3 mappedNormal = texture(normalMap, uv).rgb * 2.0 - 1.0;
```

This gives you a direction in **tangent space** -- a coordinate system local to each triangle on the surface.

---

## Tangent Space

Here is the central problem. A normal map stores normals relative to a flat surface pointing up (+Z). But mesh surfaces face all directions -- a wall face might point along world +X, a floor along world +Y. If you stored world-space normals in the map, it would only work for one specific orientation. Rotate the mesh and the lighting breaks.

The solution is **tangent space**: a per-vertex coordinate system defined by three axes:

- **T** (tangent) -- points along the U texture coordinate direction
- **B** (bitangent) -- points along the V texture coordinate direction
- **N** (normal) -- the geometric surface normal, perpendicular to the triangle

```
Tangent Space on a Surface

                    N (normal)
                    ^
                    |
                    |
                    |
        ┌───────────────────────┐
       /          / |          /
      /          /  |         /
     /    B     /   |        /    T = along texture U
    /   (along /    |       /     B = along texture V
   /    tex V)/     |      /      N = surface normal
  /          /      |     /
 /          /       *────/──────> T (tangent)
/          /             /
└───────────────────────┘
         Triangle surface

The TBN matrix = [T | B | N] transforms
tangent-space vectors into world space.
```

The **TBN matrix** is a 3x3 matrix formed by arranging T, B, and N as columns. It transforms a vector from tangent space to world space:

```
                    ┌ Tx  Bx  Nx ┐   ┌ tangent-space x ┐     ┌ world-space x ┐
    TBN * normal =  │ Ty  By  Ny │ * │ tangent-space y │  =  │ world-space y │
                    └ Tz  Bz  Nz ┘   └ tangent-space z ┘     └ world-space z ┘
```

### Two Approaches

There are two ways to use the TBN matrix in a shader:

1. **Transform the sampled normal to world space** (per-fragment). Sample the normal map, multiply by TBN to get a world-space normal, then light in world space. More common because it works naturally with multiple lights, shadows, and environment mapping. We use this approach.

2. **Transform lights to tangent space** (per-vertex). Compute `inverse(TBN)` in the vertex shader and transform light/view directions into tangent space. Slightly cheaper (inverse is per-vertex), but less flexible.

---

## Computing Tangent Vectors

The tangent and bitangent vectors are derived from the triangle's edge vectors and UV coordinates. Given three vertices of a triangle:

```
    P0 (u0, v0)           Edge vectors:    E1 = P1 - P0
       *                                   E2 = P2 - P0
      / \
     /   \                UV deltas:       dUV1 = (u1-u0, v1-v0)
    /     \                                dUV2 = (u2-u0, v2-v0)
   /       \
  *─────────*             Relationship:
P1 (u1,v1)  P2 (u2,v2)   E1 = dUV1.x * T + dUV1.y * B
                          E2 = dUV2.x * T + dUV2.y * B
```

Written as a matrix equation:

```
    ┌ E1x  E1y  E1z ┐     ┌ dU1  dV1 ┐   ┌ Tx  Ty  Tz ┐
    │                │  =  │          │ * │             │
    └ E2x  E2y  E2z ┘     └ dU2  dV2 ┘   └ Bx  By  Bz ┘

    Solving for T and B by inverting the UV matrix:

    ┌ Tx  Ty  Tz ┐            1          ┌  dV2  -dV1 ┐   ┌ E1x  E1y  E1z ┐
    │             │ = ──────────────── *  │            │ * │               │
    └ Bx  By  Bz ┘   dU1*dV2 - dU2*dV1  └ -dU2   dU1 ┘   └ E2x  E2y  E2z ┘
```

Here is the complete tangent computation function:

```cpp
// In src/engine/renderer/tangent_compute.h
#pragma once
#include <glm/glm.hpp>
#include <vector>

// Compute tangent vectors for an indexed mesh.
// Tangents are averaged per vertex, then orthogonalised with Gram-Schmidt.
inline void computeTangents(std::vector<Vertex>& vertices,
                            const std::vector<unsigned int>& indices)
{
    std::vector<glm::vec3> tanAccum(vertices.size(), glm::vec3(0.0f));
    std::vector<glm::vec3> bitanAccum(vertices.size(), glm::vec3(0.0f));

    for (size_t i = 0; i < indices.size(); i += 3) {
        unsigned int i0 = indices[i], i1 = indices[i+1], i2 = indices[i+2];

        glm::vec3 e1 = vertices[i1].position - vertices[i0].position;
        glm::vec3 e2 = vertices[i2].position - vertices[i0].position;

        glm::vec2 duv1 = vertices[i1].texCoords - vertices[i0].texCoords;
        glm::vec2 duv2 = vertices[i2].texCoords - vertices[i0].texCoords;

        float det = duv1.x * duv2.y - duv2.x * duv1.y;
        if (std::abs(det) < 1e-8f) continue;  // Degenerate UV triangle
        float inv = 1.0f / det;

        glm::vec3 t = inv * (duv2.y * e1 - duv1.y * e2);
        glm::vec3 b = inv * (-duv2.x * e1 + duv1.x * e2);

        tanAccum[i0] += t;  tanAccum[i1] += t;  tanAccum[i2] += t;
        bitanAccum[i0] += b; bitanAccum[i1] += b; bitanAccum[i2] += b;
    }

    // Gram-Schmidt orthogonalisation and handedness
    for (size_t i = 0; i < vertices.size(); ++i) {
        const glm::vec3& n = vertices[i].normal;
        const glm::vec3& t = tanAccum[i];
        const glm::vec3& b = bitanAccum[i];

        // Remove the component of t along n, then normalise
        glm::vec3 orthoT = glm::normalize(t - n * glm::dot(n, t));

        // Handedness: if cross(n, t) dot b < 0, UVs are mirrored
        float handedness = (glm::dot(glm::cross(n, t), b) < 0.0f) ? -1.0f : 1.0f;

        vertices[i].tangent = glm::vec4(orthoT, handedness);
    }
}
```

The Gram-Schmidt step (`t - n * dot(n, t)`) removes the component of `t` along `n`, ensuring the tangent is perpendicular to the normal after averaging across shared vertices. The **handedness** value (stored as `tangent.w`) tells the shader whether to flip the bitangent -- necessary when meshes have mirrored UVs.

---

## Updated Vertex Format

The `Vertex` struct now includes a `vec4` tangent:

```cpp
// In src/engine/renderer/vertex.h
#pragma once

#include <glm/glm.hpp>

struct Vertex {
    glm::vec3 position;      // location 0
    glm::vec3 normal;        // location 1
    glm::vec2 texCoords;     // location 2
    glm::vec4 tangent;       // location 3 -- xyz = tangent, w = handedness
};
```

Update the vertex attribute setup to include the tangent at location 3:

```cpp
// In src/engine/renderer/mesh.cpp -- inside setupMesh() or equivalent

// Position -- location 0
glEnableVertexAttribArray(0);
glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                      (void*)offsetof(Vertex, position));

// Normal -- location 1
glEnableVertexAttribArray(1);
glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                      (void*)offsetof(Vertex, normal));

// Texture coords -- location 2
glEnableVertexAttribArray(2);
glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                      (void*)offsetof(Vertex, texCoords));

// Tangent -- location 3
glEnableVertexAttribArray(3);
glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                      (void*)offsetof(Vertex, tangent));
```

Notice that the tangent uses `4` for the size parameter (it is a `vec4`), while position and normal use `3`.

---

## Normal Map Shader

This shader integrates normal mapping with the Phong lighting model from Chapter 7 and the shadow mapping from Chapter 29.

### Vertex Shader

```glsl
// In assets/shaders/normal_mapped.vert
#version 330 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
layout (location = 3) in vec4 aTangent;

out VS_OUT {
    vec3 fragPos;
    vec2 texCoords;
    mat3 TBN;
    vec4 fragPosLightSpace;
} vs_out;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat4 lightSpaceMatrix;    // From shadow mapping (Ch 29)

void main()
{
    // Transform position to world space
    vec4 worldPos = model * vec4(aPos, 1.0);
    vs_out.fragPos = worldPos.xyz;
    vs_out.texCoords = aTexCoords;

    // Transform for shadow mapping
    vs_out.fragPosLightSpace = lightSpaceMatrix * worldPos;

    // Build TBN matrix in world space
    // Use the normal matrix to handle non-uniform scaling correctly
    mat3 normalMatrix = transpose(inverse(mat3(model)));

    vec3 N = normalize(normalMatrix * aNormal);
    vec3 T = normalize(normalMatrix * aTangent.xyz);

    // Re-orthogonalise T with respect to N (in case normalMatrix distorted it)
    T = normalize(T - dot(T, N) * N);

    // Compute bitangent using cross product and handedness
    vec3 B = cross(N, T) * aTangent.w;

    vs_out.TBN = mat3(T, B, N);

    gl_Position = projection * view * worldPos;
}
```

### Fragment Shader

```glsl
// In assets/shaders/normal_mapped.frag
#version 330 core

in VS_OUT {
    vec3 fragPos;
    vec2 texCoords;
    mat3 TBN;
    vec4 fragPosLightSpace;
} fs_in;

out vec4 FragColor;

// Material textures
uniform sampler2D diffuseMap;     // Texture unit 0
uniform sampler2D normalMap;      // Texture unit 1
uniform sampler2D specularMap;    // Texture unit 2
uniform sampler2D shadowMap;      // Texture unit 3

// Material properties
uniform float shininess;
uniform bool hasNormalMap;
uniform bool hasSpecularMap;

// Lighting
uniform vec3 lightDir;       // Direction TO light (normalised)
uniform vec3 lightColour;
uniform vec3 ambientColour;
uniform vec3 viewPos;

// Shadow calculation from Chapter 29 (unchanged -- uses the mapped normal for bias)
float calculateShadow(vec4 fragPosLightSpace, vec3 normal);  // See Ch 29 for full impl

void main()
{
    // --- Normal ---
    vec3 N;
    if (hasNormalMap) {
        // Sample normal map and decode from [0,1] to [-1,1]
        vec3 mappedNormal = texture(normalMap, fs_in.texCoords).rgb * 2.0 - 1.0;

        // Transform from tangent space to world space
        N = normalize(fs_in.TBN * mappedNormal);
    } else {
        // No normal map: use the geometric normal (third column of TBN)
        N = normalize(fs_in.TBN[2]);
    }

    // --- Diffuse texture ---
    vec4 diffuseColour = texture(diffuseMap, fs_in.texCoords);

    // --- Specular intensity ---
    float specularIntensity = 1.0;
    if (hasSpecularMap) {
        specularIntensity = texture(specularMap, fs_in.texCoords).r;
    }

    // --- Phong lighting (Ch 7) ---

    // Ambient
    vec3 ambient = ambientColour * diffuseColour.rgb;

    // Diffuse
    float diff = max(dot(N, lightDir), 0.0);
    vec3 diffuse = diff * lightColour * diffuseColour.rgb;

    // Specular (Blinn-Phong)
    vec3 viewDir = normalize(viewPos - fs_in.fragPos);
    vec3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(N, halfDir), 0.0), shininess);
    vec3 specular = spec * lightColour * specularIntensity;

    // --- Shadow ---
    float shadow = calculateShadow(fs_in.fragPosLightSpace, N);

    // Combine: ambient is never in shadow, diffuse and specular are
    vec3 result = ambient + (1.0 - shadow) * (diffuse + specular);

    FragColor = vec4(result, diffuseColour.a);
}
```

The key line is `N = normalize(fs_in.TBN * mappedNormal)`. The TBN matrix transforms the tangent-space normal from the map into a world-space normal. From that point on, the lighting calculation is identical to standard Phong -- we just use a better normal.

When `hasNormalMap` is false, we fall back to `fs_in.TBN[2]`, which is the interpolated geometric normal `N`. This means the same shader handles both normal-mapped and plain surfaces.

---

## Material Component Update

Add normal map support to the `Material` component. This is pure data -- no behaviour, as required by ECS:

```cpp
// In src/engine/ecs/components.h

struct Material {
    GLuint diffuseTexture = 0;
    GLuint normalMap = 0;         // 0 = no normal map, use vertex normal
    GLuint specularMap = 0;       // 0 = no specular map, use default
    float shininess = 32.0f;
    bool hasNormalMap = false;
    bool hasSpecularMap = false;
};
```

The rendering system binds the textures and sets the uniforms. The key texture unit assignments are: 0 = diffuse, 1 = normal map, 2 = specular map, 3 = shadow map (bound by the shadow system before this call):

```cpp
// In src/engine/ecs/systems/render_system.cpp -- inside the per-entity loop

// Material uniforms
glUniform1f(glGetUniformLocation(shaderProgram, "shininess"), mat.shininess);
glUniform1i(glGetUniformLocation(shaderProgram, "hasNormalMap"), mat.hasNormalMap);
glUniform1i(glGetUniformLocation(shaderProgram, "hasSpecularMap"), mat.hasSpecularMap);

// Bind diffuse texture to unit 0
glActiveTexture(GL_TEXTURE0);
glBindTexture(GL_TEXTURE_2D, mat.diffuseTexture);
glUniform1i(glGetUniformLocation(shaderProgram, "diffuseMap"), 0);

// Bind normal map to unit 1
glActiveTexture(GL_TEXTURE1);
glBindTexture(GL_TEXTURE_2D, mat.normalMap);
glUniform1i(glGetUniformLocation(shaderProgram, "normalMap"), 1);

// Bind specular map to unit 2
glActiveTexture(GL_TEXTURE2);
glBindTexture(GL_TEXTURE_2D, mat.specularMap);
glUniform1i(glGetUniformLocation(shaderProgram, "specularMap"), 2);
```

---

## Loading Normal Maps

Normal maps are loaded the same way as diffuse textures, with one critical difference: **do not gamma-correct them**. Normal maps store linear direction data, not colours. If you apply sRGB correction (as you should for diffuse textures), the decoded normals will be wrong and your lighting will look subtly broken.

The key is the internal format passed to `glTexImage2D`. Colour textures use `GL_SRGB` or `GL_SRGB_ALPHA`; normal maps use `GL_RGB` or `GL_RGBA`:

```cpp
// In src/engine/renderer/texture_loader.cpp

GLuint loadTexture(const std::string& path, bool isSRGB)
{
    GLuint textureID;
    glGenTextures(1, &textureID);

    int width, height, nrChannels;
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &nrChannels, 0);
    if (!data) {
        std::cerr << "Failed to load texture: " << path << std::endl;
        return 0;
    }

    GLenum dataFormat = (nrChannels == 4) ? GL_RGBA : GL_RGB;
    GLenum internalFormat;
    if (isSRGB) {
        internalFormat = (nrChannels == 4) ? GL_SRGB_ALPHA : GL_SRGB;
    } else {
        internalFormat = dataFormat;  // Linear -- no gamma conversion
    }

    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0,
                 dataFormat, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    stbi_image_free(data);
    return textureID;
}
```

```cpp
// Usage:
GLuint diffuse = loadTexture("assets/textures/brick_diffuse.png", true);   // sRGB
GLuint normal  = loadTexture("assets/textures/brick_normal.png",  false);  // linear!
```

Where do you get normal maps? Sites like Polyhaven and ambientCG provide them alongside diffuse textures. You can generate them from height maps using GIMP (Filters > Map > Normal Map). For testing, a flat normal map where every texel is `(128, 128, 255)` produces the same result as no normal map at all.

---

## Parallax Mapping

Normal mapping changes how light interacts with a surface, but the surface itself is still geometrically flat. **Parallax mapping** goes a step further by shifting the texture coordinates based on the viewing angle, creating the illusion that the surface has actual depth.

The technique uses a **height map** (grayscale texture: white = raised, black = recessed). When the camera looks at an angle, the shader offsets UVs so raised areas appear to occlude lower areas. Simple parallax mapping adds just a few lines to the fragment shader:

```glsl
// In the fragment shader, before sampling any textures:

// Transform view direction to tangent space
vec3 viewDirTangent = normalize(transpose(fs_in.TBN) * (viewPos - fs_in.fragPos));

// Sample height map and offset UVs
float height = texture(heightMap, fs_in.texCoords).r;
float heightScale = 0.05;  // Tune this -- too large causes swimming artifacts
vec2 parallaxOffset = viewDirTangent.xy / viewDirTangent.z * (height * heightScale);
vec2 adjustedUV = fs_in.texCoords - parallaxOffset;

// Use adjustedUV instead of fs_in.texCoords for all subsequent texture samples
vec3 mappedNormal = texture(normalMap, adjustedUV).rgb * 2.0 - 1.0;
vec4 diffuseColour = texture(diffuseMap, adjustedUV);
```

This simple version works but has artifacts at steep angles. More advanced techniques -- **Steep Parallax Mapping** and **Parallax Occlusion Mapping (POM)** -- trace through the height map in multiple steps to find the correct intersection. POM produces excellent results at the cost of more texture samples per fragment. The core idea is the same: offset UVs, then proceed with normal mapping as usual.

---

## Integration with Shadow Mapping

If you followed Chapter 29, shadow mapping already works correctly with normal mapping. The shadow pass (depth from light) does not use normals at all -- it only writes depth. The main pass uses the normal for lighting (dot product with light direction) and shadow bias (angle-based bias adjustment). Both use whatever `N` you compute in the fragment shader. Since we use the mapped normal, shadows are automatically consistent with the bumpy surface -- grooves that face away from the light are darker from both diffuse lighting and self-shadowing.

No extra work needed. This is an advantage of the "transform normal to world space" approach -- everything downstream that consumes a world-space normal works without modification.

---

## C++ Concept: `glm::vec4` Packing and Tangent Handedness

Look at the tangent stored in each vertex:

```cpp
glm::vec4 tangent;    // xyz = tangent direction, w = handedness
```

We are packing two pieces of information into one variable: the tangent direction (3 floats) and the handedness flag (1 float, either +1 or -1). The bitangent is then reconstructed in the shader:

```glsl
vec3 B = cross(N, T) * aTangent.w;
```

Why not just store the bitangent as a separate `vec3`? Memory. Every vertex in the mesh carries its attributes to the GPU. Consider a mesh with 10,000 vertices:

```
Without packing:   tangent vec3 (12B) + bitangent vec3 (12B) = 24 bytes per vertex
With packing:      tangent vec4 (16B) + bitangent computed   = 16 bytes per vertex
                                                         Savings: 33% less data
```

On the GPU, vertex data is read from a buffer for every single vertex, every single frame. Less data per vertex means less memory bandwidth consumed, better cache utilisation (more vertices fit in cache), and faster vertex fetching. This is a general principle: whenever you can reconstruct a value cheaply, prefer computation over storage. A `cross` product and a multiply are trivially fast compared to a memory fetch.

The `glm::vec4` type is laid out in memory as four contiguous floats: `[x, y, z, w]`. This matches the GPU's `vec4` attribute format exactly -- no conversion overhead.

```
Memory layout of a Vertex with packed tangent:

    Byte offset:  0         12        24       32          48
                  |─────────|─────────|────────|───────────|
                  position  normal    texCoords  tangent
                  vec3      vec3      vec2       vec4
                  (12 B)    (12 B)    (8 B)     (16 B)

    Total: 48 bytes per vertex (stride)
```

Each attribute aligns naturally to 4-byte boundaries, and the `offsetof` macro in the vertex attribute setup ensures the GPU knows exactly where each attribute begins within the stride.

---

## What's Next

In **Chapter 36: Model Loading**, we will integrate the Assimp library to load complex 3D models from standard formats (OBJ, FBX, glTF). Assimp can extract mesh data, materials, and -- conveniently -- pre-computed tangent vectors, so our normal mapping pipeline will work out of the box with loaded models. We will also handle multi-mesh models, embedded textures, and the material-to-component mapping that connects Assimp's data to our ECS architecture.
