# Chapter 35a: Material System Cleanup

> **Prerequisites:** Chapter 35 (Normal Mapping) completed. You should have a working `Material` component with diffuse, normal map, and specular map support, tangent computation via `computeTangents()`, and a fragment shader that branches on `hasNormalMap` / `hasSpecularMap` booleans. You should also have the ShaderCache from 30a and the OBJ loader from Chapter 36 (or equivalent mesh loading code).

---

## Time for Another Cleanup

Five chapters of rendering features -- decals, frustum culling, skeletal animation, level transitions, and normal mapping -- and the material system has grown organically. Every chapter bolted on another texture slot, another boolean, another uniform to set. It works. The visuals are great. But the code has the familiar smell: scattered responsibility, repeated patterns, and branching logic that grows linearly with every new feature.

Open your `components.h` and look at the `Material` struct as it stands after Chapter 35:

```cpp
// src/engine/ecs/components.h -- Material after Chapter 35

struct Material {
    GLuint diffuseTexture = 0;
    GLuint normalMap = 0;         // 0 = no normal map, use vertex normal
    GLuint specularMap = 0;       // 0 = no specular map, use default
    float shininess = 32.0f;
    bool hasNormalMap = false;
    bool hasSpecularMap = false;
};
```

And look at the render system that binds it:

```cpp
// src/engine/ecs/systems/render_system.cpp -- per-entity material binding

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

And the fragment shader:

```glsl
// assets/shaders/normal_mapped.frag -- branching logic

uniform bool hasNormalMap;
uniform bool hasSpecularMap;

void main()
{
    vec3 N;
    if (hasNormalMap) {
        vec3 mappedNormal = texture(normalMap, fs_in.texCoords).rgb * 2.0 - 1.0;
        N = normalize(fs_in.TBN * mappedNormal);
    } else {
        N = normalize(fs_in.TBN[2]);
    }

    float specularIntensity = 1.0;
    if (hasSpecularMap) {
        specularIntensity = texture(specularMap, fs_in.texCoords).r;
    }

    // ... rest of Phong lighting ...
}
```

And the tangent computation -- called from `computeTangents()` in `tangent_compute.h`, but invoked from different places:

```cpp
// In level loading (Chapter 34):
auto wallMesh = MeshFactory::createWall(width, height);
computeTangents(wallMesh.vertices, wallMesh.indices);

// In OBJ loading (Chapter 36):
OBJModel model = loadOBJ("assets/models/pillar.obj");
for (auto& mesh : model.meshes) {
    computeTangents(mesh.vertices, mesh.indices);
}

// In main.cpp setup (some entities):
auto cubeMesh = MeshFactory::createCube(1.0f);
computeTangents(cubeMesh.vertices, cubeMesh.indices);
```

Count the problems:

1. **The `Material` struct has redundant state.** `hasNormalMap` and `normalMap` encode the same information. If `normalMap != 0`, there is a normal map. The boolean is redundant -- and it is a bug waiting to happen when someone sets the texture handle but forgets to flip the boolean (or vice versa). We saw this exact pattern in Chapter 15a with HUD state that duplicated what could be derived.

2. **The struct is not future-ready.** We are about to add roughness maps, metallic maps, emissive maps, and ambient occlusion maps as the engine matures toward PBR (Chapter 44). Each new map means another `GLuint`, another `bool`, another uniform, and another block of binding code. The struct grows linearly, and the render system grows in lockstep.

3. **Tangent computation is the caller's responsibility.** Every place that creates or loads a mesh must remember to call `computeTangents()`. Forget it, and normal mapping silently breaks -- the TBN matrix is garbage, lighting looks wrong, and you spend an hour debugging. The mesh loader should handle this automatically.

4. **The shader branches on booleans at runtime.** GPU shader units execute in lockstep across a warp/wavefront. A dynamic `if (hasNormalMap)` means both branches execute if any fragment in the warp takes a different path. This is not catastrophic for two branches, but as we add more map types, the branching multiplies. The standard solution is **shader variants** -- compile separate shader programs with different `#define` flags, and select the right one at bind time.

Here is our plan:

| Problem | Solution |
|---|---|
| Redundant booleans alongside texture handles | `MapFlags` bitfield derived from which textures are set |
| Growing list of texture slots and booleans | `TextureSlot` enum, array-based binding |
| Tangent computation scattered across callsites | `MeshLoader` class that auto-computes tangents |
| Runtime shader branching | Shader variants with preprocessor defines, selected per material |

---

## C++ Concept: Bitwise Flags with `enum class`

Before we build the new Material, let us talk about the technique it uses. C++ developers frequently need a set of boolean flags packed into a single integer. The naive approach uses individual `bool` fields:

```cpp
bool hasNormalMap = false;
bool hasSpecularMap = false;
bool hasRoughnessMap = false;
bool hasEmissiveMap = false;
// ... grows with every new feature
```

This works, but it does not compose. You cannot pass "the set of maps this material uses" as a single value. You cannot compare two materials' feature sets with one operation. And you cannot use the flags to key into a cache.

The better approach is a **bitmask**: each flag is a distinct power of two, and you combine them with bitwise OR.

```cpp
// Raw integer approach (C-style):
constexpr uint32_t HAS_NORMAL_MAP   = 0x01;  // bit 0
constexpr uint32_t HAS_SPECULAR_MAP = 0x02;  // bit 1
constexpr uint32_t HAS_ROUGHNESS_MAP = 0x04; // bit 2

uint32_t flags = HAS_NORMAL_MAP | HAS_SPECULAR_MAP;  // bits 0 and 1 set

if (flags & HAS_NORMAL_MAP) { /* ... */ }  // test bit 0
```

This works, but `uint32_t` is not type-safe. You could accidentally OR a `MapFlags` value with a `CollisionLayers` value and the compiler would not complain.

C++ offers a better tool: `enum class` with explicit underlying type and `constexpr` operator overloads.

```cpp
enum class MapFlags : uint8_t
{
    None         = 0,
    NormalMap    = 1 << 0,   // 0x01
    SpecularMap  = 1 << 1,   // 0x02
    RoughnessMap = 1 << 2,   // 0x04
    MetallicMap  = 1 << 3,   // 0x08
    EmissiveMap  = 1 << 4,   // 0x10
    AOMap        = 1 << 5,   // 0x20
};

// Enable bitwise operations for MapFlags
constexpr MapFlags operator|(MapFlags a, MapFlags b) {
    return static_cast<MapFlags>(
        static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

constexpr MapFlags operator&(MapFlags a, MapFlags b) {
    return static_cast<MapFlags>(
        static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

constexpr MapFlags operator~(MapFlags a) {
    return static_cast<MapFlags>(~static_cast<uint8_t>(a));
}

constexpr bool hasFlag(MapFlags flags, MapFlags test) {
    return (flags & test) != MapFlags::None;
}
```

Now `MapFlags` is a distinct type. You cannot accidentally mix it with collision layers or other integers. The `constexpr` operators mean the compiler evaluates combinations at compile time when possible. And the `hasFlag()` helper reads clearly at every callsite:

```cpp
MapFlags flags = MapFlags::NormalMap | MapFlags::SpecularMap;

if (hasFlag(flags, MapFlags::NormalMap)) {
    // This material has a normal map
}
```

The underlying `uint8_t` gives us 8 flag bits -- more than enough for material maps. The entire flags field is one byte, compared to six separate `bool` fields (six bytes, or more with padding).

Most importantly for this chapter: we can use the `MapFlags` value as a **cache key**. Two materials with the same flags need the same shader variant. The flags value is a compact, comparable integer that uniquely identifies the feature combination.

---

## Step 1: Unified Material Struct

Here is the new `Material` component. It replaces the Chapter 35 version with a cleaner design that scales to any number of texture maps.

### engine/ecs/material.h

```cpp
// engine/ecs/material.h
#pragma once

#include <glad/glad.h>
#include <cstdint>

// ─── MapFlags ────────────────────────────────────────────────────
// Bitfield indicating which texture maps a material has bound.
// Used for shader variant selection and to avoid redundant boolean
// fields. Derived automatically when textures are assigned.

enum class MapFlags : uint8_t
{
    None         = 0,
    NormalMap    = 1 << 0,
    SpecularMap  = 1 << 1,
    RoughnessMap = 1 << 2,
    MetallicMap  = 1 << 3,
    EmissiveMap  = 1 << 4,
    AOMap        = 1 << 5,
};

constexpr MapFlags operator|(MapFlags a, MapFlags b) {
    return static_cast<MapFlags>(
        static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

constexpr MapFlags operator&(MapFlags a, MapFlags b) {
    return static_cast<MapFlags>(
        static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

constexpr MapFlags operator~(MapFlags a) {
    return static_cast<MapFlags>(~static_cast<uint8_t>(a));
}

constexpr bool hasFlag(MapFlags flags, MapFlags test) {
    return (flags & test) != MapFlags::None;
}

// ─── TextureSlot ─────────────────────────────────────────────────
// Named texture unit assignments. These are fixed across all
// shaders — any shader that uses a normal map expects it on unit 1.
// The shadow map slot is reserved by the shadow system (Ch 29).

enum class TextureSlot : int
{
    Albedo    = 0,
    NormalMap = 1,
    Specular  = 2,
    Roughness = 3,
    Metallic  = 4,
    Emissive  = 5,
    AO        = 6,
    ShadowMap = 7,   // Reserved — bound by shadow pass, not by Material
};

// ─── Material ────────────────────────────────────────────────────
// Unified material component. Texture handles of 0 mean "not set".
// The mapFlags field is kept in sync by setTexture() — it is the
// single source of truth for which maps are active.
//
// The render system reads mapFlags to select the correct shader
// variant. Individual has*() queries are convenience wrappers.

struct Material
{
    // ─── Texture handles ─────────────────────────────────────
    GLuint albedo      = 0;    // Diffuse / base colour
    GLuint normalMap   = 0;    // Tangent-space normals
    GLuint specularMap = 0;    // Specular intensity
    GLuint roughnessMap = 0;   // Roughness (PBR prep)
    GLuint metallicMap = 0;    // Metallic (PBR prep)
    GLuint emissiveMap = 0;    // Self-illumination
    GLuint aoMap       = 0;    // Ambient occlusion

    // ─── Scalar properties ───────────────────────────────────
    float shininess  = 32.0f;
    float roughness  = 0.5f;   // Default roughness (used when no map)
    float metallic   = 0.0f;   // Default metallic (used when no map)

    // ─── Flags ───────────────────────────────────────────────
    MapFlags mapFlags = MapFlags::None;

    // ─── Setters that maintain flag consistency ──────────────

    void setAlbedo(GLuint tex)      { albedo = tex; }

    void setNormalMap(GLuint tex) {
        normalMap = tex;
        updateFlag(MapFlags::NormalMap, tex != 0);
    }

    void setSpecularMap(GLuint tex) {
        specularMap = tex;
        updateFlag(MapFlags::SpecularMap, tex != 0);
    }

    void setRoughnessMap(GLuint tex) {
        roughnessMap = tex;
        updateFlag(MapFlags::RoughnessMap, tex != 0);
    }

    void setMetallicMap(GLuint tex) {
        metallicMap = tex;
        updateFlag(MapFlags::MetallicMap, tex != 0);
    }

    void setEmissiveMap(GLuint tex) {
        emissiveMap = tex;
        updateFlag(MapFlags::EmissiveMap, tex != 0);
    }

    void setAOMap(GLuint tex) {
        aoMap = tex;
        updateFlag(MapFlags::AOMap, tex != 0);
    }

    // ─── Convenience queries ─────────────────────────────────

    bool hasNormalMap()    const { return hasFlag(mapFlags, MapFlags::NormalMap); }
    bool hasSpecularMap()  const { return hasFlag(mapFlags, MapFlags::SpecularMap); }
    bool hasRoughnessMap() const { return hasFlag(mapFlags, MapFlags::RoughnessMap); }
    bool hasMetallicMap()  const { return hasFlag(mapFlags, MapFlags::MetallicMap); }
    bool hasEmissiveMap()  const { return hasFlag(mapFlags, MapFlags::EmissiveMap); }
    bool hasAOMap()        const { return hasFlag(mapFlags, MapFlags::AOMap); }

    // ─── Bind all textures ───────────────────────────────────
    // Activates texture units and binds the appropriate handles.
    // Only binds slots that have a texture set. Call this once
    // per entity in the render loop, before the draw call.

    void bindTextures() const
    {
        bindSlot(TextureSlot::Albedo,    albedo);
        bindSlot(TextureSlot::NormalMap,  normalMap);
        bindSlot(TextureSlot::Specular,   specularMap);
        bindSlot(TextureSlot::Roughness,  roughnessMap);
        bindSlot(TextureSlot::Metallic,   metallicMap);
        bindSlot(TextureSlot::Emissive,   emissiveMap);
        bindSlot(TextureSlot::AO,         aoMap);
    }

private:
    void updateFlag(MapFlags flag, bool set)
    {
        if (set)
            mapFlags = mapFlags | flag;
        else
            mapFlags = mapFlags & ~flag;
    }

    static void bindSlot(TextureSlot slot, GLuint texture)
    {
        if (texture == 0) return;
        glActiveTexture(GL_TEXTURE0 + static_cast<int>(slot));
        glBindTexture(GL_TEXTURE_2D, texture);
    }
};
```

### What Changed and Why

Compare the before and after side by side:

```cpp
// BEFORE (Chapter 35):
struct Material {
    GLuint diffuseTexture = 0;
    GLuint normalMap = 0;
    GLuint specularMap = 0;
    float shininess = 32.0f;
    bool hasNormalMap = false;      // redundant with normalMap != 0
    bool hasSpecularMap = false;    // redundant with specularMap != 0
};

// AFTER (Chapter 35a):
struct Material {
    GLuint albedo = 0;
    GLuint normalMap = 0;
    GLuint specularMap = 0;
    GLuint roughnessMap = 0;       // future PBR slots
    GLuint metallicMap = 0;
    GLuint emissiveMap = 0;
    GLuint aoMap = 0;
    float shininess = 32.0f;
    float roughness = 0.5f;
    float metallic = 0.0f;
    MapFlags mapFlags = MapFlags::None;  // replaces individual booleans
    // setters maintain flag consistency automatically
};
```

The key improvements:

1. **No redundant state.** The booleans are gone. `mapFlags` is always in sync with the texture handles because the setters enforce it. You cannot set a normal map without the flag updating. You cannot clear the flag without clearing the handle.

2. **One binding function.** Instead of six lines of `glActiveTexture` / `glBindTexture` / `glUniform1i` per texture slot scattered in the render system, the material binds its own textures. The render system calls `mat.bindTextures()` once.

3. **Scalable.** Adding a new texture type means: add a `GLuint` field, add a `MapFlags` value, add a setter, add one line to `bindTextures()`. The render system does not change. The shader variant system picks it up automatically.

4. **PBR-ready.** The roughness and metallic fields (both scalar defaults and map slots) prepare for Chapter 44 without requiring another restructure. We are not implementing PBR yet -- we are just making sure the struct does not need to change again when we do.

### Why Methods on an ECS Component?

You might notice that `Material` now has methods: `setNormalMap()`, `bindTextures()`, `hasNormalMap()`. This looks like it violates the "components are pure data" rule we established early on.

It does not, for the same reason `ScreenShake::trigger()` (Chapter 20a) and `HUDMessages::add()` (Chapter 15a) did not. These methods do not contain game logic. They are **data consistency helpers** -- they ensure the struct's invariants hold. `setNormalMap()` does not decide when to apply a normal map or how to render it. It just keeps the flags in sync with the handle. `bindTextures()` does not decide rendering order or shader selection -- it mechanically activates texture units based on what is stored. The actual rendering decisions remain in the render system.

The rule is: components should not contain *behaviour* (game logic, decision-making). Helpers that maintain internal consistency are fine.

### Updating Existing Code

Find every place that creates or modifies a `Material` and update it:

```cpp
// BEFORE:
Material mat;
mat.diffuseTexture = resources.getTexture("brick_diffuse");
mat.normalMap = resources.getTexture("brick_normal");
mat.hasNormalMap = true;
mat.specularMap = resources.getTexture("brick_specular");
mat.hasSpecularMap = true;
mat.shininess = 64.0f;

// AFTER:
Material mat;
mat.setAlbedo(resources.getTexture("brick_diffuse"));
mat.setNormalMap(resources.getTexture("brick_normal"));
mat.setSpecularMap(resources.getTexture("brick_specular"));
mat.shininess = 64.0f;
```

The `hasNormalMap = true` line is gone. The setter handles it. One fewer thing to forget.

---

## Step 2: Tangent Computation in the Mesh Loader

The `computeTangents()` function from Chapter 35 is correct and does not need to change. What needs to change is *where it gets called*. Right now, every place that creates a mesh must remember to call it:

```cpp
// Chapter 34 — level loading:
auto wallMesh = MeshFactory::createWall(width, height);
computeTangents(wallMesh.vertices, wallMesh.indices);
uploadMesh(wallMesh);

// Chapter 36 — OBJ loading:
OBJModel model = loadOBJ("assets/models/pillar.obj");
for (auto& mesh : model.meshes) {
    computeTangents(mesh.vertices, mesh.indices);
}

// main.cpp — manual creation:
auto cubeMesh = MeshFactory::createCube(1.0f);
computeTangents(cubeMesh.vertices, cubeMesh.indices);
```

Three different callsites, all doing the same thing. Forget one, and that mesh silently has garbage tangent data. Normal mapping breaks, the lighting looks wrong, and you spend time debugging a problem that should have been impossible.

The fix: make tangent computation automatic. Any code path that produces a mesh should compute tangents before returning. We do this by creating a `MeshLoader` class that wraps both the OBJ loader and the mesh factory, ensuring tangents are always computed.

### engine/assets/mesh_loader.h

```cpp
// engine/assets/mesh_loader.h
#pragma once

#include "engine/renderer/vertex.h"
#include "engine/renderer/tangent_compute.h"
#include "engine/assets/obj_loader.h"

#include <string>
#include <vector>
#include <iostream>

// ─── MeshData ────────────────────────────────────────────────────
// The output of any mesh loading operation. Vertices always have
// valid tangent vectors by the time MeshLoader returns them.

struct MeshData
{
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    std::string materialName;   // From OBJ/glTF, empty for procedural
};

// ─── MeshLoader ──────────────────────────────────────────────────
// Single entry point for all mesh loading. Wraps the OBJ parser
// and MeshFactory utilities. Guarantees that tangent vectors are
// computed for every mesh before it is returned.
//
// This replaces the scattered computeTangents() calls throughout
// the codebase. After this cleanup, no code outside MeshLoader
// should call computeTangents() directly.

class MeshLoader
{
public:
    // ─── Load from OBJ file ──────────────────────────────────
    // Parses the file, computes tangents for every sub-mesh,
    // and returns a vector of MeshData (one per material group).

    static std::vector<MeshData> loadOBJ(const std::string& filepath)
    {
        OBJModel model = ::loadOBJ(filepath);

        std::vector<MeshData> result;
        result.reserve(model.meshes.size());

        for (auto& objMesh : model.meshes)
        {
            MeshData data;
            data.materialName = objMesh.materialName;

            // Convert from OBJ loader's Vertex to our engine Vertex.
            // The OBJ loader (Ch 36) produces position/normal/uv.
            // We add the tangent field here.
            data.vertices.reserve(objMesh.vertices.size());
            for (const auto& v : objMesh.vertices)
            {
                Vertex vert;
                vert.position  = v.position;
                vert.normal    = v.normal;
                vert.texCoords = v.uv;
                vert.tangent   = glm::vec4(0.0f);  // Will be computed below
                data.vertices.push_back(vert);
            }
            data.indices = objMesh.indices;

            // Tangent computation -- guaranteed for every mesh
            finaliseTangents(data);

            result.push_back(std::move(data));
        }

        return result;
    }

    // ─── Load procedural mesh ────────────────────────────────
    // Takes raw vertex/index data (from MeshFactory or manual
    // construction) and computes tangents before returning.
    // This replaces the manual computeTangents() calls after
    // MeshFactory::createCube(), createWall(), etc.

    static MeshData fromRaw(std::vector<Vertex> vertices,
                            std::vector<unsigned int> indices)
    {
        MeshData data;
        data.vertices = std::move(vertices);
        data.indices  = std::move(indices);

        finaliseTangents(data);

        return data;
    }

private:
    // ─── Tangent finalisation ────────────────────────────────
    // Computes tangent vectors and validates the result. This is
    // the ONLY place computeTangents() should be called.

    static void finaliseTangents(MeshData& data)
    {
        if (data.vertices.empty() || data.indices.empty())
            return;

        computeTangents(data.vertices, data.indices);

        // Sanity check: warn if any tangent is degenerate.
        // This catches meshes with bad UVs or degenerate triangles.
        for (size_t i = 0; i < data.vertices.size(); ++i)
        {
            const auto& t = data.vertices[i].tangent;
            if (glm::length(glm::vec3(t)) < 0.001f)
            {
                // Assign a default tangent to avoid NaN in the shader.
                // This is a fallback -- the mesh's UVs should be fixed.
                data.vertices[i].tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
            }
        }
    }
};
```

### Before vs After: Mesh Creation

```cpp
// BEFORE (scattered tangent calls):
auto wallMesh = MeshFactory::createWall(width, height);
computeTangents(wallMesh.vertices, wallMesh.indices);
GLuint wallVAO = uploadMesh(wallMesh.vertices, wallMesh.indices);

auto cubeMesh = MeshFactory::createCube(1.0f);
computeTangents(cubeMesh.vertices, cubeMesh.indices);
GLuint cubeVAO = uploadMesh(cubeMesh.vertices, cubeMesh.indices);

OBJModel pillar = loadOBJ("assets/models/pillar.obj");
for (auto& mesh : pillar.meshes) {
    computeTangents(mesh.vertices, mesh.indices);  // easy to forget
}

// AFTER (tangents computed automatically):
MeshData wallMesh = MeshLoader::fromRaw(
    MeshFactory::createWallVertices(width, height),
    MeshFactory::createWallIndices());
GLuint wallVAO = uploadMesh(wallMesh.vertices, wallMesh.indices);

MeshData cubeMesh = MeshLoader::fromRaw(
    MeshFactory::createCubeVertices(1.0f),
    MeshFactory::createCubeIndices());
GLuint cubeVAO = uploadMesh(cubeMesh.vertices, cubeMesh.indices);

auto pillarMeshes = MeshLoader::loadOBJ("assets/models/pillar.obj");
// Tangents already computed for every sub-mesh
```

The `computeTangents()` call is gone from every callsite. It happens inside `MeshLoader::finaliseTangents()`, once, in one place. The sanity check for degenerate tangents is a bonus -- it catches bad UVs early instead of producing mysterious lighting artifacts.

### Updating MeshFactory

The `MeshFactory` functions from Chapter 5a now need to return raw vertex and index arrays rather than fully assembled meshes, so `MeshLoader::fromRaw()` can wrap them. Alternatively, you can keep the existing `MeshFactory` interface and have it call `MeshLoader::fromRaw()` internally. Either approach works. Here is the wrapper approach, which requires the least change to existing code:

```cpp
// engine/core/mesh_factory.h -- updated to use MeshLoader internally

namespace MeshFactory
{
    inline MeshData createCube(float size)
    {
        // ... existing vertex and index generation code (unchanged) ...
        std::vector<Vertex> vertices = { /* ... */ };
        std::vector<unsigned int> indices = { /* ... */ };

        // Tangents computed automatically by MeshLoader
        return MeshLoader::fromRaw(std::move(vertices), std::move(indices));
    }

    inline MeshData createWall(float width, float height)
    {
        std::vector<Vertex> vertices = { /* ... */ };
        std::vector<unsigned int> indices = { /* ... */ };

        return MeshLoader::fromRaw(std::move(vertices), std::move(indices));
    }

    inline MeshData createPlane(float size)
    {
        std::vector<Vertex> vertices = { /* ... */ };
        std::vector<unsigned int> indices = { /* ... */ };

        return MeshLoader::fromRaw(std::move(vertices), std::move(indices));
    }
}
```

Now every mesh produced by `MeshFactory` has valid tangent vectors, with zero effort from the caller.

---

## Step 3: Shader Variants

This is the most significant refactoring in this chapter. The Chapter 35 fragment shader uses runtime `if` statements to handle the presence or absence of normal maps and specular maps:

```glsl
// BEFORE: runtime branching
uniform bool hasNormalMap;
uniform bool hasSpecularMap;

void main() {
    vec3 N;
    if (hasNormalMap) {
        N = normalize(fs_in.TBN * (texture(normalMap, fs_in.texCoords).rgb * 2.0 - 1.0));
    } else {
        N = normalize(fs_in.TBN[2]);
    }
    // ...
}
```

This works for two booleans. But every new texture map adds another branch. With six map types, you have 64 possible combinations. Some of those branches will have different code paths (normal mapping changes the normal computation; specular mapping changes the specular term). A shader full of `if` statements is hard to read, hard to maintain, and potentially slower due to warp divergence.

The standard solution is **shader variants** (also called an **uber-shader** approach). You write one shader source with `#ifdef` blocks, then compile multiple versions with different `#define` flags. Each material selects the variant that matches its feature set.

### The Uber-Shader

Here is the updated fragment shader. Instead of `uniform bool hasNormalMap`, it uses `#ifdef HAS_NORMAL_MAP`:

```glsl
// assets/shaders/lit.frag
// Uber-shader: compiled with different #define combinations.
// Variants are managed by ShaderVariantKey + ShaderCache.

#version 330 core

in VS_OUT {
    vec3 fragPos;
    vec2 texCoords;
    mat3 TBN;
    vec4 fragPosLightSpace;
} fs_in;

out vec4 FragColor;

// ─── Texture samplers ────────────────────────────────────────────
// Always declared — unused samplers are optimised away by the driver.
uniform sampler2D albedoMap;     // TextureSlot::Albedo    (unit 0)

#ifdef HAS_NORMAL_MAP
uniform sampler2D normalMap;     // TextureSlot::NormalMap  (unit 1)
#endif

#ifdef HAS_SPECULAR_MAP
uniform sampler2D specularMap;   // TextureSlot::Specular   (unit 2)
#endif

uniform sampler2D shadowMap;     // TextureSlot::ShadowMap  (unit 7)

// ─── Material properties ─────────────────────────────────────────
uniform float shininess;

// ─── Lighting ────────────────────────────────────────────────────
uniform vec3 lightDir;
uniform vec3 lightColour;
uniform vec3 ambientColour;
uniform vec3 viewPos;

// Shadow calculation from Chapter 29 (unchanged)
float calculateShadow(vec4 fragPosLightSpace, vec3 normal);

void main()
{
    // ─── Normal ──────────────────────────────────────────────
#ifdef HAS_NORMAL_MAP
    vec3 mappedNormal = texture(normalMap, fs_in.texCoords).rgb * 2.0 - 1.0;
    vec3 N = normalize(fs_in.TBN * mappedNormal);
#else
    vec3 N = normalize(fs_in.TBN[2]);
#endif

    // ─── Albedo ──────────────────────────────────────────────
    vec4 albedo = texture(albedoMap, fs_in.texCoords);

    // ─── Specular intensity ──────────────────────────────────
#ifdef HAS_SPECULAR_MAP
    float specIntensity = texture(specularMap, fs_in.texCoords).r;
#else
    float specIntensity = 1.0;
#endif

    // ─── Phong lighting ──────────────────────────────────────
    vec3 ambient = ambientColour * albedo.rgb;

    float diff = max(dot(N, lightDir), 0.0);
    vec3 diffuse = diff * lightColour * albedo.rgb;

    vec3 viewDir = normalize(viewPos - fs_in.fragPos);
    vec3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(N, halfDir), 0.0), shininess);
    vec3 specular = spec * lightColour * specIntensity;

    // ─── Shadow ──────────────────────────────────────────────
    float shadow = calculateShadow(fs_in.fragPosLightSpace, N);

    vec3 result = ambient + (1.0 - shadow) * (diffuse + specular);
    FragColor = vec4(result, albedo.a);
}
```

The vertex shader stays the same as Chapter 35 -- it always computes the TBN matrix. Even without a normal map, passing the TBN is harmless (the fragment shader just reads column 2 for the geometric normal). The cost is negligible, and it keeps us to one vertex shader.

### ShaderVariantKey

Each unique combination of `MapFlags` needs a separate compiled shader program. We use the `MapFlags` value directly as the key:

```cpp
// engine/renderer/shader_variants.h
#pragma once

#include "engine/ecs/material.h"

#include <string>
#include <cstdint>

// ─── ShaderVariantKey ────────────────────────────────────────────
// Identifies a unique shader variant by its set of active features.
// The key is simply the MapFlags value cast to an integer — each
// bit combination maps to exactly one compiled shader program.
//
// Example:
//   MapFlags::None                            → key 0  (no maps)
//   MapFlags::NormalMap                        → key 1  (normal only)
//   MapFlags::NormalMap | MapFlags::SpecularMap → key 3  (normal + spec)

struct ShaderVariantKey
{
    uint8_t value = 0;

    ShaderVariantKey() = default;

    explicit ShaderVariantKey(MapFlags flags)
        : value(static_cast<uint8_t>(flags))
    {}

    bool operator==(const ShaderVariantKey& other) const {
        return value == other.value;
    }

    bool operator<(const ShaderVariantKey& other) const {
        return value < other.value;
    }
};

// Hash for use in unordered_map
struct ShaderVariantKeyHash
{
    std::size_t operator()(const ShaderVariantKey& key) const {
        return std::hash<uint8_t>{}(key.value);
    }
};

// ─── Build preprocessor defines from MapFlags ────────────────────
// Generates a string like "#define HAS_NORMAL_MAP\n#define HAS_SPECULAR_MAP\n"
// that is prepended to the shader source before compilation.

inline std::string buildDefines(MapFlags flags)
{
    std::string defines;

    if (hasFlag(flags, MapFlags::NormalMap))
        defines += "#define HAS_NORMAL_MAP\n";

    if (hasFlag(flags, MapFlags::SpecularMap))
        defines += "#define HAS_SPECULAR_MAP\n";

    if (hasFlag(flags, MapFlags::RoughnessMap))
        defines += "#define HAS_ROUGHNESS_MAP\n";

    if (hasFlag(flags, MapFlags::MetallicMap))
        defines += "#define HAS_METALLIC_MAP\n";

    if (hasFlag(flags, MapFlags::EmissiveMap))
        defines += "#define HAS_EMISSIVE_MAP\n";

    if (hasFlag(flags, MapFlags::AOMap))
        defines += "#define HAS_AO_MAP\n";

    return defines;
}
```

### ShaderCache Extension

The ShaderCache from 30a needs a method that compiles shader variants on demand. We add a `getVariant()` method:

```cpp
// engine/renderer/shader_cache.h
// Extension to the ShaderCache from Chapter 30a.
#pragma once

#include "engine/renderer/shader_variants.h"

#include <glad/glad.h>
#include <string>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <iostream>

class ShaderCache
{
public:
    // ─── Existing methods from 30a (unchanged) ───────────────
    // loadShader(name, vertPath, fragPath) — compiles and caches
    // getShader(name) — retrieves by name
    // ... (see Chapter 30a for full implementation)

    // ─── NEW: Variant compilation ────────────────────────────
    // Compiles a shader variant by prepending #define statements
    // to the base shader source. Cached by (baseName, variantKey).
    // Returns the compiled program ID.
    //
    // If the variant has already been compiled, returns the cached
    // version. This means the first frame with a new material
    // combination pays the compilation cost, but subsequent frames
    // are a hash lookup.

    GLuint getVariant(const std::string& baseName,
                      ShaderVariantKey key)
    {
        auto cacheKey = std::make_pair(baseName, key);
        auto it = variants.find(cacheKey);
        if (it != variants.end())
            return it->second;

        // Not cached — compile this variant
        auto srcIt = shaderSources.find(baseName);
        if (srcIt == shaderSources.end())
        {
            std::cerr << "ShaderCache: base shader '" << baseName
                      << "' not loaded.\n";
            return 0;
        }

        const ShaderSource& src = srcIt->second;
        MapFlags flags = static_cast<MapFlags>(key.value);
        std::string defines = buildDefines(flags);

        // Insert defines after the #version line
        std::string modifiedFrag = injectDefines(src.fragmentSource, defines);

        GLuint program = compileProgram(src.vertexSource, modifiedFrag);
        if (program != 0)
        {
            // Set texture sampler uniforms once at compile time.
            // These never change — each sampler is permanently
            // bound to its TextureSlot.
            glUseProgram(program);
            setTextureUniforms(program);
            glUseProgram(0);
        }

        variants[cacheKey] = program;
        return program;
    }

    // ─── Load base shader sources ────────────────────────────
    // Reads vertex and fragment shader files and stores the source
    // text. Does NOT compile — variants are compiled on demand.

    void loadShaderSource(const std::string& name,
                          const std::string& vertPath,
                          const std::string& fragPath)
    {
        ShaderSource src;
        src.vertexSource = readFile(vertPath);
        src.fragmentSource = readFile(fragPath);
        shaderSources[name] = std::move(src);
    }

private:
    struct ShaderSource
    {
        std::string vertexSource;
        std::string fragmentSource;
    };

    // Variant cache key: (baseName, variantKey)
    struct PairHash
    {
        std::size_t operator()(const std::pair<std::string, ShaderVariantKey>& p) const
        {
            std::size_t h1 = std::hash<std::string>{}(p.first);
            std::size_t h2 = ShaderVariantKeyHash{}(p.second);
            return h1 ^ (h2 << 16);
        }
    };

    struct PairEqual
    {
        bool operator()(const std::pair<std::string, ShaderVariantKey>& a,
                        const std::pair<std::string, ShaderVariantKey>& b) const
        {
            return a.first == b.first && a.second == b.second;
        }
    };

    std::unordered_map<std::string, ShaderSource> shaderSources;
    std::unordered_map<std::pair<std::string, ShaderVariantKey>,
                       GLuint, PairHash, PairEqual> variants;

    // ─── Inject #define block after #version ──────────────────
    // The #version directive must be the first non-comment line in
    // GLSL. We find it and insert our defines immediately after.

    static std::string injectDefines(const std::string& source,
                                     const std::string& defines)
    {
        // Find the end of the #version line
        size_t versionPos = source.find("#version");
        if (versionPos == std::string::npos)
        {
            // No #version found — prepend defines at the top
            return defines + source;
        }

        size_t lineEnd = source.find('\n', versionPos);
        if (lineEnd == std::string::npos)
            lineEnd = source.size();
        else
            lineEnd += 1;  // Include the newline

        // Insert defines after the #version line
        std::string result = source.substr(0, lineEnd);
        result += defines;
        result += source.substr(lineEnd);
        return result;
    }

    // ─── Set texture sampler uniforms ────────────────────────
    // Maps each sampler name to its fixed texture unit.

    static void setTextureUniforms(GLuint program)
    {
        auto set = [&](const char* name, TextureSlot slot) {
            GLint loc = glGetUniformLocation(program, name);
            if (loc >= 0)
                glUniform1i(loc, static_cast<int>(slot));
        };

        set("albedoMap",   TextureSlot::Albedo);
        set("normalMap",   TextureSlot::NormalMap);
        set("specularMap", TextureSlot::Specular);
        set("roughnessMap", TextureSlot::Roughness);
        set("metallicMap", TextureSlot::Metallic);
        set("emissiveMap", TextureSlot::Emissive);
        set("aoMap",       TextureSlot::AO);
        set("shadowMap",   TextureSlot::ShadowMap);
    }

    // ─── File reading ────────────────────────────────────────
    static std::string readFile(const std::string& path)
    {
        std::ifstream file(path);
        if (!file.is_open())
        {
            std::cerr << "ShaderCache: failed to open " << path << "\n";
            return "";
        }
        std::stringstream ss;
        ss << file.rdbuf();
        return ss.str();
    }

    // ─── Shader compilation ──────────────────────────────────
    // compileProgram() compiles vert + frag, links, and returns
    // the program ID. Returns 0 on failure.
    // (Full implementation same as Chapter 30a — omitted for brevity.
    //  See Chapter 30a for the complete compile/link/error-check code.)
    static GLuint compileProgram(const std::string& vertSource,
                                 const std::string& fragSource);
};
```

### How Variant Selection Works

When the render system encounters an entity with a `Material` component, it asks the ShaderCache for the right variant:

```cpp
// Conceptual flow:
ShaderVariantKey key(mat.mapFlags);
GLuint shader = shaderCache.getVariant("lit", key);
glUseProgram(shader);
mat.bindTextures();
// ... set uniforms, draw ...
```

The first time a particular combination is encountered (say, normal map + specular map), the ShaderCache compiles a new variant with `#define HAS_NORMAL_MAP` and `#define HAS_SPECULAR_MAP` prepended to the fragment shader. Every subsequent entity with the same combination reuses the cached program.

In practice, most games use only a handful of material combinations:
- No maps (basic textured geometry, UI elements)
- Normal map only (most world surfaces)
- Normal map + specular map (detailed props)
- Normal map + specular map + emissive (lights, screens)

So you end up with 3-5 compiled variants, not 64. The cache is sparse by nature.

---

## Step 4: Updated Render System

With all three refactors in place, the render system simplifies substantially. Here is the updated per-entity rendering code:

### engine/ecs/systems/render_system.cpp

```cpp
// engine/ecs/systems/render_system.cpp
// Updated for Chapter 35a — uses Material, ShaderCache variants,
// and the TextureSlot binding system.

#include "engine/ecs/systems/render_system.h"
#include "engine/ecs/components.h"
#include "engine/ecs/material.h"
#include "engine/renderer/shader_cache.h"
#include "engine/renderer/shader_variants.h"

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <entt/entt.hpp>

void renderSystem(entt::registry& registry,
                  const glm::mat4& view,
                  const glm::mat4& projection,
                  ShaderCache& shaderCache,
                  const glm::vec3& viewPos,
                  const glm::vec3& lightDir,
                  const glm::vec3& lightColour,
                  const glm::vec3& ambientColour,
                  const glm::mat4& lightSpaceMatrix)
{
    auto renderView = registry.view<Position, MeshRenderer, Material>();

    // ─── Track current shader to minimise state changes ──────
    GLuint currentShader = 0;

    for (auto [entity, pos, meshRenderer, mat] : renderView.each())
    {
        // ─── Shader variant selection ────────────────────────
        ShaderVariantKey key(mat.mapFlags);
        GLuint shader = shaderCache.getVariant("lit", key);

        if (shader != currentShader)
        {
            glUseProgram(shader);
            currentShader = shader;

            // Per-frame uniforms — set once per shader switch
            glUniformMatrix4fv(
                glGetUniformLocation(shader, "view"),
                1, GL_FALSE, glm::value_ptr(view));
            glUniformMatrix4fv(
                glGetUniformLocation(shader, "projection"),
                1, GL_FALSE, glm::value_ptr(projection));
            glUniformMatrix4fv(
                glGetUniformLocation(shader, "lightSpaceMatrix"),
                1, GL_FALSE, glm::value_ptr(lightSpaceMatrix));
            glUniform3fv(
                glGetUniformLocation(shader, "viewPos"),
                1, glm::value_ptr(viewPos));
            glUniform3fv(
                glGetUniformLocation(shader, "lightDir"),
                1, glm::value_ptr(lightDir));
            glUniform3fv(
                glGetUniformLocation(shader, "lightColour"),
                1, glm::value_ptr(lightColour));
            glUniform3fv(
                glGetUniformLocation(shader, "ambientColour"),
                1, glm::value_ptr(ambientColour));
        }

        // ─── Per-entity model matrix ─────────────────────────
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, pos.value);

        if (registry.all_of<Rotation>(entity))
        {
            auto& rot = registry.get<Rotation>(entity);
            model = glm::rotate(model, rot.angle, rot.axis);
        }

        if (registry.all_of<Scale>(entity))
        {
            auto& scale = registry.get<Scale>(entity);
            model = glm::scale(model, scale.value);
        }

        glUniformMatrix4fv(
            glGetUniformLocation(shader, "model"),
            1, GL_FALSE, glm::value_ptr(model));

        // ─── Material properties ─────────────────────────────
        glUniform1f(
            glGetUniformLocation(shader, "shininess"),
            mat.shininess);

        // ─── Bind textures ───────────────────────────────────
        mat.bindTextures();

        // ─── Draw ────────────────────────────────────────────
        glBindVertexArray(meshRenderer.vao);
        glDrawElements(GL_TRIANGLES, meshRenderer.indexCount,
                       GL_UNSIGNED_INT, nullptr);
    }

    glBindVertexArray(0);
    glUseProgram(0);
}
```

### Before vs After: Render System Per-Entity Code

```cpp
// BEFORE (Chapter 35): 18 lines of material binding per entity
glUniform1f(glGetUniformLocation(shaderProgram, "shininess"), mat.shininess);
glUniform1i(glGetUniformLocation(shaderProgram, "hasNormalMap"), mat.hasNormalMap);
glUniform1i(glGetUniformLocation(shaderProgram, "hasSpecularMap"), mat.hasSpecularMap);
glActiveTexture(GL_TEXTURE0);
glBindTexture(GL_TEXTURE_2D, mat.diffuseTexture);
glUniform1i(glGetUniformLocation(shaderProgram, "diffuseMap"), 0);
glActiveTexture(GL_TEXTURE1);
glBindTexture(GL_TEXTURE_2D, mat.normalMap);
glUniform1i(glGetUniformLocation(shaderProgram, "normalMap"), 1);
glActiveTexture(GL_TEXTURE2);
glBindTexture(GL_TEXTURE_2D, mat.specularMap);
glUniform1i(glGetUniformLocation(shaderProgram, "specularMap"), 2);

// AFTER (Chapter 35a): 3 lines
glUniform1f(glGetUniformLocation(shader, "shininess"), mat.shininess);
mat.bindTextures();
```

The sampler uniform assignments (`glUniform1i(... "diffuseMap", 0)`) moved to the ShaderCache, where they are set once at compile time. The boolean uniforms (`hasNormalMap`, `hasSpecularMap`) are gone entirely -- replaced by compile-time `#ifdef` blocks in the shader source. The texture binding logic moved into `Material::bindTextures()`.

The render system also now sorts implicitly by shader variant. Entities with the same `MapFlags` hit the same shader, so the `if (shader != currentShader)` check avoids redundant per-frame uniform uploads. For a real optimization pass, you would sort the entity view by `MapFlags` before iterating, but even without explicit sorting, the state-change tracking helps.

---

## Step 5: Setup Code

Here is how a typical entity setup looks after all three refactors:

```cpp
// ─── Loading base shader sources (once at startup) ───────────
shaderCache.loadShaderSource("lit",
    "assets/shaders/lit.vert",
    "assets/shaders/lit.frag");

// ─── Creating a mesh (tangents computed automatically) ────────
MeshData wallMesh = MeshFactory::createWall(4.0f, 3.0f);
GLuint wallVAO = uploadMesh(wallMesh.vertices, wallMesh.indices);

// ─── Loading a model (tangents computed automatically) ────────
auto pillarMeshes = MeshLoader::loadOBJ("assets/models/pillar.obj");

// ─── Creating an entity with the unified Material ────────────
auto wall = registry.create();
registry.emplace<Position>(wall, glm::vec3(0.0f, 1.5f, -5.0f));
registry.emplace<MeshRenderer>(wall, wallVAO,
    static_cast<int>(wallMesh.indices.size()));

Material wallMat;
wallMat.setAlbedo(resources.getTexture("brick_diffuse"));
wallMat.setNormalMap(resources.getTexture("brick_normal"));
wallMat.setSpecularMap(resources.getTexture("brick_specular"));
wallMat.shininess = 64.0f;
registry.emplace<Material>(wall, wallMat);

// ─── Entity without normal map ───────────────────────────────
auto floor = registry.create();
registry.emplace<Position>(floor, glm::vec3(0.0f));
registry.emplace<MeshRenderer>(floor, floorVAO, floorIndexCount);

Material floorMat;
floorMat.setAlbedo(resources.getTexture("concrete_diffuse"));
// No setNormalMap() call — mapFlags stays at None for this material.
// The render system will select the no-normal-map shader variant.
floorMat.shininess = 16.0f;
registry.emplace<Material>(floor, floorMat);
```

The floor entity's `Material` has `mapFlags == MapFlags::None`. The render system requests variant key 0 from the ShaderCache, which compiles the fragment shader without any `#define` flags. The normal map sampling code is not present in that variant at all -- no dead branches, no wasted texture lookups.

---

## Updated File Structure

After this chapter, your project has these new and modified files:

```
src/
  engine/
    ecs/
      material.h                  <- NEW: Material, MapFlags, TextureSlot
      components.h                <- MODIFIED: old Material struct removed
      systems/
        render_system.h           <- MODIFIED: updated signature
        render_system.cpp         <- MODIFIED: uses Material::bindTextures(),
                                              ShaderVariantKey, ShaderCache
    renderer/
      shader_cache.h              <- MODIFIED: added getVariant(),
                                              loadShaderSource(),
                                              variant caching
      shader_variants.h           <- NEW: ShaderVariantKey, buildDefines()
      tangent_compute.h           <- UNCHANGED (still used by MeshLoader)
      vertex.h                    <- UNCHANGED
    assets/
      mesh_loader.h               <- NEW: MeshLoader class
      obj_loader.h                <- UNCHANGED
      obj_loader.cpp              <- UNCHANGED
    core/
      mesh_factory.h              <- MODIFIED: returns MeshData via MeshLoader

  assets/
    shaders/
      lit.vert                    <- RENAMED from normal_mapped.vert (unchanged)
      lit.frag                    <- MODIFIED: #ifdef blocks replace bool uniforms
```

No new `.cpp` files. `material.h`, `shader_variants.h`, and `mesh_loader.h` are all header-only. The only modified `.cpp` file is `render_system.cpp`.

---

## Build and Test

Rebuild the project:

```bash
cmake --build build
```

The game should look identical to Chapter 35. Normal-mapped surfaces have per-pixel bumps. Non-normal-mapped surfaces use geometric normals. Specular highlights appear where specular maps define them. Nothing has changed visually.

If something does not work:

1. **Normal maps look broken.** Check that `MeshLoader::finaliseTangents()` is being called. If you still have old callsites that create vertices without going through `MeshLoader`, those meshes will have zero tangent vectors. Search your codebase for any remaining direct calls to `computeTangents()` and replace them with `MeshLoader::fromRaw()`.

2. **Shader fails to compile.** The `#define` injection must come after the `#version` line. Check that `injectDefines()` finds the `#version` directive correctly. If your shader has comments or whitespace before `#version`, the function handles that -- but verify the output by printing the modified source to the console during development.

3. **Textures appear on wrong surfaces.** Verify that the `TextureSlot` enum values match your `glActiveTexture` calls. The shadow map moved from unit 3 to unit 7 to make room for future PBR maps. Update the shadow pass to bind to `GL_TEXTURE0 + static_cast<int>(TextureSlot::ShadowMap)`.

4. **Missing shader variant.** If you see a black entity, the ShaderCache might be returning 0 (failed compilation). Check the console for compilation errors. A common issue is the `#define` string missing a trailing newline -- `buildDefines()` handles this, but if you modified it, verify.

5. **`Material` setters not called.** If you assign texture handles directly (`mat.normalMap = tex`) instead of using the setter (`mat.setNormalMap(tex)`), the `mapFlags` will not update. The render system will select the wrong shader variant. Search for any direct field assignments and replace them with setter calls. If you want to make this impossible to get wrong, you can make the texture handle fields private and only expose them through setters and getters. We left them public for this tutorial to keep the transition simple, but private fields with accessor methods is the more defensive approach.

---

## Before vs After: Summary

| Aspect | Before (Chapter 35) | After (Chapter 35a) |
|---|---|---|
| **Material struct** | 3 textures, 2 booleans, 1 float | 7 texture slots, 3 floats, 1 `MapFlags` bitfield |
| **Boolean flags** | `hasNormalMap`, `hasSpecularMap` (manual) | `MapFlags` (automatic via setters) |
| **Texture binding** | 12 lines of GL calls per entity | `mat.bindTextures()` (1 line) |
| **Sampler uniforms** | Set every frame per entity | Set once at shader compile time |
| **Shader branching** | `if (hasNormalMap)` at runtime | `#ifdef HAS_NORMAL_MAP` at compile time |
| **Shader selection** | One shader, booleans toggle paths | ShaderCache compiles variant per `MapFlags` |
| **Tangent computation** | Caller's responsibility (3+ callsites) | `MeshLoader` handles automatically |
| **Adding a new map type** | New GLuint + new bool + new binding code + new uniform + new shader branch | New GLuint + new `MapFlags` value + new setter + one line in `bindTextures()` |
| **New header files** | 0 | 3 (`material.h`, `shader_variants.h`, `mesh_loader.h`) |

---

## What We Accomplished

No new features. No visual changes. The game renders identically to the end of Chapter 35. Here is what changed underneath:

1. **The Material struct is unified and future-proof.** Texture slots, scalar properties, and a bitfield flag set all live in one component. The bitfield replaces individual booleans and is maintained automatically by setter methods. Adding new texture types for PBR (Chapter 44) means adding fields and flags -- the render system and shader infrastructure do not change.

2. **Texture binding is centralised.** `Material::bindTextures()` handles all texture unit activation. The render system does not need to know about individual texture slots. Sampler uniform assignment happens once at shader compile time, not every frame.

3. **Tangent computation is automatic.** `MeshLoader` computes tangent vectors for every mesh it produces, whether loaded from OBJ or created procedurally. No code outside of `MeshLoader` calls `computeTangents()`. You cannot forget.

4. **Shader variants replace runtime branching.** The uber-shader uses `#ifdef` blocks that are resolved at compile time. The ShaderCache compiles and caches variants on demand, keyed by `MapFlags`. Each material's feature set selects the exact shader it needs -- no dead branches, no warp divergence.

5. **The pattern is consistent.** This follows the same discipline as every cleanup chapter before it: identify scattered responsibility, consolidate it. Identify redundant state, eliminate it. Identify manual steps that can be automated, automate them. Chapters 5a through 30a cleaned up input, physics, HUD, particles, animation, and rendering infrastructure. Chapter 35a cleans up the material pipeline that connects them all to the GPU.

---

## What's Next

In **Chapter 36: Model Loading**, we build a complete OBJ and glTF loader. The `MeshLoader` class from this chapter is the foundation -- it already handles OBJ files and computes tangents automatically. Chapter 36 extends it with glTF support via the tinygltf library, multi-mesh models, embedded textures, and the material-to-component mapping that connects external model data to our unified `Material` component. Every model we load will have valid tangent vectors and correctly configured `MapFlags` from the moment it enters the engine.
