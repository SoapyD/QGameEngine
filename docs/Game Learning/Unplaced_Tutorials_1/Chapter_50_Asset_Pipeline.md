# Chapter 50: Asset Pipeline & Preprocessing

## What You'll Learn
- Why real engines separate raw assets from cooked assets, and how this affects load times
- Designing a binary mesh format (.qmesh) with vertex deduplication, index optimisation, and precomputed tangents
- Preprocessing textures offline: mipmap generation, sRGB metadata, and compressed formats
- Building an asset compiler as a standalone command-line tool that scans, processes, and outputs cooked data
- Incremental rebuilds: only reprocessing assets whose source files have changed
- An asset manifest that tracks every cooked asset, its type, and its dependencies
- Updating ResourceManager to load cooked binary assets in release and fall back to raw assets in development
- Asset handles: replacing string lookups with integer IDs for fast runtime access
- C++ concept: offline tools vs runtime code -- different constraints, different trade-offs

---

## Where We Left Off

Over the last several chapters we gave QEngine proper tooling. Chapter 47 added an ImGui-based editor overlay. Chapter 48 built a level editor that saves scenes to JSON. Chapter 49 embedded Lua scripting for game configuration and behaviour. The engine is now a genuine authoring environment -- you can build levels, tweak parameters, and script logic without recompiling C++.

But there is a problem hiding in every `loadTexture()`, `loadOBJ()`, and `loadAnimation()` call. Watch what happens when the game starts:

```
CURRENT STARTUP SEQUENCE
──────────────────────────────────────────────────────────────────
  [0.00s] Window created, OpenGL context ready
  [0.01s] Loading brick_wall.png...        (stb_image decodes PNG)
  [0.08s] Loading stone_floor.png...       (stb_image decodes PNG)
  [0.14s] Loading enemy_grunt.obj...       (text parsing, vertex dedup)
  [0.31s] Loading enemy_grunt_diffuse.png...
  [0.38s] Loading shotgun.gltf...          (JSON parsing, buffer decode)
  [0.52s] Computing tangent vectors...     (MikkTSpace on every mesh)
  [0.71s] Generating mipmaps...            (glGenerateMipmap on GPU)
  [0.78s] Loading level_01.json...
  [0.85s] Loading 14 more textures...
  [1.40s] Loading 6 more models...
  [2.10s] Ready.
  ──────────────────────────────────────────────────────────────────
  Total: 2.1 seconds for a small test level.
  A real game with hundreds of assets: 15-30 seconds.
```

Every frame of that loading time, the CPU is doing work that could have been done once, offline, before the game ever ran. PNG decompression, OBJ text parsing, vertex deduplication, tangent computation, mipmap generation -- these operations produce the same result every time for the same input file. We are paying the cost on every launch.

Commercial engines solve this with an **asset pipeline**: a preprocessing step that converts raw source assets into optimised, binary "cooked" formats. The game loads only the cooked data, which is already in the exact format the GPU and runtime expect.

```
ASSET PIPELINE OVERVIEW

  AUTHORING TIME                    BUILD TIME                    RUNTIME
  ─────────────                     ──────────                    ───────
  Artist creates                    Asset compiler                Game loads
  raw assets                        processes them                cooked assets

  brick_wall.png ──────┐
  stone_floor.png ─────┤
  enemy_grunt.obj ─────┼────→  [ Asset Compiler ] ────→  brick_wall.qtex
  enemy_grunt.png ─────┤       (standalone tool)         stone_floor.qtex
  shotgun.gltf ────────┤                                 enemy_grunt.qmesh
  level_01.json ───────┘                                 enemy_grunt.qtex
                                                         shotgun.qmesh
                                                         level_01.json
                                                         manifest.json

  assets_raw/                                            assets/
  (source files,                                         (cooked files,
   version controlled)                                    generated output)
```

After cooking, the startup sequence looks like this:

```
COOKED STARTUP SEQUENCE
──────────────────────────────────────────────────────────────────
  [0.00s] Window created, OpenGL context ready
  [0.01s] Loading manifest.json...         (knows every asset upfront)
  [0.02s] Loading brick_wall.qtex...       (binary, pre-mipmapped)
  [0.04s] Loading stone_floor.qtex...      (binary, pre-mipmapped)
  [0.05s] Loading enemy_grunt.qmesh...     (binary, indexed, tangents done)
  [0.06s] Loading enemy_grunt.qtex...
  [0.07s] Loading shotgun.qmesh...
  [0.08s] Loading level_01.json...
  [0.10s] Loading remaining assets...
  [0.18s] Ready.
──────────────────────────────────────────────────────────────────
  Total: 0.18 seconds. Over 10x faster.
```

The game does no parsing, no decompression, no computation. It reads binary blobs that map directly to GPU upload calls. That is the goal.

---

## Directory Structure

The pipeline introduces a clear separation between source and output:

```
project/
├── assets_raw/                    ← Source assets (version controlled)
│   ├── textures/
│   │   ├── brick_wall.png
│   │   ├── stone_floor.png
│   │   └── enemy_grunt_diffuse.png
│   ├── models/
│   │   ├── enemy_grunt.obj
│   │   ├── enemy_grunt.mtl
│   │   └── shotgun.gltf
│   ├── animations/
│   │   └── shotgun_fire.json
│   ├── effects/
│   │   └── explosion.json
│   └── levels/
│       └── level_01.json
│
├── assets/                        ← Cooked output (git-ignored, generated)
│   ├── textures/
│   │   ├── brick_wall.qtex
│   │   ├── stone_floor.qtex
│   │   └── enemy_grunt_diffuse.qtex
│   ├── models/
│   │   ├── enemy_grunt.qmesh
│   │   └── shotgun.qmesh
│   ├── animations/
│   │   └── shotgun_fire.json      ← Some assets pass through unchanged
│   ├── effects/
│   │   └── explosion.json
│   ├── levels/
│   │   └── level_01.json
│   └── manifest.json              ← Master index of all cooked assets
│
├── tools/
│   └── asset_compiler/
│       ├── main.cpp               ← The compiler entry point
│       ├── mesh_compiler.h
│       ├── mesh_compiler.cpp
│       ├── texture_compiler.h
│       ├── texture_compiler.cpp
│       ├── manifest_builder.h
│       └── manifest_builder.cpp
│
└── src/
    └── engine/
        ├── assets/
        │   ├── asset_handle.h     ← Lightweight integer handle type
        │   ├── binary_mesh.h      ← .qmesh loader for runtime
        │   ├── binary_texture.h   ← .qtex loader for runtime
        │   └── asset_manifest.h   ← Reads manifest.json
        └── resource_manager.h     ← Updated to prefer cooked assets
```

The `assets/` directory is added to `.gitignore`. It is purely derived output -- delete it and the asset compiler regenerates everything. The `assets_raw/` directory is the single source of truth.

---

## Binary Mesh Format (.qmesh)

### Why Binary?

The OBJ loader from Chapter 36 reads a text file line by line, splits strings on spaces and slashes, converts ASCII digits to floats, deduplicates vertices with a hash map, and triangulates quads. For a 50,000-vertex model, this takes tens of milliseconds. The result is always the same arrays of floats and integers.

A binary mesh file stores those arrays directly. Loading is a single `fread` call. No parsing, no deduplication, no conversion.

### Format Layout

```
.qmesh FILE FORMAT
──────────────────────────────────────────────────────────────────
  OFFSET   SIZE       DESCRIPTION
  ──────   ────       ───────────
  0        4 bytes    Magic number: "QMSH" (0x51 0x4D 0x53 0x48)
  4        4 bytes    Version (uint32_t, currently 1)
  8        4 bytes    Vertex count (uint32_t)
  12       4 bytes    Index count (uint32_t)
  16       4 bytes    Vertex format flags (uint32_t, bitmask)
  20       4 bytes    Bounding sphere radius (float)
  24       12 bytes   Bounding box min (vec3)
  36       12 bytes   Bounding box max (vec3)
  48       N bytes    Vertex data (N = vertexCount * vertexStride)
  48+N     M bytes    Index data (M = indexCount * sizeof(uint32_t))
──────────────────────────────────────────────────────────────────

  VERTEX FORMAT FLAGS (bitmask)
  ──────────────────────────────
  Bit 0 (0x01): Has positions (vec3)     ← always set
  Bit 1 (0x02): Has normals (vec3)
  Bit 2 (0x04): Has UVs (vec2)
  Bit 3 (0x08): Has tangents (vec4)      ← w stores handedness
  Bit 4 (0x10): Has bone weights (vec4 weights + ivec4 indices)

  VERTEX STRIDE = sum of all enabled attribute sizes
  Example: positions + normals + UVs + tangents
         = 12 + 12 + 8 + 16 = 48 bytes per vertex
```

The magic number and version let the loader quickly reject files that are corrupt, truncated, or from an incompatible version. Bounding data is precomputed so the frustum culling system (Chapter 32) does not need to scan vertices at load time.

### Header and Data Structures

```cpp
// In tools/asset_compiler/mesh_compiler.h

#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <glm/glm.hpp>

// ─── Binary format constants ──────────────────────────────────

constexpr uint32_t QMESH_MAGIC   = 0x48534D51;  // "QMSH" in little-endian
constexpr uint32_t QMESH_VERSION = 1;

// Vertex format flags
constexpr uint32_t VFMT_POSITION     = 0x01;
constexpr uint32_t VFMT_NORMAL       = 0x02;
constexpr uint32_t VFMT_UV           = 0x04;
constexpr uint32_t VFMT_TANGENT      = 0x08;
constexpr uint32_t VFMT_BONE_WEIGHTS = 0x10;

struct QMeshHeader {
    uint32_t  magic;
    uint32_t  version;
    uint32_t  vertexCount;
    uint32_t  indexCount;
    uint32_t  vertexFormat;
    float     boundingSphereRadius;
    glm::vec3 boundsMin;
    glm::vec3 boundsMax;
};

// ─── Intermediate vertex used during compilation ──────────────

struct CompiledVertex {
    glm::vec3 position  = {0, 0, 0};
    glm::vec3 normal    = {0, 0, 0};
    glm::vec2 uv        = {0, 0};
    glm::vec4 tangent   = {0, 0, 0, 0};   // xyz = tangent dir, w = handedness
    glm::vec4 boneWeights = {0, 0, 0, 0};
    glm::ivec4 boneIndices = {0, 0, 0, 0};
};

// ─── Compiled mesh ready for serialisation ────────────────────

struct CompiledMesh {
    QMeshHeader                header;
    std::vector<CompiledVertex> vertices;
    std::vector<uint32_t>      indices;
};

// ─── Public interface ─────────────────────────────────────────

CompiledMesh compileMeshFromOBJ(const std::string& inputPath);
CompiledMesh compileMeshFromGLTF(const std::string& inputPath);
bool         writeQMesh(const CompiledMesh& mesh, const std::string& outputPath);
```

### Mesh Compilation — Full Implementation

The mesh compiler does four things that the runtime OBJ loader currently does, plus one it does not:

1. **Parse** the source format (OBJ or glTF)
2. **Deduplicate** vertices (already done in Chapter 36's loader)
3. **Compute tangent vectors** (moved from runtime to offline)
4. **Compute bounding volumes** (moved from runtime to offline)
5. **Optimise index order for vertex cache** (new -- not done at runtime)

Vertex cache optimisation reorders the index buffer so that triangles sharing vertices are drawn close together. Modern GPUs cache recently-used vertices in a small FIFO buffer (typically 16-32 entries). If the next triangle reuses a vertex already in the cache, the vertex shader does not run again. Good index order can cut vertex shader invocations by 50% on complex meshes.

We use Tom Forsyth's linear-speed vertex cache optimisation algorithm. It is simple, fast, and produces near-optimal results.

```cpp
// In tools/asset_compiler/mesh_compiler.cpp

#include "mesh_compiler.h"

#include <fstream>
#include <sstream>
#include <iostream>
#include <unordered_map>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <cstring>

// ─── Tangent computation (MikkTSpace-style simplified) ────────
//
// For each triangle, compute the tangent and bitangent from the
// UV gradients. Accumulate per-vertex, then normalise.
// The w component stores handedness: +1 or -1, which tells the
// shader whether to flip the bitangent.

static void computeTangents(std::vector<CompiledVertex>& vertices,
                            const std::vector<uint32_t>& indices)
{
    // Accumulation buffers
    std::vector<glm::vec3> tangents(vertices.size(), glm::vec3(0.0f));
    std::vector<glm::vec3> bitangents(vertices.size(), glm::vec3(0.0f));

    for (size_t i = 0; i < indices.size(); i += 3) {
        uint32_t i0 = indices[i + 0];
        uint32_t i1 = indices[i + 1];
        uint32_t i2 = indices[i + 2];

        const glm::vec3& p0 = vertices[i0].position;
        const glm::vec3& p1 = vertices[i1].position;
        const glm::vec3& p2 = vertices[i2].position;

        const glm::vec2& uv0 = vertices[i0].uv;
        const glm::vec2& uv1 = vertices[i1].uv;
        const glm::vec2& uv2 = vertices[i2].uv;

        glm::vec3 edge1 = p1 - p0;
        glm::vec3 edge2 = p2 - p0;
        glm::vec2 deltaUV1 = uv1 - uv0;
        glm::vec2 deltaUV2 = uv2 - uv0;

        float denom = deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y;
        if (std::abs(denom) < 1e-8f) continue;  // Degenerate UV triangle

        float f = 1.0f / denom;

        glm::vec3 tangent = f * (deltaUV2.y * edge1 - deltaUV1.y * edge2);
        glm::vec3 bitangent = f * (-deltaUV2.x * edge1 + deltaUV1.x * edge2);

        tangents[i0]   += tangent;
        tangents[i1]   += tangent;
        tangents[i2]   += tangent;
        bitangents[i0] += bitangent;
        bitangents[i1] += bitangent;
        bitangents[i2] += bitangent;
    }

    // Orthonormalise (Gram-Schmidt) and compute handedness
    for (size_t i = 0; i < vertices.size(); ++i) {
        const glm::vec3& n = vertices[i].normal;
        const glm::vec3& t = tangents[i];
        const glm::vec3& b = bitangents[i];

        // Gram-Schmidt: remove the normal component from the tangent
        glm::vec3 orthoT = glm::normalize(t - n * glm::dot(n, t));

        // Handedness: is the bitangent on the expected side?
        float handedness = (glm::dot(glm::cross(n, t), b) < 0.0f) ? -1.0f : 1.0f;

        vertices[i].tangent = glm::vec4(orthoT, handedness);
    }
}

// ─── Bounding volume computation ──────────────────────────────

static void computeBounds(CompiledMesh& mesh)
{
    if (mesh.vertices.empty()) return;

    glm::vec3 minP(std::numeric_limits<float>::max());
    glm::vec3 maxP(std::numeric_limits<float>::lowest());

    for (const auto& v : mesh.vertices) {
        minP = glm::min(minP, v.position);
        maxP = glm::max(maxP, v.position);
    }

    mesh.header.boundsMin = minP;
    mesh.header.boundsMax = maxP;

    // Bounding sphere: centre of AABB, radius = max distance from centre
    glm::vec3 centre = (minP + maxP) * 0.5f;
    float maxDist2 = 0.0f;
    for (const auto& v : mesh.vertices) {
        float d2 = glm::dot(v.position - centre, v.position - centre);
        if (d2 > maxDist2) maxDist2 = d2;
    }
    mesh.header.boundingSphereRadius = std::sqrt(maxDist2);
}

// ─── Vertex cache optimisation (Forsyth algorithm) ────────────
//
// The full Forsyth algorithm scores each triangle based on how many
// of its vertices are currently in a simulated cache, preferring
// triangles that reuse cached vertices. This is a simplified but
// effective version that processes vertices in cache-friendly order.

static void optimiseIndexOrder(std::vector<uint32_t>& indices,
                               uint32_t vertexCount)
{
    if (indices.size() < 3) return;

    const uint32_t triCount = static_cast<uint32_t>(indices.size() / 3);
    const int CACHE_SIZE = 32;

    // Build adjacency: for each vertex, which triangles use it?
    std::vector<std::vector<uint32_t>> vertToTri(vertexCount);
    for (uint32_t t = 0; t < triCount; ++t) {
        vertToTri[indices[t * 3 + 0]].push_back(t);
        vertToTri[indices[t * 3 + 1]].push_back(t);
        vertToTri[indices[t * 3 + 2]].push_back(t);
    }

    // Simulated LRU vertex cache
    std::vector<uint32_t> cache;
    cache.reserve(CACHE_SIZE);

    std::vector<bool> triEmitted(triCount, false);
    std::vector<uint32_t> newIndices;
    newIndices.reserve(indices.size());

    // Score function: how many of this triangle's vertices are in cache?
    auto triCacheScore = [&](uint32_t tri) -> int {
        int score = 0;
        for (int j = 0; j < 3; ++j) {
            uint32_t v = indices[tri * 3 + j];
            for (size_t c = 0; c < cache.size(); ++c) {
                if (cache[c] == v) {
                    score += (CACHE_SIZE - static_cast<int>(c));
                    break;
                }
            }
        }
        return score;
    };

    // Push a vertex into the front of the LRU cache
    auto pushCache = [&](uint32_t v) {
        // Remove if already present
        auto it = std::find(cache.begin(), cache.end(), v);
        if (it != cache.end()) cache.erase(it);
        cache.insert(cache.begin(), v);
        if (static_cast<int>(cache.size()) > CACHE_SIZE)
            cache.resize(CACHE_SIZE);
    };

    // Greedily emit the best-scoring triangle from the fan of cached vertices
    uint32_t emittedCount = 0;

    // Seed with triangle 0
    auto emitTriangle = [&](uint32_t tri) {
        if (triEmitted[tri]) return;
        triEmitted[tri] = true;
        for (int j = 0; j < 3; ++j) {
            uint32_t v = indices[tri * 3 + j];
            newIndices.push_back(v);
            pushCache(v);
        }
        ++emittedCount;
    };

    // Process all triangles
    while (emittedCount < triCount) {
        // Find the best triangle among those adjacent to cached vertices
        uint32_t bestTri = UINT32_MAX;
        int bestScore = -1;

        for (uint32_t v : cache) {
            for (uint32_t tri : vertToTri[v]) {
                if (triEmitted[tri]) continue;
                int score = triCacheScore(tri);
                if (score > bestScore) {
                    bestScore = score;
                    bestTri = tri;
                }
            }
        }

        // If no cached triangle found, pick the first unemitted one
        if (bestTri == UINT32_MAX) {
            for (uint32_t t = 0; t < triCount; ++t) {
                if (!triEmitted[t]) {
                    bestTri = t;
                    break;
                }
            }
        }

        if (bestTri == UINT32_MAX) break;  // Should not happen
        emitTriangle(bestTri);
    }

    indices = std::move(newIndices);
}

// ─── OBJ Compilation ─────────────────────────────────────────
// This duplicates much of the OBJ parser from Chapter 36, but the
// output is a CompiledMesh instead of GPU buffers. In a real engine
// you would refactor the parser into a shared library.

struct IndexTuple {
    int posIdx  = -1;
    int uvIdx   = -1;
    int normIdx = -1;

    bool operator==(const IndexTuple& o) const {
        return posIdx == o.posIdx && uvIdx == o.uvIdx && normIdx == o.normIdx;
    }
};

struct IndexTupleHash {
    size_t operator()(const IndexTuple& t) const {
        size_t h1 = std::hash<int>{}(t.posIdx);
        size_t h2 = std::hash<int>{}(t.uvIdx);
        size_t h3 = std::hash<int>{}(t.normIdx);
        return h1 ^ (h2 << 10) ^ (h3 << 20);
    }
};

static IndexTuple parseFaceVertex(const std::string& token)
{
    IndexTuple result;
    size_t slash1 = token.find('/');
    if (slash1 == std::string::npos) {
        result.posIdx = std::stoi(token) - 1;
        return result;
    }
    result.posIdx = std::stoi(token.substr(0, slash1)) - 1;

    size_t slash2 = token.find('/', slash1 + 1);
    if (slash2 == std::string::npos) {
        result.uvIdx = std::stoi(token.substr(slash1 + 1)) - 1;
        return result;
    }

    std::string uvStr = token.substr(slash1 + 1, slash2 - slash1 - 1);
    if (!uvStr.empty()) result.uvIdx = std::stoi(uvStr) - 1;

    std::string normStr = token.substr(slash2 + 1);
    if (!normStr.empty()) result.normIdx = std::stoi(normStr) - 1;

    return result;
}

CompiledMesh compileMeshFromOBJ(const std::string& inputPath)
{
    CompiledMesh result;

    std::ifstream file(inputPath);
    if (!file.is_open()) {
        std::cerr << "[MeshCompiler] Failed to open: " << inputPath << "\n";
        return result;
    }

    std::vector<glm::vec3> tempPositions;
    std::vector<glm::vec2> tempUVs;
    std::vector<glm::vec3> tempNormals;

    std::unordered_map<IndexTuple, uint32_t, IndexTupleHash> vertexMap;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        std::istringstream iss(line);
        std::string keyword;
        iss >> keyword;

        if (keyword == "v") {
            glm::vec3 pos;
            iss >> pos.x >> pos.y >> pos.z;
            tempPositions.push_back(pos);

        } else if (keyword == "vt") {
            glm::vec2 uv;
            iss >> uv.x >> uv.y;
            tempUVs.push_back(uv);

        } else if (keyword == "vn") {
            glm::vec3 norm;
            iss >> norm.x >> norm.y >> norm.z;
            tempNormals.push_back(norm);

        } else if (keyword == "f") {
            std::vector<IndexTuple> face;
            std::string token;
            while (iss >> token) {
                face.push_back(parseFaceVertex(token));
            }

            // Triangulate: fan from first vertex
            for (size_t i = 1; i + 1 < face.size(); ++i) {
                IndexTuple tri[3] = { face[0], face[i], face[i + 1] };

                for (auto& idx : tri) {
                    auto it = vertexMap.find(idx);
                    if (it != vertexMap.end()) {
                        result.indices.push_back(it->second);
                    } else {
                        CompiledVertex v;
                        if (idx.posIdx >= 0)
                            v.position = tempPositions[idx.posIdx];
                        if (idx.uvIdx >= 0)
                            v.uv = tempUVs[idx.uvIdx];
                        if (idx.normIdx >= 0)
                            v.normal = tempNormals[idx.normIdx];

                        uint32_t newIdx = static_cast<uint32_t>(result.vertices.size());
                        result.vertices.push_back(v);
                        result.indices.push_back(newIdx);
                        vertexMap[idx] = newIdx;
                    }
                }
            }
        }
    }

    // ─── Post-processing ──────────────────────────────────────
    std::cout << "[MeshCompiler] Parsed " << inputPath
              << ": " << result.vertices.size() << " vertices, "
              << result.indices.size() / 3 << " triangles\n";

    // 1. Compute tangent vectors for normal mapping
    computeTangents(result.vertices, result.indices);
    std::cout << "[MeshCompiler] Computed tangent vectors\n";

    // 2. Optimise index order for vertex cache
    optimiseIndexOrder(result.indices,
                       static_cast<uint32_t>(result.vertices.size()));
    std::cout << "[MeshCompiler] Optimised index order for vertex cache\n";

    // 3. Compute bounding volumes
    computeBounds(result);

    // 4. Fill header
    result.header.magic        = QMESH_MAGIC;
    result.header.version      = QMESH_VERSION;
    result.header.vertexCount  = static_cast<uint32_t>(result.vertices.size());
    result.header.indexCount   = static_cast<uint32_t>(result.indices.size());
    result.header.vertexFormat = VFMT_POSITION | VFMT_NORMAL
                               | VFMT_UV | VFMT_TANGENT;

    return result;
}

// ─── Write .qmesh to disk ─────────────────────────────────────

bool writeQMesh(const CompiledMesh& mesh, const std::string& outputPath)
{
    std::ofstream out(outputPath, std::ios::binary);
    if (!out.is_open()) {
        std::cerr << "[MeshCompiler] Cannot write: " << outputPath << "\n";
        return false;
    }

    // Write header
    out.write(reinterpret_cast<const char*>(&mesh.header), sizeof(QMeshHeader));

    // Write vertex data
    out.write(reinterpret_cast<const char*>(mesh.vertices.data()),
              mesh.vertices.size() * sizeof(CompiledVertex));

    // Write index data
    out.write(reinterpret_cast<const char*>(mesh.indices.data()),
              mesh.indices.size() * sizeof(uint32_t));

    out.close();

    size_t totalBytes = sizeof(QMeshHeader)
                      + mesh.vertices.size() * sizeof(CompiledVertex)
                      + mesh.indices.size() * sizeof(uint32_t);

    std::cout << "[MeshCompiler] Wrote " << outputPath
              << " (" << totalBytes << " bytes)\n";

    return true;
}
```

The `compileMeshFromGLTF` function follows the same pattern but uses tinygltf to extract vertex and index data from the glTF binary buffers. The interesting work (tangents, cache optimisation, bounds) is shared. I have omitted it here for brevity -- the glTF buffer extraction logic is already in Chapter 36, and wrapping it to produce a `CompiledMesh` instead of calling `glBufferData` is a straightforward refactor.

---

## Texture Preprocessing (.qtex)

### What the Compiler Does

Raw textures arrive as PNG or JPEG files. The texture compiler:

1. **Decodes** the image (using stb_image, same as runtime)
2. **Generates mipmaps** on the CPU (box filter downsampling)
3. **Marks colour space** -- is this an sRGB diffuse/albedo texture, or a linear data texture (normal map, roughness)?
4. **Writes** a binary file containing the header and all mip levels

Optionally, a production pipeline would also compress to GPU formats like BC1/BC3 (DXT) or ASTC. We will design the format to support this but implement only the uncompressed path -- GPU texture compression libraries are large dependencies that add complexity without teaching new engine concepts.

### .qtex Format

```
.qtex FILE FORMAT
──────────────────────────────────────────────────────────────────
  OFFSET   SIZE        DESCRIPTION
  ──────   ────        ───────────
  0        4 bytes     Magic number: "QTEX" (0x51 0x54 0x45 0x58)
  4        4 bytes     Version (uint32_t, currently 1)
  8        4 bytes     Width (uint32_t, base mip level)
  12       4 bytes     Height (uint32_t, base mip level)
  16       4 bytes     Channels (uint32_t: 1, 2, 3, or 4)
  20       4 bytes     Mip level count (uint32_t)
  24       4 bytes     Format (uint32_t: 0=uncompressed, 1=BC1, 2=BC3)
  28       4 bytes     Colour space (uint32_t: 0=linear, 1=sRGB)
  32       N bytes     Mip level 0 pixel data (width * height * channels)
  32+N     M bytes     Mip level 1 pixel data (w/2 * h/2 * channels)
  ...      ...         Further mip levels down to 1x1
──────────────────────────────────────────────────────────────────
```

### Texture Compiler Implementation

```cpp
// In tools/asset_compiler/texture_compiler.h

#pragma once

#include <string>
#include <vector>
#include <cstdint>

constexpr uint32_t QTEX_MAGIC   = 0x58455451;  // "QTEX" little-endian
constexpr uint32_t QTEX_VERSION = 1;

enum class TexFormat : uint32_t {
    Uncompressed = 0,
    BC1          = 1,   // DXT1, 4:1 compression, no alpha
    BC3          = 2    // DXT5, 4:1 compression, with alpha
};

enum class ColourSpace : uint32_t {
    Linear = 0,
    sRGB   = 1
};

struct QTexHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t width;
    uint32_t height;
    uint32_t channels;
    uint32_t mipCount;
    uint32_t format;      // TexFormat
    uint32_t colourSpace;  // ColourSpace
};

struct QTexMipLevel {
    uint32_t              width;
    uint32_t              height;
    std::vector<uint8_t>  data;
};

struct CompiledTexture {
    QTexHeader                header;
    std::vector<QTexMipLevel> mipLevels;
};

CompiledTexture compileTexture(const std::string& inputPath,
                               ColourSpace space = ColourSpace::sRGB);

bool writeQTex(const CompiledTexture& tex, const std::string& outputPath);
```

```cpp
// In tools/asset_compiler/texture_compiler.cpp

#include "texture_compiler.h"

#include <stb_image.h>
#include <fstream>
#include <iostream>
#include <cmath>
#include <algorithm>

// ─── CPU mipmap generation (box filter) ───────────────────────
//
// Each mip level is half the dimensions of the previous one.
// Each pixel is the average of the 2x2 block in the level above.
// This is the simplest and most common downsampling filter.

static QTexMipLevel downsample(const QTexMipLevel& src, uint32_t channels)
{
    QTexMipLevel dst;
    dst.width  = std::max(1u, src.width / 2);
    dst.height = std::max(1u, src.height / 2);
    dst.data.resize(dst.width * dst.height * channels);

    for (uint32_t y = 0; y < dst.height; ++y) {
        for (uint32_t x = 0; x < dst.width; ++x) {
            for (uint32_t c = 0; c < channels; ++c) {
                // Sample the four source pixels
                uint32_t sx = x * 2;
                uint32_t sy = y * 2;

                // Clamp to source dimensions for non-power-of-two edge cases
                uint32_t sx1 = std::min(sx + 1, src.width - 1);
                uint32_t sy1 = std::min(sy + 1, src.height - 1);

                uint32_t p00 = src.data[(sy  * src.width + sx ) * channels + c];
                uint32_t p10 = src.data[(sy  * src.width + sx1) * channels + c];
                uint32_t p01 = src.data[(sy1 * src.width + sx ) * channels + c];
                uint32_t p11 = src.data[(sy1 * src.width + sx1) * channels + c];

                uint8_t avg = static_cast<uint8_t>((p00 + p10 + p01 + p11) / 4);
                dst.data[(y * dst.width + x) * channels + c] = avg;
            }
        }
    }

    return dst;
}

// ─── Compile a texture from a source image ────────────────────

CompiledTexture compileTexture(const std::string& inputPath,
                               ColourSpace space)
{
    CompiledTexture result;

    // Load with stb_image
    int w, h, ch;
    unsigned char* pixels = stbi_load(inputPath.c_str(), &w, &h, &ch, 0);
    if (!pixels) {
        std::cerr << "[TexCompiler] Failed to load: " << inputPath
                  << " (" << stbi_failure_reason() << ")\n";
        return result;
    }

    std::cout << "[TexCompiler] Loaded " << inputPath
              << " (" << w << "x" << h << ", " << ch << " channels)\n";

    // Base mip level (level 0 = original image)
    QTexMipLevel baseMip;
    baseMip.width  = static_cast<uint32_t>(w);
    baseMip.height = static_cast<uint32_t>(h);
    baseMip.data.assign(pixels, pixels + (w * h * ch));
    stbi_image_free(pixels);

    result.mipLevels.push_back(std::move(baseMip));

    // Generate mip chain down to 1x1
    while (result.mipLevels.back().width > 1 ||
           result.mipLevels.back().height > 1)
    {
        result.mipLevels.push_back(
            downsample(result.mipLevels.back(), static_cast<uint32_t>(ch))
        );
    }

    std::cout << "[TexCompiler] Generated " << result.mipLevels.size()
              << " mip levels\n";

    // Fill header
    result.header.magic       = QTEX_MAGIC;
    result.header.version     = QTEX_VERSION;
    result.header.width       = static_cast<uint32_t>(w);
    result.header.height      = static_cast<uint32_t>(h);
    result.header.channels    = static_cast<uint32_t>(ch);
    result.header.mipCount    = static_cast<uint32_t>(result.mipLevels.size());
    result.header.format      = static_cast<uint32_t>(TexFormat::Uncompressed);
    result.header.colourSpace = static_cast<uint32_t>(space);

    return result;
}

// ─── Write .qtex to disk ──────────────────────────────────────

bool writeQTex(const CompiledTexture& tex, const std::string& outputPath)
{
    std::ofstream out(outputPath, std::ios::binary);
    if (!out.is_open()) {
        std::cerr << "[TexCompiler] Cannot write: " << outputPath << "\n";
        return false;
    }

    // Header
    out.write(reinterpret_cast<const char*>(&tex.header), sizeof(QTexHeader));

    // Mip levels back to back
    size_t totalPixelBytes = 0;
    for (const auto& mip : tex.mipLevels) {
        out.write(reinterpret_cast<const char*>(mip.data.data()),
                  mip.data.size());
        totalPixelBytes += mip.data.size();
    }

    out.close();

    std::cout << "[TexCompiler] Wrote " << outputPath
              << " (" << sizeof(QTexHeader) + totalPixelBytes << " bytes, "
              << tex.mipLevels.size() << " mip levels)\n";

    return true;
}
```

### sRGB vs Linear: Why It Matters

When the texture compiler marks a texture as sRGB or linear, the runtime loader uses that metadata to pick the correct OpenGL internal format:

```
COLOUR SPACE DECISION
──────────────────────────────────────────────────────────────────
  Texture type        Colour space    OpenGL internal format
  ─────────────       ────────────    ──────────────────────
  Diffuse / Albedo    sRGB            GL_SRGB8_ALPHA8
  Emissive            sRGB            GL_SRGB8_ALPHA8
  Normal map          Linear          GL_RGBA8
  Roughness           Linear          GL_R8
  Metallic            Linear          GL_R8
  AO map              Linear          GL_R8
──────────────────────────────────────────────────────────────────

  If you upload a normal map as sRGB, the GPU applies a gamma curve
  to the values, and your lighting goes subtly wrong. If you upload
  an albedo texture as linear, colours look washed out.

  The asset compiler makes this decision once, at build time.
  The runtime loader just reads the flag from the header.
```

The convention we use: any texture file in a subdirectory or with a suffix hinting at its purpose gets automatically classified. The compiler applies simple heuristics:

```cpp
// In the asset compiler's main processing loop:

ColourSpace inferColourSpace(const std::string& filename)
{
    // Normal maps, roughness, metallic, AO are linear data
    if (filename.find("_normal")    != std::string::npos) return ColourSpace::Linear;
    if (filename.find("_norm")      != std::string::npos) return ColourSpace::Linear;
    if (filename.find("_roughness") != std::string::npos) return ColourSpace::Linear;
    if (filename.find("_metallic")  != std::string::npos) return ColourSpace::Linear;
    if (filename.find("_ao")        != std::string::npos) return ColourSpace::Linear;
    if (filename.find("_height")    != std::string::npos) return ColourSpace::Linear;

    // Everything else defaults to sRGB (colour data for display)
    return ColourSpace::sRGB;
}
```

This is a convention, not magic. If an artist names a diffuse texture `wall_normal.png` by accident, it will be marked linear and look wrong. Naming conventions matter. Document them for your team.

---

## Asset Manifest

The manifest is a JSON file that the game reads at startup to know what assets exist, where they are, and what they depend on. Without a manifest, the game would have to scan the `assets/` directory recursively at startup -- slow on some platforms (consoles, web) and fragile if the filesystem layout changes.

### Manifest Format

```json
{
    "version": 1,
    "buildTimestamp": "2026-02-18T14:30:00Z",
    "assets": [
        {
            "name": "brick_wall",
            "type": "texture",
            "sourcePath": "textures/brick_wall.png",
            "cookedPath": "textures/brick_wall.qtex",
            "sourceHash": "a3f8c2e1",
            "dependencies": []
        },
        {
            "name": "enemy_grunt",
            "type": "mesh",
            "sourcePath": "models/enemy_grunt.obj",
            "cookedPath": "models/enemy_grunt.qmesh",
            "sourceHash": "b7d14f92",
            "dependencies": [
                "enemy_grunt_diffuse",
                "enemy_grunt_normal"
            ]
        },
        {
            "name": "level_01",
            "type": "level",
            "sourcePath": "levels/level_01.json",
            "cookedPath": "levels/level_01.json",
            "sourceHash": "c9e23a01",
            "dependencies": [
                "brick_wall",
                "stone_floor",
                "enemy_grunt",
                "shotgun"
            ]
        },
        {
            "name": "explosion",
            "type": "effect",
            "sourcePath": "effects/explosion.json",
            "cookedPath": "effects/explosion.json",
            "sourceHash": "d1f5e8b3",
            "dependencies": [
                "smoke_particle",
                "spark_particle"
            ]
        }
    ]
}
```

### Manifest Builder

```cpp
// In tools/asset_compiler/manifest_builder.h

#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <unordered_map>

struct AssetEntry {
    std::string              name;
    std::string              type;        // "texture", "mesh", "level", etc.
    std::string              sourcePath;  // relative to assets_raw/
    std::string              cookedPath;  // relative to assets/
    std::string              sourceHash;  // for incremental rebuilds
    std::vector<std::string> dependencies;
};

class ManifestBuilder {
public:
    void addAsset(const AssetEntry& entry);
    void removeAsset(const std::string& name);

    // Load existing manifest (for incremental updates)
    bool loadExisting(const std::string& path);

    // Check if an asset needs rebuilding (source hash changed)
    bool needsRebuild(const std::string& name,
                      const std::string& currentHash) const;

    // Write manifest to disk
    bool write(const std::string& outputPath) const;

    // Access
    const std::vector<AssetEntry>& getAssets() const { return m_assets; }

private:
    std::vector<AssetEntry>                       m_assets;
    std::unordered_map<std::string, size_t>       m_nameToIndex;
};
```

```cpp
// In tools/asset_compiler/manifest_builder.cpp

#include "manifest_builder.h"

#include <fstream>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>

void ManifestBuilder::addAsset(const AssetEntry& entry)
{
    auto it = m_nameToIndex.find(entry.name);
    if (it != m_nameToIndex.end()) {
        // Update existing entry
        m_assets[it->second] = entry;
    } else {
        m_nameToIndex[entry.name] = m_assets.size();
        m_assets.push_back(entry);
    }
}

void ManifestBuilder::removeAsset(const std::string& name)
{
    auto it = m_nameToIndex.find(name);
    if (it == m_nameToIndex.end()) return;

    size_t idx = it->second;
    m_assets.erase(m_assets.begin() + idx);
    m_nameToIndex.erase(it);

    // Rebuild index map (indices shifted)
    m_nameToIndex.clear();
    for (size_t i = 0; i < m_assets.size(); ++i) {
        m_nameToIndex[m_assets[i].name] = i;
    }
}

bool ManifestBuilder::loadExisting(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open()) return false;

    nlohmann::json j;
    try {
        file >> j;
    } catch (const std::exception& e) {
        std::cerr << "[Manifest] Failed to parse " << path
                  << ": " << e.what() << "\n";
        return false;
    }

    m_assets.clear();
    m_nameToIndex.clear();

    for (const auto& entry : j["assets"]) {
        AssetEntry a;
        a.name       = entry["name"].get<std::string>();
        a.type       = entry["type"].get<std::string>();
        a.sourcePath = entry["sourcePath"].get<std::string>();
        a.cookedPath = entry["cookedPath"].get<std::string>();
        a.sourceHash = entry["sourceHash"].get<std::string>();

        if (entry.contains("dependencies")) {
            for (const auto& dep : entry["dependencies"]) {
                a.dependencies.push_back(dep.get<std::string>());
            }
        }

        m_nameToIndex[a.name] = m_assets.size();
        m_assets.push_back(std::move(a));
    }

    std::cout << "[Manifest] Loaded existing manifest with "
              << m_assets.size() << " assets\n";
    return true;
}

bool ManifestBuilder::needsRebuild(const std::string& name,
                                   const std::string& currentHash) const
{
    auto it = m_nameToIndex.find(name);
    if (it == m_nameToIndex.end()) return true;  // New asset
    return m_assets[it->second].sourceHash != currentHash;
}

bool ManifestBuilder::write(const std::string& outputPath) const
{
    nlohmann::json j;
    j["version"] = 1;

    // Build timestamp
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::ostringstream ts;
    ts << std::put_time(std::gmtime(&time), "%FT%TZ");
    j["buildTimestamp"] = ts.str();

    j["assets"] = nlohmann::json::array();
    for (const auto& a : m_assets) {
        nlohmann::json entry;
        entry["name"]         = a.name;
        entry["type"]         = a.type;
        entry["sourcePath"]   = a.sourcePath;
        entry["cookedPath"]   = a.cookedPath;
        entry["sourceHash"]   = a.sourceHash;
        entry["dependencies"] = a.dependencies;
        j["assets"].push_back(entry);
    }

    std::ofstream file(outputPath);
    if (!file.is_open()) {
        std::cerr << "[Manifest] Cannot write: " << outputPath << "\n";
        return false;
    }

    file << j.dump(4);
    std::cout << "[Manifest] Wrote manifest with " << m_assets.size()
              << " assets to " << outputPath << "\n";
    return true;
}
```

---

## The Asset Compiler — Putting It All Together

The asset compiler is a separate executable. It is not linked into the game. It lives in `tools/asset_compiler/` and has its own `CMakeLists.txt` (or build target in your existing build system).

```
ASSET COMPILER WORKFLOW
──────────────────────────────────────────────────────────────────
  1. Load existing manifest (if any) from assets/manifest.json
  2. Scan assets_raw/ recursively for all source files
  3. For each source file:
     a. Compute a hash (we use file modification time for simplicity)
     b. Check manifest: has this file changed since last build?
     c. If changed (or new): compile it to the cooked format
     d. Update the manifest entry
  4. Remove manifest entries whose source files no longer exist
  5. Write the updated manifest to assets/manifest.json
──────────────────────────────────────────────────────────────────
```

### File Hashing

A production asset compiler uses content hashes (CRC32, xxHash, or SHA-256) to detect changes. For our purposes, the file's last modification timestamp is simple and sufficient. If you edit and save a file, its timestamp changes, and the compiler knows to rebuild it.

```cpp
// Simple hash from file modification time
#include <filesystem>
#include <sstream>
#include <iomanip>

std::string computeFileHash(const std::filesystem::path& path)
{
    auto ftime = std::filesystem::last_write_time(path);
    auto sctp = std::chrono::time_point_cast<std::chrono::seconds>(
        std::chrono::clock_cast<std::chrono::system_clock>(ftime)
    );
    auto epoch = sctp.time_since_epoch().count();

    std::ostringstream oss;
    oss << std::hex << epoch;
    return oss.str();
}
```

### Main Entry Point

```cpp
// In tools/asset_compiler/main.cpp

#include "mesh_compiler.h"
#include "texture_compiler.h"
#include "manifest_builder.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>

namespace fs = std::filesystem;

// ─── Configuration ────────────────────────────────────────────

static const std::string RAW_DIR     = "assets_raw";
static const std::string COOKED_DIR  = "assets";
static const std::string MANIFEST    = "assets/manifest.json";

// ─── File classification ──────────────────────────────────────

enum class AssetType {
    Texture,
    Mesh,
    Animation,
    Effect,
    Level,
    Unknown
};

static AssetType classifyFile(const fs::path& path)
{
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp")
        return AssetType::Texture;
    if (ext == ".obj" || ext == ".gltf" || ext == ".glb")
        return AssetType::Mesh;

    // JSON files are classified by their parent directory
    if (ext == ".json") {
        std::string parentDir = path.parent_path().filename().string();
        if (parentDir == "animations") return AssetType::Animation;
        if (parentDir == "effects")    return AssetType::Effect;
        if (parentDir == "levels")     return AssetType::Level;
    }

    return AssetType::Unknown;
}

static std::string assetTypeName(AssetType type)
{
    switch (type) {
        case AssetType::Texture:   return "texture";
        case AssetType::Mesh:      return "mesh";
        case AssetType::Animation: return "animation";
        case AssetType::Effect:    return "effect";
        case AssetType::Level:     return "level";
        default:                   return "unknown";
    }
}

// ─── Derive asset name from file path ─────────────────────────
// "textures/brick_wall.png" → "brick_wall"

static std::string deriveAssetName(const fs::path& relativePath)
{
    return relativePath.stem().string();
}

// ─── Derive cooked output path ────────────────────────────────
// "textures/brick_wall.png" → "textures/brick_wall.qtex"

static fs::path deriveCookedPath(const fs::path& relativePath, AssetType type)
{
    fs::path result = relativePath;

    switch (type) {
        case AssetType::Texture:
            result.replace_extension(".qtex");
            break;
        case AssetType::Mesh:
            result.replace_extension(".qmesh");
            break;
        default:
            // JSON-based assets pass through unchanged
            break;
    }

    return result;
}

// ─── Process a single asset ───────────────────────────────────

static bool processAsset(const fs::path& sourcePath,
                         const fs::path& cookedPath,
                         AssetType type)
{
    // Ensure output directory exists
    fs::create_directories((fs::path(COOKED_DIR) / cookedPath).parent_path());

    std::string srcStr = (fs::path(RAW_DIR) / sourcePath).string();
    std::string dstStr = (fs::path(COOKED_DIR) / cookedPath).string();

    switch (type) {
        case AssetType::Texture: {
            ColourSpace space = inferColourSpace(sourcePath.stem().string());
            auto compiled = compileTexture(srcStr, space);
            if (compiled.mipLevels.empty()) return false;
            return writeQTex(compiled, dstStr);
        }

        case AssetType::Mesh: {
            std::string ext = sourcePath.extension().string();
            CompiledMesh compiled;
            if (ext == ".obj") {
                compiled = compileMeshFromOBJ(srcStr);
            } else {
                compiled = compileMeshFromGLTF(srcStr);
            }
            if (compiled.vertices.empty()) return false;
            return writeQMesh(compiled, dstStr);
        }

        case AssetType::Animation:
        case AssetType::Effect:
        case AssetType::Level: {
            // JSON assets: copy to output directory (could validate/minify here)
            fs::copy_file(srcStr, dstStr,
                          fs::copy_options::overwrite_existing);
            std::cout << "[AssetCompiler] Copied " << srcStr
                      << " → " << dstStr << "\n";
            return true;
        }

        default:
            return false;
    }
}

// ─── Main ─────────────────────────────────────────────────────

int main(int argc, char* argv[])
{
    std::cout << "╔══════════════════════════════════════╗\n"
              << "║     QEngine Asset Compiler v1.0      ║\n"
              << "╚══════════════════════════════════════╝\n\n";

    bool forceRebuild = false;
    if (argc > 1 && std::string(argv[1]) == "--force") {
        forceRebuild = true;
        std::cout << "[AssetCompiler] Force rebuild enabled.\n\n";
    }

    // Verify source directory exists
    if (!fs::exists(RAW_DIR)) {
        std::cerr << "[AssetCompiler] Source directory not found: "
                  << RAW_DIR << "\n";
        return 1;
    }

    // Create output directory
    fs::create_directories(COOKED_DIR);

    // Load existing manifest for incremental builds
    ManifestBuilder manifest;
    if (!forceRebuild) {
        manifest.loadExisting(MANIFEST);
    }

    // Track which assets we see this run (to detect deleted sources)
    std::vector<std::string> seenAssets;

    // Scan source directory
    int processed = 0;
    int skipped   = 0;
    int failed    = 0;

    for (const auto& entry : fs::recursive_directory_iterator(RAW_DIR)) {
        if (!entry.is_regular_file()) continue;

        fs::path relativePath = fs::relative(entry.path(), RAW_DIR);
        AssetType type = classifyFile(entry.path());

        if (type == AssetType::Unknown) continue;

        std::string assetName = deriveAssetName(relativePath);
        fs::path cookedPath   = deriveCookedPath(relativePath, type);
        std::string fileHash  = computeFileHash(entry.path());

        seenAssets.push_back(assetName);

        // Check if rebuild is needed
        if (!forceRebuild && !manifest.needsRebuild(assetName, fileHash)) {
            ++skipped;
            continue;
        }

        std::cout << "── Processing: " << relativePath.string()
                  << " (" << assetTypeName(type) << ") ──\n";

        bool success = processAsset(relativePath, cookedPath, type);

        if (success) {
            AssetEntry manifestEntry;
            manifestEntry.name       = assetName;
            manifestEntry.type       = assetTypeName(type);
            manifestEntry.sourcePath = relativePath.string();
            manifestEntry.cookedPath = cookedPath.string();
            manifestEntry.sourceHash = fileHash;
            // Dependencies could be extracted from JSON files or material
            // references -- left as a TODO for production use
            manifest.addAsset(manifestEntry);
            ++processed;
        } else {
            std::cerr << "[AssetCompiler] FAILED: " << relativePath.string()
                      << "\n";
            ++failed;
        }

        std::cout << "\n";
    }

    // Remove manifest entries for deleted source files
    auto existingAssets = manifest.getAssets();
    for (const auto& asset : existingAssets) {
        auto it = std::find(seenAssets.begin(), seenAssets.end(), asset.name);
        if (it == seenAssets.end()) {
            std::cout << "[AssetCompiler] Removing stale asset: "
                      << asset.name << "\n";
            // Also delete the cooked file
            fs::path cookedFile = fs::path(COOKED_DIR) / asset.cookedPath;
            if (fs::exists(cookedFile)) {
                fs::remove(cookedFile);
            }
            manifest.removeAsset(asset.name);
        }
    }

    // Write manifest
    manifest.write(MANIFEST);

    // Summary
    std::cout << "\n══════════════════════════════════════\n"
              << "  Processed: " << processed << "\n"
              << "  Skipped:   " << skipped << " (unchanged)\n"
              << "  Failed:    " << failed << "\n"
              << "══════════════════════════════════════\n";

    return (failed > 0) ? 1 : 0;
}
```

### Build Configuration

The asset compiler is a separate CMake target. It links against the same libraries the engine uses for loading (stb_image, nlohmann/json, glm) but does not link OpenGL or GLFW -- it has no window, no GPU context, no rendering. It is a pure command-line data processing tool.

```cmake
# In tools/asset_compiler/CMakeLists.txt

add_executable(asset_compiler
    main.cpp
    mesh_compiler.cpp
    texture_compiler.cpp
    manifest_builder.cpp
)

target_include_directories(asset_compiler PRIVATE
    ${CMAKE_SOURCE_DIR}/external          # stb, nlohmann, glm
    ${CMAKE_SOURCE_DIR}/tools/asset_compiler
)

# No OpenGL, no GLFW, no GLEW -- this is a command-line tool
target_link_libraries(asset_compiler PRIVATE
    # Only header-only libraries needed
)

# Optionally define STB_IMAGE_IMPLEMENTATION in one .cpp
target_compile_definitions(asset_compiler PRIVATE
    STB_IMAGE_IMPLEMENTATION
)
```

Running it looks like this:

```
$ ./asset_compiler
╔══════════════════════════════════════╗
║     QEngine Asset Compiler v1.0      ║
╚══════════════════════════════════════╝

[Manifest] Loaded existing manifest with 12 assets

── Processing: textures/brick_wall.png (texture) ──
[TexCompiler] Loaded textures/brick_wall.png (512x512, 4 channels)
[TexCompiler] Generated 10 mip levels
[TexCompiler] Wrote assets/textures/brick_wall.qtex (1398128 bytes, 10 mip levels)

── Processing: models/enemy_grunt.obj (mesh) ──
[MeshCompiler] Parsed models/enemy_grunt.obj: 4821 vertices, 9200 triangles
[MeshCompiler] Computed tangent vectors
[MeshCompiler] Optimised index order for vertex cache
[MeshCompiler] Wrote assets/models/enemy_grunt.qmesh (341264 bytes)

══════════════════════════════════════
  Processed: 2
  Skipped:   10 (unchanged)
  Failed:    0
══════════════════════════════════════

$ ./asset_compiler
  ...
  Processed: 0
  Skipped:   12 (unchanged)    ← Nothing changed, nothing to do
  Failed:    0

$ ./asset_compiler --force
  ...
  Processed: 12                 ← Rebuild everything
  Skipped:   0
  Failed:    0
```

---

## Runtime: Loading Cooked Assets

Now we update the game's runtime code to load the binary formats we just created.

### Binary Mesh Loader

```cpp
// In src/engine/assets/binary_mesh.h

#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <glm/glm.hpp>

// Reuse the same constants and header struct as the compiler.
// In a real project, put these in a shared header between tools/ and src/.

constexpr uint32_t QMESH_MAGIC   = 0x48534D51;
constexpr uint32_t QMESH_VERSION = 1;

constexpr uint32_t VFMT_POSITION     = 0x01;
constexpr uint32_t VFMT_NORMAL       = 0x02;
constexpr uint32_t VFMT_UV           = 0x04;
constexpr uint32_t VFMT_TANGENT      = 0x08;
constexpr uint32_t VFMT_BONE_WEIGHTS = 0x10;

struct QMeshHeader {
    uint32_t  magic;
    uint32_t  version;
    uint32_t  vertexCount;
    uint32_t  indexCount;
    uint32_t  vertexFormat;
    float     boundingSphereRadius;
    glm::vec3 boundsMin;
    glm::vec3 boundsMax;
};

// The vertex layout must match what the compiler wrote.
struct BinaryVertex {
    glm::vec3  position;
    glm::vec3  normal;
    glm::vec2  uv;
    glm::vec4  tangent;
    glm::vec4  boneWeights;
    glm::ivec4 boneIndices;
};

struct LoadedMesh {
    QMeshHeader                header;
    std::vector<BinaryVertex>  vertices;
    std::vector<uint32_t>      indices;
    bool                       valid = false;
};

LoadedMesh loadQMesh(const std::string& path);
```

```cpp
// In src/engine/assets/binary_mesh.cpp

#include "engine/assets/binary_mesh.h"

#include <fstream>
#include <iostream>

LoadedMesh loadQMesh(const std::string& path)
{
    LoadedMesh result;

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[QMesh] Cannot open: " << path << "\n";
        return result;
    }

    // Read header
    file.read(reinterpret_cast<char*>(&result.header), sizeof(QMeshHeader));

    // Validate magic and version
    if (result.header.magic != QMESH_MAGIC) {
        std::cerr << "[QMesh] Invalid magic number in: " << path << "\n";
        return result;
    }

    if (result.header.version != QMESH_VERSION) {
        std::cerr << "[QMesh] Unsupported version " << result.header.version
                  << " in: " << path << " (expected " << QMESH_VERSION << ")\n";
        return result;
    }

    // Read vertex data
    result.vertices.resize(result.header.vertexCount);
    file.read(reinterpret_cast<char*>(result.vertices.data()),
              result.header.vertexCount * sizeof(BinaryVertex));

    // Read index data
    result.indices.resize(result.header.indexCount);
    file.read(reinterpret_cast<char*>(result.indices.data()),
              result.header.indexCount * sizeof(uint32_t));

    if (file.fail()) {
        std::cerr << "[QMesh] Read error in: " << path << "\n";
        result.vertices.clear();
        result.indices.clear();
        return result;
    }

    result.valid = true;
    return result;
}
```

Loading a .qmesh is three function calls: open, read header, read data. No parsing, no hash maps, no string splitting. The data is already in the exact layout the GPU expects.

### Binary Texture Loader

```cpp
// In src/engine/assets/binary_texture.h

#pragma once

#include <string>
#include <vector>
#include <cstdint>

constexpr uint32_t QTEX_MAGIC   = 0x58455451;
constexpr uint32_t QTEX_VERSION = 1;

struct QTexHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t width;
    uint32_t height;
    uint32_t channels;
    uint32_t mipCount;
    uint32_t format;
    uint32_t colourSpace;
};

struct LoadedTexture {
    QTexHeader header;
    // Mip level data stored contiguously; each level is (w * h * channels) bytes
    // where w and h halve for each successive level
    std::vector<uint8_t> pixelData;
    bool valid = false;
};

LoadedTexture loadQTex(const std::string& path);
```

```cpp
// In src/engine/assets/binary_texture.cpp

#include "engine/assets/binary_texture.h"

#include <fstream>
#include <iostream>

LoadedTexture loadQTex(const std::string& path)
{
    LoadedTexture result;

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[QTex] Cannot open: " << path << "\n";
        return result;
    }

    file.read(reinterpret_cast<char*>(&result.header), sizeof(QTexHeader));

    if (result.header.magic != QTEX_MAGIC) {
        std::cerr << "[QTex] Invalid magic number in: " << path << "\n";
        return result;
    }

    if (result.header.version != QTEX_VERSION) {
        std::cerr << "[QTex] Unsupported version in: " << path << "\n";
        return result;
    }

    // Calculate total pixel data size across all mip levels
    size_t totalBytes = 0;
    uint32_t w = result.header.width;
    uint32_t h = result.header.height;
    for (uint32_t m = 0; m < result.header.mipCount; ++m) {
        totalBytes += w * h * result.header.channels;
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
    }

    result.pixelData.resize(totalBytes);
    file.read(reinterpret_cast<char*>(result.pixelData.data()), totalBytes);

    if (file.fail()) {
        std::cerr << "[QTex] Read error in: " << path << "\n";
        result.pixelData.clear();
        return result;
    }

    result.valid = true;
    return result;
}
```

### GPU Upload with Pre-built Mipmaps

When the runtime has a .qtex with precomputed mipmaps, we upload each level individually instead of calling `glGenerateMipmap`:

```cpp
// In the texture upload path (e.g. ResourceManager::loadTexture)

#include <glad/glad.h>
#include "engine/assets/binary_texture.h"

GLuint uploadQTex(const LoadedTexture& tex)
{
    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    // Choose internal format based on channels and colour space
    GLenum internalFormat, pixelFormat;
    bool srgb = (tex.header.colourSpace == 1);

    switch (tex.header.channels) {
        case 1:
            internalFormat = GL_R8;
            pixelFormat    = GL_RED;
            break;
        case 3:
            internalFormat = srgb ? GL_SRGB8 : GL_RGB8;
            pixelFormat    = GL_RGB;
            break;
        case 4:
        default:
            internalFormat = srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
            pixelFormat    = GL_RGBA;
            break;
    }

    // Upload each mip level
    size_t offset = 0;
    uint32_t w = tex.header.width;
    uint32_t h = tex.header.height;

    for (uint32_t level = 0; level < tex.header.mipCount; ++level) {
        size_t levelSize = w * h * tex.header.channels;

        glTexImage2D(GL_TEXTURE_2D, level, internalFormat,
                     w, h, 0, pixelFormat, GL_UNSIGNED_BYTE,
                     tex.pixelData.data() + offset);

        offset += levelSize;
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
    }

    // Set filtering (mipmaps are already uploaded, no need to generate)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL,
                    tex.header.mipCount - 1);

    glBindTexture(GL_TEXTURE_2D, 0);
    return textureID;
}
```

---

## Asset Handles

Throughout the engine, assets are referenced by string name: `resourceManager.getTexture("brick_wall")`. String comparisons are slow compared to integer comparisons. With dozens of systems looking up assets every frame, this adds up.

An **asset handle** is a lightweight integer ID that maps to a loaded asset. You resolve the string once at load time, get a handle, and use the handle everywhere else.

```cpp
// In src/engine/assets/asset_handle.h

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// ─── AssetHandle ──────────────────────────────────────────────
// A thin wrapper around a uint32_t. Zero is the invalid handle.
// The handle is an index into the ResourceManager's internal arrays.

struct AssetHandle {
    uint32_t id = 0;

    bool isValid() const { return id != 0; }

    bool operator==(const AssetHandle& other) const { return id == other.id; }
    bool operator!=(const AssetHandle& other) const { return id != other.id; }
    bool operator<(const AssetHandle& other) const  { return id < other.id; }
};

// For use as a hash map key
struct AssetHandleHash {
    size_t operator()(const AssetHandle& h) const {
        return std::hash<uint32_t>{}(h.id);
    }
};

// ─── HandleRegistry ───────────────────────────────────────────
// Maps string names to integer handles. Shared across all asset types.

class HandleRegistry {
public:
    // Get or create a handle for the given name.
    // Returns the same handle if the name was already registered.
    AssetHandle resolve(const std::string& name)
    {
        auto it = m_nameToHandle.find(name);
        if (it != m_nameToHandle.end()) return it->second;

        AssetHandle handle;
        handle.id = m_nextId++;
        m_nameToHandle[name]      = handle;
        m_handleToName[handle.id] = name;
        return handle;
    }

    // Look up without creating (returns invalid handle if not found)
    AssetHandle find(const std::string& name) const
    {
        auto it = m_nameToHandle.find(name);
        return (it != m_nameToHandle.end()) ? it->second : AssetHandle{0};
    }

    // Reverse lookup
    const std::string& getName(AssetHandle handle) const
    {
        static const std::string empty;
        auto it = m_handleToName.find(handle.id);
        return (it != m_handleToName.end()) ? it->second : empty;
    }

private:
    uint32_t m_nextId = 1;  // 0 is reserved for "invalid"
    std::unordered_map<std::string, AssetHandle>  m_nameToHandle;
    std::unordered_map<uint32_t, std::string>     m_handleToName;
};
```

Systems now store `AssetHandle` values instead of strings:

```cpp
// Before (string-based):
struct MeshRenderer {
    std::string meshName;
    std::string textureName;
};

// After (handle-based):
struct MeshRenderer {
    AssetHandle mesh;
    AssetHandle texture;
};
```

The handle is resolved once during entity creation or level loading:

```cpp
// During level load:
auto& renderer = registry.emplace<MeshRenderer>(entity);
renderer.mesh    = resourceManager.resolveHandle("enemy_grunt");
renderer.texture = resourceManager.resolveHandle("enemy_grunt_diffuse");

// During rendering (every frame):
auto* meshData = resourceManager.getMesh(renderer.mesh);   // O(1) array lookup
auto* texData  = resourceManager.getTexture(renderer.texture);  // O(1) array lookup
```

---

## Integrating with ResourceManager

The existing ResourceManager needs two changes: it should prefer cooked assets over raw ones, and it should support handle-based lookups.

```cpp
// In src/engine/resource_manager.h (updated)

#pragma once

#include "engine/assets/asset_handle.h"
#include "engine/assets/asset_manifest.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <glad/glad.h>

struct MeshGPU {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    uint32_t indexCount = 0;
    float    boundingSphereRadius = 0.0f;
    glm::vec3 boundsMin{0};
    glm::vec3 boundsMax{0};
};

class ResourceManager {
public:
    // ─── Initialisation ───────────────────────────────────────
    // Call once at startup. If a manifest exists, load it.
    // useCookedAssets controls whether we prefer cooked or raw.
    void init(bool useCookedAssets = true);

    // ─── Handle resolution ────────────────────────────────────
    AssetHandle resolveHandle(const std::string& name);

    // ─── Loading (triggers actual file I/O and GPU upload) ────
    void loadTexture(const std::string& name);
    void loadMesh(const std::string& name);

    // ─── Access by handle (fast, O(1)) ────────────────────────
    GLuint   getTextureID(AssetHandle handle) const;
    MeshGPU* getMesh(AssetHandle handle);

    // ─── Access by name (slower, hash map lookup) ─────────────
    GLuint   getTextureID(const std::string& name);
    MeshGPU* getMesh(const std::string& name);

    // ─── Cleanup ──────────────────────────────────────────────
    void shutdown();

private:
    // ─── Path resolution ──────────────────────────────────────
    // Returns the best available path for an asset:
    // cooked path if it exists and useCookedAssets is true,
    // otherwise the raw path.
    std::string resolveTexturePath(const std::string& name) const;
    std::string resolveMeshPath(const std::string& name) const;

    // ─── Internal loading by path ─────────────────────────────
    GLuint  loadTextureFromRaw(const std::string& path);
    GLuint  loadTextureFromQTex(const std::string& path);
    MeshGPU loadMeshFromRaw(const std::string& path);
    MeshGPU loadMeshFromQMesh(const std::string& path);

    // ─── State ────────────────────────────────────────────────
    bool           m_useCookedAssets = true;
    AssetManifest  m_manifest;
    HandleRegistry m_handles;

    // Indexed by handle ID.  Index 0 is unused (invalid handle).
    std::vector<GLuint>  m_textures = { 0 };  // index 0 = invalid
    std::vector<MeshGPU> m_meshes   = { {} };

    // Maps handle ID to slot in the typed arrays above.
    // (Handle IDs are global across types; the slot index is per-type.)
    std::unordered_map<uint32_t, size_t> m_textureSlots;
    std::unordered_map<uint32_t, size_t> m_meshSlots;
};
```

```cpp
// In src/engine/resource_manager.cpp (key methods shown)

#include "engine/resource_manager.h"
#include "engine/assets/binary_mesh.h"
#include "engine/assets/binary_texture.h"

#include <stb_image.h>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

void ResourceManager::init(bool useCookedAssets)
{
    m_useCookedAssets = useCookedAssets;

    if (useCookedAssets && fs::exists("assets/manifest.json")) {
        m_manifest.load("assets/manifest.json");
        std::cout << "[ResourceManager] Loaded manifest ("
                  << m_manifest.assetCount() << " assets). "
                  << "Using cooked assets.\n";
    } else {
        std::cout << "[ResourceManager] No manifest found or cooked assets "
                  << "disabled. Falling back to raw assets.\n";
        m_useCookedAssets = false;
    }
}

AssetHandle ResourceManager::resolveHandle(const std::string& name)
{
    return m_handles.resolve(name);
}

// ─── Path resolution ──────────────────────────────────────────

std::string ResourceManager::resolveTexturePath(const std::string& name) const
{
    if (m_useCookedAssets) {
        std::string cookedPath = m_manifest.getCookedPath(name);
        if (!cookedPath.empty() && fs::exists("assets/" + cookedPath)) {
            return "assets/" + cookedPath;
        }
    }

    // Fallback: search raw directories for common extensions
    for (const char* ext : {".png", ".jpg", ".jpeg", ".tga", ".bmp"}) {
        std::string path = "assets_raw/textures/" + name + ext;
        if (fs::exists(path)) return path;
    }

    std::cerr << "[ResourceManager] Texture not found: " << name << "\n";
    return "";
}

std::string ResourceManager::resolveMeshPath(const std::string& name) const
{
    if (m_useCookedAssets) {
        std::string cookedPath = m_manifest.getCookedPath(name);
        if (!cookedPath.empty() && fs::exists("assets/" + cookedPath)) {
            return "assets/" + cookedPath;
        }
    }

    for (const char* ext : {".obj", ".gltf", ".glb"}) {
        std::string path = "assets_raw/models/" + name + ext;
        if (fs::exists(path)) return path;
    }

    std::cerr << "[ResourceManager] Mesh not found: " << name << "\n";
    return "";
}

// ─── Texture loading ──────────────────────────────────────────

void ResourceManager::loadTexture(const std::string& name)
{
    AssetHandle handle = m_handles.resolve(name);

    // Already loaded?
    if (m_textureSlots.count(handle.id)) return;

    std::string path = resolveTexturePath(name);
    if (path.empty()) return;

    GLuint texID = 0;

    // Choose loader based on extension
    if (path.ends_with(".qtex")) {
        texID = loadTextureFromQTex(path);
    } else {
        texID = loadTextureFromRaw(path);
    }

    if (texID == 0) return;

    size_t slot = m_textures.size();
    m_textures.push_back(texID);
    m_textureSlots[handle.id] = slot;
}

GLuint ResourceManager::loadTextureFromQTex(const std::string& path)
{
    LoadedTexture tex = loadQTex(path);
    if (!tex.valid) return 0;
    return uploadQTex(tex);  // Function shown earlier in this chapter
}

GLuint ResourceManager::loadTextureFromRaw(const std::string& path)
{
    // Existing stb_image loading path from Chapter 5
    int w, h, ch;
    unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &ch, 0);
    if (!pixels) {
        std::cerr << "[ResourceManager] stbi_load failed: " << path << "\n";
        return 0;
    }

    GLuint texID;
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_2D, texID);

    GLenum format = (ch == 4) ? GL_RGBA : (ch == 3) ? GL_RGB : GL_RED;
    GLenum internalFormat = (ch == 4) ? GL_SRGB8_ALPHA8
                          : (ch == 3) ? GL_SRGB8 : GL_R8;

    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, w, h, 0,
                 format, GL_UNSIGNED_BYTE, pixels);
    glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    stbi_image_free(pixels);
    glBindTexture(GL_TEXTURE_2D, 0);

    return texID;
}

// ─── Handle-based access ──────────────────────────────────────

GLuint ResourceManager::getTextureID(AssetHandle handle) const
{
    auto it = m_textureSlots.find(handle.id);
    if (it == m_textureSlots.end()) return 0;
    return m_textures[it->second];
}

GLuint ResourceManager::getTextureID(const std::string& name)
{
    AssetHandle handle = m_handles.find(name);
    if (!handle.isValid()) return 0;
    return getTextureID(handle);
}
```

The mesh loading follows the same pattern: resolve path, choose loader based on extension, upload to GPU, store by handle. The fallback to raw loading means you can work without running the asset compiler during development -- the engine just loads .obj and .png directly, the way it always has.

---

## Asset Manifest Reader (Runtime)

The game needs a lightweight reader for the manifest. This is simpler than the builder -- it only needs to load and query, not modify or write.

```cpp
// In src/engine/assets/asset_manifest.h

#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <unordered_map>

class AssetManifest {
public:
    bool load(const std::string& path)
    {
        std::ifstream file(path);
        if (!file.is_open()) return false;

        nlohmann::json j;
        try {
            file >> j;
        } catch (...) {
            return false;
        }

        for (const auto& entry : j["assets"]) {
            ManifestEntry e;
            e.name       = entry["name"].get<std::string>();
            e.type       = entry["type"].get<std::string>();
            e.cookedPath = entry["cookedPath"].get<std::string>();

            if (entry.contains("dependencies")) {
                for (const auto& dep : entry["dependencies"]) {
                    e.dependencies.push_back(dep.get<std::string>());
                }
            }

            m_lookup[e.name] = m_entries.size();
            m_entries.push_back(std::move(e));
        }

        return true;
    }

    std::string getCookedPath(const std::string& name) const
    {
        auto it = m_lookup.find(name);
        if (it == m_lookup.end()) return "";
        return m_entries[it->second].cookedPath;
    }

    std::string getType(const std::string& name) const
    {
        auto it = m_lookup.find(name);
        if (it == m_lookup.end()) return "";
        return m_entries[it->second].type;
    }

    std::vector<std::string> getDependencies(const std::string& name) const
    {
        auto it = m_lookup.find(name);
        if (it == m_lookup.end()) return {};
        return m_entries[it->second].dependencies;
    }

    size_t assetCount() const { return m_entries.size(); }

    // Load all dependencies for a given asset (e.g. load all textures
    // and meshes referenced by a level file)
    std::vector<std::string> gatherAllDependencies(const std::string& name) const
    {
        std::vector<std::string> result;
        std::unordered_map<std::string, bool> visited;
        gatherDepsRecursive(name, result, visited);
        return result;
    }

private:
    struct ManifestEntry {
        std::string              name;
        std::string              type;
        std::string              cookedPath;
        std::vector<std::string> dependencies;
    };

    void gatherDepsRecursive(const std::string& name,
                             std::vector<std::string>& result,
                             std::unordered_map<std::string, bool>& visited) const
    {
        if (visited.count(name)) return;
        visited[name] = true;

        auto deps = getDependencies(name);
        for (const auto& dep : deps) {
            gatherDepsRecursive(dep, result, visited);
        }

        result.push_back(name);
    }

    std::vector<ManifestEntry>                m_entries;
    std::unordered_map<std::string, size_t>   m_lookup;
};
```

Using the manifest, a level loading system can prefetch all assets a level needs before the player enters:

```cpp
void loadLevel(const std::string& levelName, ResourceManager& rm,
               const AssetManifest& manifest)
{
    // Gather transitive dependencies
    auto deps = manifest.gatherAllDependencies(levelName);

    for (const auto& assetName : deps) {
        std::string type = manifest.getType(assetName);

        if (type == "texture") {
            rm.loadTexture(assetName);
        } else if (type == "mesh") {
            rm.loadMesh(assetName);
        }
        // Animations, effects, etc. handled similarly
    }

    // Now load the level geometry and spawn entities
    // All referenced assets are already cached -- no stalls during gameplay
}
```

---

## C++ Concept Sidebar: Offline Tools vs Runtime Code

The asset compiler and the game engine are both C++ programs, but they have fundamentally different constraints:

```
                        ASSET COMPILER              GAME ENGINE
                        ──────────────              ───────────
  Runs when?            Once, before shipping       Every frame, forever
  Time budget?          Minutes are acceptable      16ms per frame (60 FPS)
  Memory budget?        Whatever the PC has          Shared with gameplay, GPU, etc.
  Input/output?         Files → files               Files → GPU → screen
  Error handling?       Print error, skip file       Cannot crash, must recover
  Optimise for?         Thoroughness, correctness    Speed, low latency
  Dependencies?         Can use heavy libraries      Minimal, lean runtime
```

This asymmetry is why the pipeline exists. The compiler can afford to:

- Run a sophisticated vertex cache optimisation algorithm that scans every triangle multiple times
- Generate mipmaps with high-quality filters (Lanczos, Kaiser) instead of a simple box filter
- Validate every asset thoroughly and report detailed error messages
- Use `std::filesystem` for recursive directory scanning (too slow for per-frame use)
- Allocate and free memory freely without worrying about frame spikes

The game cannot afford any of that per frame. But it does not need to -- the compiler already did the work. The game just reads the results.

This pattern extends beyond asset pipelines. Lighting bakes (precomputed lightmaps), navigation mesh generation, shader compilation, physics collision mesh simplification -- all follow the same principle: spend time offline so you do not have to spend time at runtime.

When you design engine systems, always ask: "Does this work need to happen every frame, or can it be done once?" If the answer is once, it belongs in a tool, not in the game loop.

---

## Development Workflow

With the pipeline in place, your daily workflow looks like this:

```
DEVELOPMENT WORKFLOW
──────────────────────────────────────────────────────────────────

  ITERATION LOOP (fast, no cooking):
  ┌─────────────────────────────────────────────────────┐
  │  1. Edit a texture in Photoshop, save as .png       │
  │  2. Run the game (loads raw .png directly)          │
  │  3. See the result immediately                      │
  │  4. Repeat until satisfied                          │
  └─────────────────────────────────────────────────────┘
  ResourceManager falls back to raw assets automatically.

  COOKING STEP (before testing performance or shipping):
  ┌─────────────────────────────────────────────────────┐
  │  1. Run: ./asset_compiler                           │
  │  2. Only changed assets are reprocessed             │
  │  3. Run the game (loads cooked .qtex and .qmesh)    │
  │  4. Loading is 10x faster, GPU formats optimal      │
  └─────────────────────────────────────────────────────┘

  RELEASE BUILD:
  ┌─────────────────────────────────────────────────────┐
  │  1. Run: ./asset_compiler --force                   │
  │  2. Ship the assets/ directory with the game        │
  │  3. Do NOT ship assets_raw/ (not needed at runtime) │
  │  4. Optionally disable raw fallback in release      │
  └─────────────────────────────────────────────────────┘
```

You can control the cooked-vs-raw preference through a compile-time flag or a Lua config value from Chapter 49:

```lua
-- In config.lua
engine.useCookedAssets = true   -- false for raw iteration, true for cooked
```

```cpp
// In engine initialisation:
bool useCookedAssets = luaConfig.getBool("engine.useCookedAssets", true);
resourceManager.init(useCookedAssets);
```

---

## Summary

Here is what we built in this chapter and why each piece matters:

```
CHAPTER 50 SUMMARY
──────────────────────────────────────────────────────────────────
  COMPONENT              PURPOSE
  ─────────              ───────
  .qmesh format          Binary mesh: deduplicated, tangents baked,
                         indices cache-optimised, bounds precomputed

  .qtex format           Binary texture: mipmaps pre-generated,
                         colour space tagged, ready for GPU upload

  Asset compiler         Standalone tool: scans assets_raw/, produces
                         cooked files in assets/, incremental rebuilds

  Asset manifest         JSON index: lists all assets, types, and
                         dependency chains for prefetching

  Handle system          Integer IDs replace string lookups for O(1)
                         asset access at runtime

  ResourceManager        Updated to prefer cooked assets, fall back
                         to raw for development, support handle access

  Path resolution        Automatic: cooked if available, raw otherwise
──────────────────────────────────────────────────────────────────
```

The raw-to-cooked separation is one of the most impactful architectural decisions in a game engine. Every second you shave off loading time is a second your players are not staring at a loading screen. Every computation you move offline is a computation that is not competing with your game loop for CPU time. And the incremental rebuild system means the cost of cooking is paid once per change, not once per launch.

---

## What's Next

**Chapter 50a: Tools & Pipeline Cleanup** will tie together the systems from Chapters 47-50. The ImGui editor will get an "asset browser" panel that reads the manifest and shows available assets with thumbnails. The level editor will reference assets by handle instead of raw file path. The Lua scripting system will expose the asset compiler as a callable command from the developer console (`cook_assets()`), so you never leave the engine to rebuild. We will also add dependency extraction -- the compiler will parse level JSON files and material definitions to automatically populate the dependency lists in the manifest, removing the manual step.

The engine is approaching production readiness. A data-driven particle system, a scripting layer, an editor, and now an asset pipeline -- these are the tools that turn a hobby engine into something you can ship a game with.
