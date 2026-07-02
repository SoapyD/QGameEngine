# Chapter 36: Model Loading (OBJ & glTF)

## What You'll Learn
- Why a game engine needs to load artist-created 3D models
- The OBJ file format: line-by-line structure, vertices, normals, UVs, faces
- Writing a complete OBJ loader with vertex deduplication and quad triangulation
- The MTL material format: parsing diffuse, specular, and texture properties
- The glTF format: JSON structure, buffer hierarchy, PBR materials, skeleton data
- Integrating the tinygltf library for glTF/GLB loading
- An AssetManager that caches loaded models and textures
- GPU upload: converting parsed mesh data into VAO/VBO/EBO
- An updated MeshRenderer component and a stateless model loading system
- Using `std::string_view` for efficient string parsing without allocations

---

## Why Model Loading?

So far, QEngine creates meshes in two ways: procedurally generated shapes (cubes, planes for particles) and level geometry parsed from map files (Chapter 8). That is enough for walls, floors, and simple props — but it is not enough for a real game.

Characters, weapons, furniture, vehicles, decorative props — these are created by artists in tools like Blender, Maya, or 3ds Max, then exported as model files. The engine needs to read those files, extract the vertex data, materials, and (optionally) skeleton information, and upload everything to the GPU.

Two formats dominate:

```
OBJ                                glTF
─────────────────────              ─────────────────────
- Text-based (.obj)                - JSON + binary (.gltf/.glb)
- Simple, widely supported         - Modern, GPU-friendly
- No animation support             - Full animation & skeleton
- Separate material file (.mtl)    - PBR materials built-in
- Good for static props            - Good for characters & scenes
```

We will build a complete OBJ loader from scratch — it is simple enough to write by hand and teaches the fundamentals. For glTF, we will use the tinygltf library and show how to extract the data we need.

---

## OBJ Format Overview

An OBJ file is plain text. Each line starts with a keyword that describes what data follows. Here is a small example — a textured cube face (two triangles):

```
# Simple quad (two triangles)
# File: assets/models/crate.obj

mtllib crate.mtl
o Crate

v -1.0  1.0  1.0
v  1.0  1.0  1.0
v  1.0 -1.0  1.0
v -1.0 -1.0  1.0

vt 0.0 1.0
vt 1.0 1.0
vt 1.0 0.0
vt 0.0 0.0

vn 0.0 0.0 1.0

usemtl CrateMaterial

f 1/1/1 2/2/1 3/3/1
f 1/1/1 3/3/1 4/4/1
```

### Line-by-Line Breakdown

```
Keyword    Meaning                         Example
───────    ───────                         ───────
#          Comment                         # This is a comment
mtllib     Material library filename        mtllib crate.mtl
o          Object name                     o Crate
g          Group name                      g LeftArm
v          Vertex position (x y z)          v -1.0 1.0 1.0
vt         Texture coordinate (u v)         vt 0.0 1.0
vn         Vertex normal (x y z)            vn 0.0 0.0 1.0
usemtl     Switch active material           usemtl CrateMaterial
f          Face (vertex indices)            f 1/1/1 2/2/1 3/3/1
```

### Face Index Format

Each vertex in an `f` line uses the format `v/vt/vn` — position index, texture coordinate index, normal index. All indices are **1-based** (the first vertex is 1, not 0). Some variants:

```
f 1/1/1 2/2/1 3/3/1       Full: position / UV / normal
f 1//1  2//1  3//1         No UVs: position // normal
f 1/1   2/2   3/3          No normals: position / UV
f 1 2 3                    Position only
```

Faces can have three vertices (triangle) or four (quad). Quads must be split into two triangles during loading.

```
Quad face: f 1 2 3 4

Split into:
  Triangle 1: 1 2 3
  Triangle 2: 1 3 4

     1 ─────── 2          1 ─────── 2        1         2
     │         │          │       ╱          │ ╲       │
     │  quad   │   ──→    │  T1 ╱            │   ╲ T2  │
     │         │          │   ╱              │     ╲   │
     4 ─────── 3          4                  4 ─────── 3
```

---

## OBJ Loader — Data Structures

The loader parses the file into intermediate arrays, then assembles final vertex/index buffers with deduplication.

```cpp
// In src/engine/assets/obj_loader.h
#pragma once

#include <string>
#include <vector>
#include <glm/glm.hpp>

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};

struct OBJMesh {
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    std::string materialName;
};

struct OBJModel {
    std::vector<OBJMesh> meshes;
    std::string directory;  // For resolving relative texture paths
};

OBJModel loadOBJ(const std::string& filepath);
```

---

## OBJ Loader — Complete Implementation

```cpp
// In src/engine/assets/obj_loader.cpp

#include "engine/assets/obj_loader.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <unordered_map>
#include <tuple>

// ─── Index tuple for vertex deduplication ────────────────────────
// Each unique combination of position/uv/normal indices maps to one
// final vertex. This avoids duplicating vertices that share the same
// attribute combination across multiple faces.

struct IndexTuple {
    int posIdx;
    int uvIdx;
    int normIdx;

    bool operator==(const IndexTuple& other) const {
        return posIdx == other.posIdx &&
               uvIdx == other.uvIdx &&
               normIdx == other.normIdx;
    }
};

struct IndexTupleHash {
    std::size_t operator()(const IndexTuple& t) const {
        // Combine three hashes using bit shifting
        std::size_t h1 = std::hash<int>{}(t.posIdx);
        std::size_t h2 = std::hash<int>{}(t.uvIdx);
        std::size_t h3 = std::hash<int>{}(t.normIdx);
        return h1 ^ (h2 << 10) ^ (h3 << 20);
    }
};

// ─── Parse a single face vertex "v/vt/vn" ────────────────────────
// Returns an IndexTuple with 0-based indices. Missing components
// are set to -1.
static IndexTuple parseFaceVertex(const std::string& token) {
    IndexTuple result{-1, -1, -1};

    // Find the slashes
    size_t slash1 = token.find('/');
    if (slash1 == std::string::npos) {
        // Format: v
        result.posIdx = std::stoi(token) - 1;
        return result;
    }

    // Position is always present
    result.posIdx = std::stoi(token.substr(0, slash1)) - 1;

    size_t slash2 = token.find('/', slash1 + 1);
    if (slash2 == std::string::npos) {
        // Format: v/vt
        result.uvIdx = std::stoi(token.substr(slash1 + 1)) - 1;
        return result;
    }

    // UV might be empty (v//vn)
    std::string uvStr = token.substr(slash1 + 1, slash2 - slash1 - 1);
    if (!uvStr.empty()) {
        result.uvIdx = std::stoi(uvStr) - 1;
    }

    // Normal
    std::string normStr = token.substr(slash2 + 1);
    if (!normStr.empty()) {
        result.normIdx = std::stoi(normStr) - 1;
    }

    return result;
}

// ─── Main loader ─────────────────────────────────────────────────
OBJModel loadOBJ(const std::string& filepath) {
    OBJModel model;

    // Extract directory for resolving material/texture paths
    size_t lastSlash = filepath.find_last_of("/\\");
    model.directory = (lastSlash != std::string::npos)
                    ? filepath.substr(0, lastSlash + 1)
                    : "";

    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[OBJ] Failed to open: " << filepath << std::endl;
        return model;
    }

    // Temporary arrays — OBJ indices reference these globally
    std::vector<glm::vec3> tempPositions;
    std::vector<glm::vec2> tempUVs;
    std::vector<glm::vec3> tempNormals;

    // Current mesh being built
    OBJMesh currentMesh;
    std::unordered_map<IndexTuple, unsigned int, IndexTupleHash> vertexMap;
    bool hasMesh = false;

    std::string line;
    while (std::getline(file, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') continue;

        std::istringstream iss(line);
        std::string keyword;
        iss >> keyword;

        if (keyword == "v") {
            // ─── Vertex position ─────────────────────────────
            glm::vec3 pos;
            iss >> pos.x >> pos.y >> pos.z;
            tempPositions.push_back(pos);

        } else if (keyword == "vt") {
            // ─── Texture coordinate ──────────────────────────
            glm::vec2 uv;
            iss >> uv.x >> uv.y;
            tempUVs.push_back(uv);

        } else if (keyword == "vn") {
            // ─── Vertex normal ───────────────────────────────
            glm::vec3 norm;
            iss >> norm.x >> norm.y >> norm.z;
            tempNormals.push_back(norm);

        } else if (keyword == "usemtl") {
            // ─── Material switch ─────────────────────────────
            // If we already have faces, save the current mesh
            // and start a new one
            if (hasMesh && !currentMesh.indices.empty()) {
                model.meshes.push_back(std::move(currentMesh));
                currentMesh = OBJMesh{};
                vertexMap.clear();
            }

            iss >> currentMesh.materialName;
            hasMesh = true;

        } else if (keyword == "f") {
            // ─── Face ────────────────────────────────────────
            hasMesh = true;

            // Read all vertex tokens on this line
            std::vector<IndexTuple> faceIndices;
            std::string token;
            while (iss >> token) {
                faceIndices.push_back(parseFaceVertex(token));
            }

            // Triangulate: fan from the first vertex
            // Works for triangles (3 verts), quads (4), and n-gons
            for (size_t i = 1; i + 1 < faceIndices.size(); i++) {
                IndexTuple triangle[3] = {
                    faceIndices[0],
                    faceIndices[i],
                    faceIndices[i + 1]
                };

                for (const auto& idx : triangle) {
                    // Check if this exact index combination already exists
                    auto it = vertexMap.find(idx);
                    if (it != vertexMap.end()) {
                        // Reuse existing vertex
                        currentMesh.indices.push_back(it->second);
                    } else {
                        // Create a new vertex
                        Vertex vert{};

                        if (idx.posIdx >= 0 && idx.posIdx < (int)tempPositions.size()) {
                            vert.position = tempPositions[idx.posIdx];
                        }
                        if (idx.uvIdx >= 0 && idx.uvIdx < (int)tempUVs.size()) {
                            vert.uv = tempUVs[idx.uvIdx];
                        }
                        if (idx.normIdx >= 0 && idx.normIdx < (int)tempNormals.size()) {
                            vert.normal = tempNormals[idx.normIdx];
                        }

                        unsigned int newIndex = static_cast<unsigned int>(
                            currentMesh.vertices.size());
                        currentMesh.vertices.push_back(vert);
                        currentMesh.indices.push_back(newIndex);
                        vertexMap[idx] = newIndex;
                    }
                }
            }
        }
        // We silently ignore: mtllib, o, g, s — handled elsewhere or not needed
    }

    // Don't forget the last mesh
    if (hasMesh && !currentMesh.indices.empty()) {
        model.meshes.push_back(std::move(currentMesh));
    }

    std::cout << "[OBJ] Loaded " << filepath << ": "
              << model.meshes.size() << " mesh(es), "
              << tempPositions.size() << " positions" << std::endl;

    return model;
}
```

### How Vertex Deduplication Works

Without deduplication, every face vertex creates a new entry in the vertex buffer — even if the same position/UV/normal combination appeared in a previous face. Deduplication uses a hash map to detect repeats:

```
Face 1: f 1/1/1  2/2/1  3/3/1
Face 2: f 1/1/1  3/3/1  4/4/1
                  ^^^^^
                  This vertex appears in both faces.
                  Without dedup: 6 vertices in the buffer.
                  With dedup:    4 vertices in the buffer.

Vertex Map:
  {1,1,1} → index 0
  {2,2,1} → index 1
  {3,3,1} → index 2     ← reused by Face 2
  {4,4,1} → index 3

Index Buffer: [0, 1, 2, 0, 2, 3]
```

This matters. A complex model with shared vertices (smooth surfaces, UV seams) can save 30-50% of vertex buffer memory through deduplication.

---

## MTL Material Format

OBJ files reference materials through `mtllib` (the filename) and `usemtl` (which material to apply). The `.mtl` file defines material properties:

```
# File: assets/models/crate.mtl

newmtl CrateMaterial
Kd 0.8 0.8 0.8          # Diffuse colour (RGB, 0-1)
Ks 0.2 0.2 0.2          # Specular colour
Ns 32.0                  # Specular exponent (shininess)
d 1.0                    # Dissolve (opacity, 1.0 = fully opaque)
map_Kd crate_diffuse.png # Diffuse texture
map_Bump crate_normal.png # Normal map (also seen as map_Kn)
```

### MTL Parser

```cpp
// In src/engine/assets/mtl_loader.h
#pragma once

#include <string>
#include <unordered_map>
#include <glm/glm.hpp>

struct OBJMaterial {
    std::string name;
    glm::vec3 diffuseColour  = glm::vec3(0.8f);
    glm::vec3 specularColour = glm::vec3(0.0f);
    float shininess          = 1.0f;
    float opacity            = 1.0f;
    std::string diffuseTexturePath;   // map_Kd
    std::string normalMapPath;        // map_Bump or map_Kn
};

// Returns a map of material name → material data
std::unordered_map<std::string, OBJMaterial> loadMTL(const std::string& filepath);
```

```cpp
// In src/engine/assets/mtl_loader.cpp

#include "engine/assets/mtl_loader.h"
#include <fstream>
#include <sstream>
#include <iostream>

std::unordered_map<std::string, OBJMaterial> loadMTL(const std::string& filepath) {
    std::unordered_map<std::string, OBJMaterial> materials;

    // Extract directory for resolving texture paths
    std::string directory;
    size_t lastSlash = filepath.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        directory = filepath.substr(0, lastSlash + 1);
    }

    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[MTL] Failed to open: " << filepath << std::endl;
        return materials;
    }

    OBJMaterial* current = nullptr;
    std::string line;

    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        std::istringstream iss(line);
        std::string keyword;
        iss >> keyword;

        if (keyword == "newmtl") {
            // ─── New material definition ─────────────────────
            std::string name;
            iss >> name;
            materials[name] = OBJMaterial{};
            materials[name].name = name;
            current = &materials[name];

        } else if (!current) {
            continue;  // No active material — skip

        } else if (keyword == "Kd") {
            // ─── Diffuse colour ──────────────────────────────
            iss >> current->diffuseColour.r
                >> current->diffuseColour.g
                >> current->diffuseColour.b;

        } else if (keyword == "Ks") {
            // ─── Specular colour ─────────────────────────────
            iss >> current->specularColour.r
                >> current->specularColour.g
                >> current->specularColour.b;

        } else if (keyword == "Ns") {
            // ─── Shininess ───────────────────────────────────
            iss >> current->shininess;

        } else if (keyword == "d") {
            // ─── Opacity ─────────────────────────────────────
            iss >> current->opacity;

        } else if (keyword == "map_Kd") {
            // ─── Diffuse texture ─────────────────────────────
            std::string texPath;
            iss >> texPath;
            current->diffuseTexturePath = directory + texPath;

        } else if (keyword == "map_Bump" || keyword == "map_Kn") {
            // ─── Normal map ──────────────────────────────────
            std::string texPath;
            iss >> texPath;
            current->normalMapPath = directory + texPath;
        }
    }

    std::cout << "[MTL] Loaded " << filepath << ": "
              << materials.size() << " material(s)" << std::endl;

    return materials;
}
```

To connect the OBJ loader with the MTL parser, modify the OBJ loader to capture `mtllib` lines:

```cpp
// In the OBJ loader's while loop, add this case:

} else if (keyword == "mtllib") {
    std::string mtlFile;
    iss >> mtlFile;
    // Load materials — store them on the model or pass to the asset manager
    auto materials = loadMTL(model.directory + mtlFile);
    // Materials are looked up later by the mesh's materialName field
}
```

---

## glTF Format Overview

glTF (Graphics Language Transmission Format) is the modern standard for 3D model interchange. Where OBJ is a flat text file, glTF is a structured hierarchy designed to map closely to GPU data.

### Two Variants

- **`.gltf`** — JSON file with separate `.bin` buffer files and image files
- **`.glb`** — Single binary container with JSON header and embedded buffers

For game engines, `.glb` is preferred: one file, no missing dependencies.

### Data Hierarchy

```
glTF File
├── Scenes[]
│   └── Nodes[]                    ← Transform hierarchy
│       ├── translation/rotation/scale
│       ├── Mesh reference
│       │   └── Primitives[]       ← One per material
│       │       ├── Attributes     ← POSITION, NORMAL, TEXCOORD_0, JOINTS_0, WEIGHTS_0
│       │       ├── Indices
│       │       └── Material ref
│       └── Skin reference         ← Skeleton binding
│           ├── joints[]           ← Node indices for bones
│           └── inverseBindMatrices
├── Meshes[]
│   └── Primitives[]
│       └── Accessors[]            ← Typed views into buffer data
│           └── BufferViews[]      ← Byte ranges within buffers
│               └── Buffers[]      ← Raw binary data
├── Materials[]
│   └── pbrMetallicRoughness
│       ├── baseColorTexture
│       ├── metallicRoughnessTexture
│       ├── baseColorFactor
│       ├── metallicFactor
│       └── roughnessFactor
├── Textures[]
│   └── Images[]                   ← PNG/JPEG data (embedded or external)
├── Animations[]
│   └── Channels[]                 ← target node + path (translation/rotation/scale)
│       └── Sampler               ← keyframe times + values
└── Skins[]
    ├── joints[]                   ← Which nodes are bones
    └── inverseBindMatrices        ← Accessor for the bind pose matrices
```

### How Data Flows from Buffer to GPU

```
Buffer (raw bytes)
    │
    ▼
BufferView (byte offset + length + stride)
    │
    ▼
Accessor (component type, count, min/max)
    │
    ▼
Attribute (POSITION, NORMAL, etc.)
    │
    ▼
GPU Vertex Buffer
```

The key insight: glTF's accessor/bufferView/buffer chain is designed to map almost directly to OpenGL's `glVertexAttribPointer`. The buffer is the VBO data, the bufferView defines the byte range and stride, and the accessor describes the data type and count.

We will not write a glTF parser from scratch — the format's JSON schema and binary encoding make that a large project with many edge cases. Instead, we use the **tinygltf** library.

---

## tinygltf Integration

[tinygltf](https://github.com/syoyo/tinygltf) is a header-only C++ library that parses glTF 2.0 files into a clean data structure. It depends on nlohmann/json (which QEngine already uses from Chapter 23) and stb_image.

### CMake Setup

```cmake
# In CMakeLists.txt — add tinygltf as a header-only dependency

# Option 1: Subdirectory
add_subdirectory(external/tinygltf)
target_link_libraries(QEngine PRIVATE tinygltf)

# Option 2: Header-only (just include the path)
target_include_directories(QEngine PRIVATE external/tinygltf)
```

In exactly one `.cpp` file, define the implementation:

```cpp
// In src/engine/assets/gltf_loader.cpp

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION  // Only if not already defined elsewhere
#include <tiny_gltf.h>
```

### Loading a GLB File

```cpp
// In src/engine/assets/gltf_loader.cpp

#include <tiny_gltf.h>
#include <iostream>
#include "engine/assets/obj_loader.h"  // Reuse Vertex and OBJMesh types

bool loadGLTFModel(const std::string& filepath, tinygltf::Model& model) {
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    // Detect format by extension
    bool success = false;
    if (filepath.ends_with(".glb")) {
        success = loader.LoadBinaryFromFile(&model, &err, &warn, filepath);
    } else {
        success = loader.LoadASCIIFromFile(&model, &err, &warn, filepath);
    }

    if (!warn.empty()) {
        std::cerr << "[glTF] Warning: " << warn << std::endl;
    }
    if (!err.empty()) {
        std::cerr << "[glTF] Error: " << err << std::endl;
    }
    if (!success) {
        std::cerr << "[glTF] Failed to load: " << filepath << std::endl;
    }

    return success;
}
```

### Extracting Mesh Data

The core task: walk through the glTF primitives and pull out vertex positions, normals, UVs, and indices.

```cpp
// In src/engine/assets/gltf_loader.cpp

// Helper: get a typed pointer to accessor data
template <typename T>
const T* getAccessorData(const tinygltf::Model& model,
                         const tinygltf::Accessor& accessor) {
    const auto& bufferView = model.bufferViews[accessor.bufferView];
    const auto& buffer = model.buffers[bufferView.buffer];
    return reinterpret_cast<const T*>(
        buffer.data.data() + bufferView.byteOffset + accessor.byteOffset);
}

std::vector<OBJMesh> extractMeshes(const tinygltf::Model& model) {
    std::vector<OBJMesh> result;

    for (const auto& mesh : model.meshes) {
        for (const auto& primitive : mesh.primitives) {
            OBJMesh outMesh;

            // ─── Positions (required) ────────────────────────
            auto posIt = primitive.attributes.find("POSITION");
            if (posIt == primitive.attributes.end()) continue;

            const auto& posAccessor = model.accessors[posIt->second];
            const float* posData = getAccessorData<float>(model, posAccessor);
            size_t vertexCount = posAccessor.count;

            // ─── Normals (optional) ──────────────────────────
            const float* normData = nullptr;
            auto normIt = primitive.attributes.find("NORMAL");
            if (normIt != primitive.attributes.end()) {
                normData = getAccessorData<float>(
                    model, model.accessors[normIt->second]);
            }

            // ─── UVs (optional) ──────────────────────────────
            const float* uvData = nullptr;
            auto uvIt = primitive.attributes.find("TEXCOORD_0");
            if (uvIt != primitive.attributes.end()) {
                uvData = getAccessorData<float>(
                    model, model.accessors[uvIt->second]);
            }

            // ─── Build vertices ──────────────────────────────
            outMesh.vertices.resize(vertexCount);
            for (size_t i = 0; i < vertexCount; i++) {
                outMesh.vertices[i].position = {
                    posData[i * 3 + 0],
                    posData[i * 3 + 1],
                    posData[i * 3 + 2]
                };

                if (normData) {
                    outMesh.vertices[i].normal = {
                        normData[i * 3 + 0],
                        normData[i * 3 + 1],
                        normData[i * 3 + 2]
                    };
                }

                if (uvData) {
                    outMesh.vertices[i].uv = {
                        uvData[i * 2 + 0],
                        uvData[i * 2 + 1]
                    };
                }
            }

            // ─── Indices ─────────────────────────────────────
            if (primitive.indices >= 0) {
                const auto& idxAccessor = model.accessors[primitive.indices];
                const auto& bufView = model.bufferViews[idxAccessor.bufferView];
                const auto& buf = model.buffers[bufView.buffer];
                const uint8_t* rawData = buf.data.data()
                                       + bufView.byteOffset
                                       + idxAccessor.byteOffset;

                outMesh.indices.resize(idxAccessor.count);

                // glTF indices can be uint8, uint16, or uint32
                for (size_t i = 0; i < idxAccessor.count; i++) {
                    switch (idxAccessor.componentType) {
                        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                            outMesh.indices[i] = rawData[i];
                            break;
                        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                            outMesh.indices[i] =
                                reinterpret_cast<const uint16_t*>(rawData)[i];
                            break;
                        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                            outMesh.indices[i] =
                                reinterpret_cast<const uint32_t*>(rawData)[i];
                            break;
                    }
                }
            }

            result.push_back(std::move(outMesh));
        }
    }

    return result;
}
```

### Extracting Skeleton Data

For characters with skeletal animation (Chapter 33), glTF stores the bone hierarchy through the `Skin` object. Each skin references a set of nodes as joints and an accessor containing the inverse bind matrices:

```cpp
// In src/engine/assets/gltf_loader.cpp

#include "engine/animation/skeleton.h"  // Skeleton, Bone from Ch 33

Skeleton extractSkeleton(const tinygltf::Model& model, int skinIndex) {
    Skeleton skeleton;
    const auto& skin = model.skins[skinIndex];

    // ─── Inverse bind matrices ───────────────────────────────
    const auto& ibmAccessor = model.accessors[skin.inverseBindMatrices];
    const float* ibmData = getAccessorData<float>(model, ibmAccessor);

    // ─── Build bone array ────────────────────────────────────
    // skin.joints is a list of node indices that act as bones.
    // The order matches the inverse bind matrix order.
    std::unordered_map<int, int> nodeToJointIndex;

    for (size_t i = 0; i < skin.joints.size(); i++) {
        int nodeIdx = skin.joints[i];
        nodeToJointIndex[nodeIdx] = static_cast<int>(i);

        Bone bone;
        bone.name = model.nodes[nodeIdx].name;

        // Copy the inverse bind matrix (column-major, 16 floats)
        memcpy(&bone.offsetMatrix, &ibmData[i * 16], 16 * sizeof(float));

        // Parent index: find this node's parent among the joints
        bone.parentIndex = -1;  // Default: root
        // We resolve parents after all bones are added

        skeleton.bones.push_back(bone);
        skeleton.boneNameToIndex[bone.name] = static_cast<int>(i);
    }

    // ─── Resolve parent indices ──────────────────────────────
    // Walk the node tree to find parent-child relationships
    for (size_t nodeIdx = 0; nodeIdx < model.nodes.size(); nodeIdx++) {
        const auto& node = model.nodes[nodeIdx];
        auto parentIt = nodeToJointIndex.find(static_cast<int>(nodeIdx));
        if (parentIt == nodeToJointIndex.end()) continue;

        int parentJointIdx = parentIt->second;

        for (int childNodeIdx : node.children) {
            auto childIt = nodeToJointIndex.find(childNodeIdx);
            if (childIt != nodeToJointIndex.end()) {
                skeleton.bones[childIt->second].parentIndex = parentJointIdx;
            }
        }
    }

    return skeleton;
}
```

This ties directly into the Skeleton struct from Chapter 33. Once extracted, the skeleton feeds into the animation system — the Animator component stores the current animation clip and time, and the animationSystem computes bone transforms each frame.

---

## Asset Manager

Loading the same model or texture twice is wasteful. The AssetManager caches everything by file path:

```cpp
// In src/engine/assets/asset_manager.h
#pragma once

#include <string>
#include <unordered_map>
#include <iostream>
#include <glad/glad.h>
#include "engine/assets/obj_loader.h"
#include "engine/assets/mtl_loader.h"

class AssetManager {
public:
    // ─── Models ──────────────────────────────────────────────
    const OBJModel& getModel(const std::string& path) {
        auto it = m_models.find(path);
        if (it != m_models.end()) {
            return it->second;
        }

        // First request — load from disk
        std::cout << "[AssetManager] Loading model: " << path << std::endl;
        m_models[path] = loadOBJ(path);
        return m_models[path];
    }

    // ─── Materials ───────────────────────────────────────────
    const std::unordered_map<std::string, OBJMaterial>&
    getMaterials(const std::string& mtlPath) {
        auto it = m_materials.find(mtlPath);
        if (it != m_materials.end()) {
            return it->second;
        }

        m_materials[mtlPath] = loadMTL(mtlPath);
        return m_materials[mtlPath];
    }

    // ─── Textures ────────────────────────────────────────────
    GLuint getTexture(const std::string& path) {
        auto it = m_textures.find(path);
        if (it != m_textures.end()) {
            return it->second;
        }

        // Load using stb_image (same as your existing texture loader)
        GLuint tex = loadTextureFromFile(path);
        m_textures[path] = tex;
        return tex;
    }

    // ─── Cleanup ─────────────────────────────────────────────
    // Call during level transitions (Ch 34) to free GPU memory
    void clear() {
        for (auto& [path, tex] : m_textures) {
            glDeleteTextures(1, &tex);
        }
        m_textures.clear();
        m_models.clear();
        m_materials.clear();
        std::cout << "[AssetManager] All assets cleared" << std::endl;
    }

private:
    std::unordered_map<std::string, OBJModel> m_models;
    std::unordered_map<std::string, std::unordered_map<std::string, OBJMaterial>> m_materials;
    std::unordered_map<std::string, GLuint> m_textures;

    // Texture loading using stb_image — same approach as earlier chapters
    GLuint loadTextureFromFile(const std::string& path);
};
```

The AssetManager is **not** a singleton. It is created in `main.cpp` and passed by reference to whatever needs it. This makes testing easier — you can create a fresh AssetManager for each test without worrying about global state.

```
Asset Loading Pipeline:
                                                    ┌──────────┐
  "assets/models/grunt.obj"                         │          │
         │                                          │   GPU    │
         ▼                                          │          │
  ┌──────────────┐    ┌───────────┐    ┌─────────┐ │ ┌──────┐ │
  │ AssetManager │───→│ OBJ/glTF  │───→│ Upload  │─┼→│ VAO  │ │
  │   (cache)    │    │  Parser   │    │ to GPU  │ │ │ VBO  │ │
  └──────────────┘    └───────────┘    └─────────┘ │ │ EBO  │ │
         │                                          │ └──────┘ │
         │ already cached?                          │          │
         │ return immediately                       │ ┌──────┐ │
         └──────────────────────────────────────────┼→│ TEX  │ │
                                                    │ └──────┘ │
                                                    └──────────┘
```

---

## GPU Upload

Once the OBJ loader produces vertex and index data, it needs to be uploaded to the GPU as OpenGL buffer objects. This function takes a parsed mesh and returns a VAO handle:

```cpp
// In src/engine/assets/mesh_upload.h
#pragma once

#include <glad/glad.h>
#include "engine/assets/obj_loader.h"

struct GPUMesh {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    int indexCount = 0;
};

inline GPUMesh uploadMesh(const OBJMesh& mesh) {
    GPUMesh gpu;
    gpu.indexCount = static_cast<int>(mesh.indices.size());

    glGenVertexArrays(1, &gpu.vao);
    glGenBuffers(1, &gpu.vbo);
    glGenBuffers(1, &gpu.ebo);

    glBindVertexArray(gpu.vao);

    // ─── Vertex buffer ───────────────────────────────────────
    glBindBuffer(GL_ARRAY_BUFFER, gpu.vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 mesh.vertices.size() * sizeof(Vertex),
                 mesh.vertices.data(),
                 GL_STATIC_DRAW);

    // ─── Index buffer ────────────────────────────────────────
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 mesh.indices.size() * sizeof(unsigned int),
                 mesh.indices.data(),
                 GL_STATIC_DRAW);

    // ─── Vertex attributes ───────────────────────────────────
    // Position: location 0
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                          sizeof(Vertex),
                          (void*)offsetof(Vertex, position));

    // Normal: location 1
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                          sizeof(Vertex),
                          (void*)offsetof(Vertex, normal));

    // UV: location 2
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE,
                          sizeof(Vertex),
                          (void*)offsetof(Vertex, uv));

    glBindVertexArray(0);

    return gpu;
}

// Free GPU resources when no longer needed
inline void deleteMesh(GPUMesh& gpu) {
    if (gpu.vao) glDeleteVertexArrays(1, &gpu.vao);
    if (gpu.vbo) glDeleteBuffers(1, &gpu.vbo);
    if (gpu.ebo) glDeleteBuffers(1, &gpu.ebo);
    gpu = GPUMesh{};
}
```

---

## Updated MeshRenderer Component

The MeshRenderer component stores everything the render system needs to draw a loaded model. Pure data — no behaviour:

```cpp
// In src/engine/ecs/components.h

struct MeshRenderer {
    std::string modelPath;    // "assets/models/grunt.obj"
    GLuint vao       = 0;
    GLuint texture   = 0;
    GLuint normalMap = 0;
    int indexCount   = 0;
    bool loaded      = false; // Has this model been loaded and uploaded?
};
```

The `loaded` flag is the key. When an entity is created (from a level file, a spawner, or editor placement), it gets a MeshRenderer with just a `modelPath`. The actual loading happens later, when the model loading system processes it.

---

## Model Loading System

A stateless free function that checks every MeshRenderer each frame. If a model has not been loaded yet, it loads it via the AssetManager, uploads the mesh data to the GPU, and marks it as loaded:

```cpp
// In src/engine/ecs/systems/model_loading_system.h
#pragma once

#include <entt/entt.hpp>
#include "engine/assets/asset_manager.h"
#include "engine/assets/mesh_upload.h"
#include "engine/ecs/components.h"

// Free function — no state. The AssetManager provides caching.
inline void modelLoadingSystem(entt::registry& registry,
                               AssetManager& assets) {
    auto view = registry.view<MeshRenderer>();

    for (auto [entity, mesh] : view.each()) {
        if (mesh.loaded) continue;  // Already loaded — skip
        if (mesh.modelPath.empty()) continue;  // No model assigned

        // ─── Load the model (cached after first load) ────────
        const OBJModel& model = assets.getModel(mesh.modelPath);
        if (model.meshes.empty()) {
            // Model failed to load or has no geometry
            mesh.loaded = true;  // Mark loaded to avoid retrying every frame
            continue;
        }

        // ─── Upload the first mesh to the GPU ────────────────
        // For multi-mesh models, you would create child entities
        // or use a MeshGroup component. For simplicity, we take
        // the first mesh.
        GPUMesh gpu = uploadMesh(model.meshes[0]);
        mesh.vao        = gpu.vao;
        mesh.indexCount  = gpu.indexCount;

        // ─── Load textures if a material is referenced ───────
        const std::string& matName = model.meshes[0].materialName;
        if (!matName.empty()) {
            // Derive the .mtl path from the .obj path
            std::string mtlPath = model.directory + matName + ".mtl";
            // Or: parse mtllib from the OBJ file and store it on the model

            const auto& materials = assets.getMaterials(mtlPath);
            auto matIt = materials.find(matName);
            if (matIt != materials.end()) {
                const OBJMaterial& mat = matIt->second;

                if (!mat.diffuseTexturePath.empty()) {
                    mesh.texture = assets.getTexture(mat.diffuseTexturePath);
                }
                if (!mat.normalMapPath.empty()) {
                    mesh.normalMap = assets.getTexture(mat.normalMapPath);
                }
            }
        }

        mesh.loaded = true;
    }
}
```

### Why Lazy Loading?

Loading models on the frame they are first needed — rather than at level load time — has two benefits:

1. **Level load times stay fast.** Only models visible in the opening area need to load immediately. Distant enemies or hidden props load when the player approaches and the entity enters the scene.

2. **Spawned entities work automatically.** When the spawner system (Chapter 10) creates a new enemy mid-game, the new entity gets a MeshRenderer with `loaded = false`. The model loading system picks it up on the next frame — no special spawning code needed.

The downside is a potential hitch on the first frame a model appears. For a production engine, you would preload models during the loading screen. The AssetManager's caching means subsequent entities using the same model path incur zero disk I/O.

### Wiring Into the Game Loop

```cpp
// In src/game/states/playing_state.cpp — update()

void PlayingState::update(float dt) {
    // Model loading runs first — ensures meshes are ready before rendering
    modelLoadingSystem(m_registry, m_assetManager);

    inputSystem(m_registry, m_window, m_camera, dt);
    aiSystem(m_registry, dt);
    physicsSystem(m_registry, dt);
    // ... other systems ...
}
```

---

## C++ Concept: `std::string_view` for Parsing

The OBJ loader spends most of its time splitting strings — reading keywords, extracting numbers, slicing face indices. Every `std::string::substr()` call allocates a new string on the heap. For a model with 100,000 faces, that is hundreds of thousands of unnecessary allocations.

### The Problem

```cpp
// Every substr allocates a new std::string
std::string line = "f 1/2/3 4/5/6 7/8/9";
std::string keyword = line.substr(0, 1);          // Allocates "f"
std::string rest = line.substr(2);                 // Allocates "1/2/3 4/5/6 7/8/9"
std::string token = rest.substr(0, rest.find(' ')); // Allocates "1/2/3"
```

### The Solution: `std::string_view`

`std::string_view` (C++17) is a non-owning reference to a string. It stores a pointer and a length — no allocation, no copy. Think of it as a "window" into an existing string:

```cpp
#include <string_view>

std::string line = "f 1/2/3 4/5/6 7/8/9";
std::string_view sv(line);

std::string_view keyword = sv.substr(0, 1);  // No allocation — just a pointer + length
std::string_view rest = sv.substr(2);         // No allocation
```

### Tokenising with string_view

Here is how you would split a line into tokens without any heap allocations:

```cpp
#include <string_view>
#include <vector>

std::vector<std::string_view> tokenise(std::string_view line) {
    std::vector<std::string_view> tokens;

    size_t start = 0;
    while (start < line.size()) {
        // Skip whitespace
        size_t tokenStart = line.find_first_not_of(" \t", start);
        if (tokenStart == std::string_view::npos) break;

        // Find end of token
        size_t tokenEnd = line.find_first_of(" \t", tokenStart);
        if (tokenEnd == std::string_view::npos) {
            tokenEnd = line.size();
        }

        tokens.push_back(line.substr(tokenStart, tokenEnd - tokenStart));
        start = tokenEnd;
    }

    return tokens;
}

// Usage in an OBJ parser:
void parseLine(std::string_view line) {
    auto tokens = tokenise(line);
    if (tokens.empty()) return;

    if (tokens[0] == "v" && tokens.size() >= 4) {
        // Convert string_view to float — need a temporary std::string
        // or use std::from_chars (C++17, no allocation):
        float x, y, z;
        std::from_chars(tokens[1].data(), tokens[1].data() + tokens[1].size(), x);
        std::from_chars(tokens[2].data(), tokens[2].data() + tokens[2].size(), y);
        std::from_chars(tokens[3].data(), tokens[3].data() + tokens[3].size(), z);
        // Add to positions array...
    }
}
```

### When to Use string_view vs string

```
Use std::string_view when:                 Use std::string when:
─────────────────────────                  ──────────────────────
- You only need to read the data           - You need to store the data
- The source string outlives the view      - The source might be destroyed
- Parsing / tokenising / searching         - Storing in a data structure
- Function parameters (read-only)          - Return values that outlive the call
- Comparing against literals               - Building strings with concatenation
```

The critical rule: **a `string_view` does not own the data it points to.** If the original string is destroyed, the `string_view` becomes a dangling reference — just like a dangling pointer. Never store a `string_view` in a long-lived data structure unless you can guarantee the source string's lifetime.

In the OBJ loader, `string_view` is safe for parsing individual lines because the `std::string line` variable lives for the entire loop iteration. But when we store `materialName` on the mesh, we use `std::string` because the material name must outlive the parsing loop.

---

## What's Next

In **Chapter 37: Pathfinding**, we give QEngine's AI the ability to navigate around obstacles. Right now enemies walk in straight lines toward the player — they get stuck on walls, fall into pits, and walk through furniture. A navigation mesh defines the walkable surface, and the A* algorithm finds the shortest path across it. Enemies will path around corners, through doorways, and across bridges — making them feel like they actually understand the space they inhabit.
