# Chapter 6: Mesh & Model Loading

## What You'll Learn
- Index buffers — eliminating duplicate vertices
- The OBJ file format — a simple text-based 3D model format
- Building a Mesh class that handles GPU upload
- Loading models from files
- Resource management basics — loading once, sharing across entities

---

## The Problem with Duplicate Vertices

In Chapter 5, our quad used 6 vertices for 2 triangles:

```
Triangle 1: bottom-left, bottom-right, top-right
Triangle 2: bottom-left, top-right, top-left
```

Bottom-left and top-right appear twice. For a quad that's 2 wasted vertices. For a complex model with thousands of triangles sharing vertices, the waste is enormous.

**Index buffers** solve this. Instead of listing every vertex for every triangle, we:
1. List each unique vertex once
2. Use an array of indices to say "triangle 1 uses vertices 0, 1, 2; triangle 2 uses vertices 0, 2, 3"

```
Vertices (unique):
  0: bottom-left
  1: bottom-right
  2: top-right
  3: top-left

Indices:
  0, 1, 2,    ← Triangle 1
  0, 2, 3     ← Triangle 2
```

4 vertices + 6 indices instead of 6 vertices. The savings scale dramatically with complex meshes.

---

## Vertex Data Structure

Let's define a proper vertex structure instead of using raw float arrays. Since components are our shared data definitions, this belongs in `components.h`:

```cpp
// In components.h — add alongside the existing components

struct Vertex {
    glm::vec3 position  = glm::vec3(0.0f);
    glm::vec3 normal    = glm::vec3(0.0f);   // For lighting (Chapter 7)
    glm::vec2 texCoords = glm::vec2(0.0f);
};
```

### C++ Concept: Struct Memory Layout

`Vertex` contains 3 + 3 + 2 = 8 floats = 32 bytes per vertex. In C++, struct members are laid out sequentially in memory:

```
[posX, posY, posZ, normX, normY, normZ, texU, texV]
 ◄── position ──►  ◄──── normal ────►  ◄─ uv ─►
```

This contiguous layout is exactly what OpenGL's `glVertexAttribPointer` expects, so we can pass a vector of `Vertex` structs directly to the GPU.

---

## Building the Mesh Class

### src/engine/renderer/mesh.h

```cpp
#pragma once

#include "engine/ecs/components.h"   // Vertex struct lives here

#include <glad/glad.h>
#include <vector>
#include <string>

class Mesh {
public:
    Mesh(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices);
    ~Mesh();

    // Prevent copying (GPU resources shouldn't be duplicated accidentally)
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    // Allow moving (transfer ownership)
    Mesh(Mesh&& other) noexcept;
    Mesh& operator=(Mesh&& other) noexcept;

    unsigned int getVAO() const { return m_vao; }
    unsigned int getIndexCount() const { return m_indexCount; }

private:
    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;
    unsigned int m_ebo = 0;   // Element Buffer Object (index buffer)
    unsigned int m_indexCount = 0;

    void setupMesh(const std::vector<Vertex>& vertices,
                   const std::vector<unsigned int>& indices);
    void cleanup();
};
```

### C++ Concept: Delete and Move

```cpp
Mesh(const Mesh&) = delete;              // No copying
Mesh(Mesh&& other) noexcept;             // Moving is allowed
```

**Why no copying?** A Mesh owns GPU resources (VAO, VBO, EBO). If you copy a Mesh, both copies would think they own the same GPU resources. When one is destroyed, it deletes the resources — and the other now has dangling handles.

**Move semantics** solve this. When you "move" a Mesh, ownership transfers. The source gives up its handles (set to 0), and the destination takes them.

```cpp
Mesh a = loadMesh("cube.obj");    // a owns the GPU resources
Mesh b = std::move(a);            // b now owns them, a is empty
// a's destructor does nothing (handles are 0)
// b's destructor cleans up
```

### src/engine/renderer/mesh.cpp

```cpp
#include "engine/renderer/mesh.h"

Mesh::Mesh(const std::vector<Vertex>& vertices,
           const std::vector<unsigned int>& indices) {
    m_indexCount = static_cast<unsigned int>(indices.size());
    setupMesh(vertices, indices);
}

Mesh::~Mesh() {
    cleanup();
}

Mesh::Mesh(Mesh&& other) noexcept
    : m_vao(other.m_vao)
    , m_vbo(other.m_vbo)
    , m_ebo(other.m_ebo)
    , m_indexCount(other.m_indexCount)
{
    other.m_vao = 0;
    other.m_vbo = 0;
    other.m_ebo = 0;
    other.m_indexCount = 0;
}

Mesh& Mesh::operator=(Mesh&& other) noexcept {
    if (this != &other) {
        cleanup();
        m_vao = other.m_vao;
        m_vbo = other.m_vbo;
        m_ebo = other.m_ebo;
        m_indexCount = other.m_indexCount;
        other.m_vao = 0;
        other.m_vbo = 0;
        other.m_ebo = 0;
        other.m_indexCount = 0;
    }
    return *this;
}

void Mesh::setupMesh(const std::vector<Vertex>& vertices,
                     const std::vector<unsigned int>& indices) {
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);

    glBindVertexArray(m_vao);

    // Upload vertex data
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 vertices.size() * sizeof(Vertex),
                 vertices.data(),
                 GL_STATIC_DRAW);

    // Upload index data
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 indices.size() * sizeof(unsigned int),
                 indices.data(),
                 GL_STATIC_DRAW);

    // Vertex attribute layout:
    // Position (location 0): 3 floats at offset 0
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void*)offsetof(Vertex, position));
    glEnableVertexAttribArray(0);

    // Normal (location 1): 3 floats at offset of 'normal' member
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void*)offsetof(Vertex, normal));
    glEnableVertexAttribArray(1);

    // Tex coords (location 2): 2 floats at offset of 'texCoords' member
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void*)offsetof(Vertex, texCoords));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);
}

void Mesh::cleanup() {
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    if (m_ebo) glDeleteBuffers(1, &m_ebo);
    m_vao = m_vbo = m_ebo = 0;
}
```

### C++ Concept: `offsetof`

```cpp
(void*)offsetof(Vertex, normal)
```

`offsetof` is a macro that returns the byte offset of a member within a struct. For our `Vertex`:

```
offsetof(Vertex, position)  = 0   (starts at the beginning)
offsetof(Vertex, normal)    = 12  (after 3 floats = 12 bytes)
offsetof(Vertex, texCoords) = 24  (after 6 floats = 24 bytes)
```

This is cleaner than manually calculating `(void*)(3 * sizeof(float))`.

### C++ Concept: `std::vector`

```cpp
std::vector<Vertex> vertices;
std::vector<unsigned int> indices;
```

`std::vector` is a dynamic array — it grows and shrinks as needed. Key operations:

```cpp
vertices.push_back(vertex);     // Add to end
vertices.size();                 // Number of elements
vertices.data();                 // Raw pointer to underlying array (for OpenGL)
vertices[0];                     // Access by index
vertices.clear();                // Remove all elements
```

Internally, a vector is a contiguous block of memory on the heap. When you `push_back` past its capacity, it allocates a larger block and copies everything over. For performance-critical code, you can `reserve()` capacity upfront:

```cpp
vertices.reserve(10000);  // Allocate space for 10000 vertices (no copies needed)
```

---

## Loading OBJ Files

The OBJ format is a simple text-based 3D model format. Here's what it looks like:

```
v  -0.5 -0.5  0.5      ← vertex position
vt  0.0  0.0            ← texture coordinate
vn  0.0  0.0  1.0       ← normal vector
f  1/1/1  2/2/1  3/3/1  ← face (pos/tex/norm indices, 1-based)
```

Each line starts with a type prefix:
- `v` — vertex position
- `vt` — texture coordinate
- `vn` — normal vector
- `f` — face (triangle), referencing indices into the v/vt/vn lists

### A Test Model: assets/models/cube.obj

Save the following as `assets/models/cube.obj`. This is a complete unit cube with normals and texture coordinates — we'll use it to test our OBJ loader at the end of the chapter:

```obj
# Unit cube for QEngine
# 8 corners, 6 faces (each face has its own normal)

# Positions
v -0.5 -0.5  0.5
v  0.5 -0.5  0.5
v  0.5  0.5  0.5
v -0.5  0.5  0.5
v -0.5 -0.5 -0.5
v  0.5 -0.5 -0.5
v  0.5  0.5 -0.5
v -0.5  0.5 -0.5

# Texture coordinates
vt 0.0 0.0
vt 1.0 0.0
vt 1.0 1.0
vt 0.0 1.0

# Normals (one per face direction)
vn  0.0  0.0  1.0
vn  0.0  0.0 -1.0
vn  0.0  1.0  0.0
vn  0.0 -1.0  0.0
vn  1.0  0.0  0.0
vn -1.0  0.0  0.0

# Front face
f 1/1/1 2/2/1 3/3/1
f 1/1/1 3/3/1 4/4/1
# Back face
f 6/1/2 5/2/2 8/3/2
f 6/1/2 8/3/2 7/4/2
# Top face
f 4/1/3 3/2/3 7/3/3
f 4/1/3 7/3/3 8/4/3
# Bottom face
f 5/1/4 6/2/4 2/3/4
f 5/1/4 2/3/4 1/4/4
# Right face
f 2/1/5 6/2/5 7/3/5
f 2/1/5 7/3/5 3/4/5
# Left face
f 5/1/6 1/2/6 4/3/6
f 5/1/6 4/3/6 8/4/6
```

Make sure the `assets/models/` directory exists. This cube gives us 24 unique vertices (shared positions but different normals per face) and 12 triangles — exactly what `createBox()` generates procedurally, so we can compare both paths.

### src/engine/renderer/obj_loader.h

```cpp
#pragma once

#include "engine/renderer/mesh.h"
#include <string>

Mesh loadOBJ(const std::string& path);
```

### src/engine/renderer/obj_loader.cpp

```cpp
#include "engine/renderer/obj_loader.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <unordered_map>

Mesh loadOBJ(const std::string& path) {
    std::vector<glm::vec3> tempPositions;
    std::vector<glm::vec2> tempTexCoords;
    std::vector<glm::vec3> tempNormals;

    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;

    // Map to detect and reuse duplicate vertices
    // Key: "posIndex/texIndex/normIndex" string
    // Value: index in the final vertex array
    std::unordered_map<std::string, unsigned int> vertexMap;

    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "ERROR: Could not open OBJ file: " << path << std::endl;
        return Mesh(vertices, indices);
    }

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string prefix;
        iss >> prefix;

        if (prefix == "v") {
            glm::vec3 pos;
            iss >> pos.x >> pos.y >> pos.z;
            tempPositions.push_back(pos);
        }
        else if (prefix == "vt") {
            glm::vec2 tex;
            iss >> tex.x >> tex.y;
            tempTexCoords.push_back(tex);
        }
        else if (prefix == "vn") {
            glm::vec3 norm;
            iss >> norm.x >> norm.y >> norm.z;
            tempNormals.push_back(norm);
        }
        else if (prefix == "f") {
            // Parse face — can have 3 or more vertices (we handle triangles and quads)
            std::vector<std::string> faceTokens;
            std::string token;
            while (iss >> token) {
                faceTokens.push_back(token);
            }

            // Triangulate: fan from first vertex (works for convex polygons)
            for (size_t i = 1; i + 1 < faceTokens.size(); i++) {
                std::string triTokens[3] = {
                    faceTokens[0], faceTokens[i], faceTokens[i + 1]
                };

                for (const auto& tok : triTokens) {
                    // Check if we've already created this exact vertex
                    auto it = vertexMap.find(tok);
                    if (it != vertexMap.end()) {
                        indices.push_back(it->second);
                        continue;
                    }

                    // Parse "posIndex/texIndex/normIndex"
                    std::istringstream tokenStream(tok);
                    std::string part;
                    int posIdx = 0, texIdx = 0, normIdx = 0;

                    // Position index (required)
                    std::getline(tokenStream, part, '/');
                    posIdx = std::stoi(part) - 1;  // OBJ is 1-based

                    // Texture coordinate index (optional)
                    if (std::getline(tokenStream, part, '/') && !part.empty()) {
                        texIdx = std::stoi(part) - 1;
                    }

                    // Normal index (optional)
                    if (std::getline(tokenStream, part, '/') && !part.empty()) {
                        normIdx = std::stoi(part) - 1;
                    }

                    Vertex vertex{};
                    vertex.position = tempPositions[posIdx];

                    if (!tempTexCoords.empty() && texIdx >= 0) {
                        vertex.texCoords = tempTexCoords[texIdx];
                    }

                    if (!tempNormals.empty() && normIdx >= 0) {
                        vertex.normal = tempNormals[normIdx];
                    }

                    unsigned int newIndex = static_cast<unsigned int>(vertices.size());
                    vertices.push_back(vertex);
                    indices.push_back(newIndex);
                    vertexMap[tok] = newIndex;
                }
            }
        }
        // Ignore other lines (comments #, materials, etc.)
    }

    std::cout << "Loaded OBJ: " << path
              << " (" << vertices.size() << " vertices, "
              << indices.size() / 3 << " triangles)" << std::endl;

    return Mesh(vertices, indices);
}
```

### C++ Concept: `std::unordered_map`

```cpp
std::unordered_map<std::string, unsigned int> vertexMap;
```

A hash map (dictionary). Key lookups are O(1) average time. We use it to detect duplicate vertices:

```cpp
vertexMap["1/1/1"] = 0;       // "vertex combo 1/1/1 is at index 0"
auto it = vertexMap.find("1/1/1");
if (it != vertexMap.end()) {
    // Found! Reuse index: it->second
}
```

### C++ Concept: `std::istringstream`

```cpp
std::istringstream iss(line);
std::string prefix;
iss >> prefix;
```

An `istringstream` wraps a string and lets you read from it like a file using `>>`. The `>>` operator skips whitespace and reads the next token. This is the idiomatic way to parse simple text formats in C++.

---

## Updating the Shaders for the New Vertex Layout

Our `Vertex` struct now has `position`, `normal`, and `texCoords` at locations 0, 1, and 2. The shaders from Chapter 5 expect colour data at location 1 — we need to update both shaders to match the new layout.

### assets/shaders/basic.vert (updated)

The basic shader previously read a per-vertex **colour** at location 1. Now location 1 is the **normal**. A quick trick: use `abs(normal)` as the colour. Each cube face has a different normal direction, so each face gets a distinct colour — red for X-facing, green for Y-facing, blue for Z-facing. This makes it obvious that rendering is working correctly:

```glsl
#version 460 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;     // Was aColor — now the normal

out vec3 vertexColor;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main() {
    gl_Position = projection * view * model * vec4(aPos, 1.0);

    // Use the absolute normal as colour — each face gets a distinct colour
    // +/-X = red, +/-Y = green, +/-Z = blue
    vertexColor = abs(aNormal);
}
```

The fragment shader (`basic.frag`) stays unchanged — it already reads `vertexColor` and outputs it.

### assets/shaders/textured.vert (updated)

```glsl
#version 460 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;     // NEW
layout (location = 2) in vec2 aTexCoord;   // Moved to location 2

out vec2 TexCoord;
out vec3 Normal;       // Pass to fragment shader for lighting (Chapter 7)
out vec3 FragPos;      // World-space position for lighting

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main() {
    FragPos = vec3(model * vec4(aPos, 1.0));
    Normal = mat3(transpose(inverse(model))) * aNormal;  // Normal matrix
    TexCoord = aTexCoord;
    gl_Position = projection * view * vec4(FragPos, 1.0);
}
```

The `Normal` line looks complex. The **normal matrix** (`mat3(transpose(inverse(model)))`) ensures normals stay perpendicular to the surface even when the object is scaled non-uniformly. For now, just know it's the correct way to transform normals — we'll explain it fully in Chapter 7.

---

## Procedural Meshes — Building Geometry in Code

Not everything comes from files. Floors, walls, and debug shapes can be built in code:

### A Box (Cube) Generator

```cpp
Mesh createBox(float width, float height, float depth) {
    float w = width / 2.0f, h = height / 2.0f, d = depth / 2.0f;

    std::vector<Vertex> vertices = {
        // Front face (normal: 0, 0, 1)
        {{-w, -h,  d}, {0, 0, 1}, {0, 0}},
        {{ w, -h,  d}, {0, 0, 1}, {1, 0}},
        {{ w,  h,  d}, {0, 0, 1}, {1, 1}},
        {{-w,  h,  d}, {0, 0, 1}, {0, 1}},
        // Back face (normal: 0, 0, -1)
        {{ w, -h, -d}, {0, 0, -1}, {0, 0}},
        {{-w, -h, -d}, {0, 0, -1}, {1, 0}},
        {{-w,  h, -d}, {0, 0, -1}, {1, 1}},
        {{ w,  h, -d}, {0, 0, -1}, {0, 1}},
        // Top face (normal: 0, 1, 0)
        {{-w,  h,  d}, {0, 1, 0}, {0, 0}},
        {{ w,  h,  d}, {0, 1, 0}, {1, 0}},
        {{ w,  h, -d}, {0, 1, 0}, {1, 1}},
        {{-w,  h, -d}, {0, 1, 0}, {0, 1}},
        // Bottom face (normal: 0, -1, 0)
        {{-w, -h, -d}, {0, -1, 0}, {0, 0}},
        {{ w, -h, -d}, {0, -1, 0}, {1, 0}},
        {{ w, -h,  d}, {0, -1, 0}, {1, 1}},
        {{-w, -h,  d}, {0, -1, 0}, {0, 1}},
        // Right face (normal: 1, 0, 0)
        {{ w, -h,  d}, {1, 0, 0}, {0, 0}},
        {{ w, -h, -d}, {1, 0, 0}, {1, 0}},
        {{ w,  h, -d}, {1, 0, 0}, {1, 1}},
        {{ w,  h,  d}, {1, 0, 0}, {0, 1}},
        // Left face (normal: -1, 0, 0)
        {{-w, -h, -d}, {-1, 0, 0}, {0, 0}},
        {{-w, -h,  d}, {-1, 0, 0}, {1, 0}},
        {{-w,  h,  d}, {-1, 0, 0}, {1, 1}},
        {{-w,  h, -d}, {-1, 0, 0}, {0, 1}},
    };

    std::vector<unsigned int> indices;
    for (unsigned int face = 0; face < 6; face++) {
        unsigned int base = face * 4;
        indices.push_back(base + 0);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base + 0);
        indices.push_back(base + 2);
        indices.push_back(base + 3);
    }

    return Mesh(vertices, indices);
}
```

24 vertices (4 per face — we can't share corners because normals differ per face) and 36 indices (6 per face, 2 triangles each).

---

## Extending the ResourceManager for Meshes

Multiple entities might use the same mesh (e.g. 50 crates all using the same cube mesh). We shouldn't load it 50 times. In Chapter 5a, we built a `ResourceManager` that caches shaders and textures. The same pattern works for meshes.

Add mesh caching to `ResourceManager`:

### Updated engine/core/resource_manager.h

Add the following alongside the existing shader and texture methods:

```cpp
#include "engine/renderer/mesh.h"

// Inside class ResourceManager:

	// ─── Meshes ──────────────────────────────────────────────
	// Load a mesh from an OBJ file (or return the cached version)
	std::shared_ptr<Mesh> getMesh(
		const std::string& name,
		const std::string& path);

	// Store a procedurally generated mesh
	void storeMesh(const std::string& name, std::shared_ptr<Mesh> mesh);

	// Retrieve a previously loaded mesh by name
	std::shared_ptr<Mesh> getMesh(const std::string& name) const;

// Add to the private section:
	std::unordered_map<std::string, std::shared_ptr<Mesh>> m_meshes;
```

### Updated engine/core/resource_manager.cpp

Add the include for the OBJ loader and the new mesh methods:

```cpp
#include "engine/renderer/obj_loader.h"   // For loadOBJ()

std::shared_ptr<Mesh> ResourceManager::getMesh(
	const std::string& name,
	const std::string& path)
{
	auto it = m_meshes.find(name);
	if (it != m_meshes.end())
	{
		return it->second;
	}

	auto mesh = std::make_shared<Mesh>(loadOBJ(path));
	m_meshes[name] = mesh;
	std::cout << "ResourceManager: cached mesh '" << name << "'" << std::endl;
	return mesh;
}

void ResourceManager::storeMesh(const std::string& name, std::shared_ptr<Mesh> mesh)
{
	m_meshes[name] = mesh;
	std::cout << "ResourceManager: cached mesh '" << name << "'" << std::endl;
}

std::shared_ptr<Mesh> ResourceManager::getMesh(const std::string& name) const
{
	auto it = m_meshes.find(name);
	if (it != m_meshes.end())
	{
		return it->second;
	}

	std::cerr << "ERROR: Mesh '" << name << "' not found in cache" << std::endl;
	return nullptr;
}
```

And update the `clear()` method to also clear meshes:

```cpp
void ResourceManager::clear()
{
	m_shaders.clear();
	m_textures.clear();
	m_meshes.clear();
	std::cout << "ResourceManager: all resources cleared" << std::endl;
}
```

This follows the exact same pattern we used for shaders and textures in Chapter 5a — `std::shared_ptr` with reference counting, `std::unordered_map` for O(1) lookup by name. Now all our assets (shaders, textures, meshes) are managed in one place.

---

## Retiring MeshFactory

In Chapter 5a we introduced `MeshFactory` and `MeshData` — a simple struct holding a raw VAO, VBO, and vertex count. That was fine for getting triangles and quads on screen quickly, but `Mesh` now does everything `MeshData` did and more:

| | `MeshData` (Ch 5a) | `Mesh` (Ch 6) |
|---|---|---|
| Index buffers | No | Yes |
| RAII cleanup | Manual `destroy()` call | Destructor handles it |
| Move semantics | No | Yes |
| Copy protection | No | Yes (deleted copy) |
| Normals | No | Yes |

**`MeshFactory` and `MeshData` are now retired.** Delete `mesh_factory.h` and `mesh_factory.cpp` from your project, and remove `src/engine/core/mesh_factory.cpp` from `CMakeLists.txt` — we won't use them again. Everything they did is now handled by the `Mesh` class and `ResourceManager` mesh caching.

The procedural mesh functions (`createTriangleMesh`, `createQuadMesh`) are replaced by functions like `createBox()` above, which return a proper `Mesh` with index buffers and normals.

---

## Updating setupScene

`setupScene()` from Chapter 5a used `MeshData`. Now it should use `Mesh` via `ResourceManager`:

### Updated src/engine/ecs/scene_setup.h

```cpp
#pragma once

#include <entt/entt.hpp>
#include "engine/core/resource_manager.h"

// Set up the initial scene entities using ResourceManager for all assets
void setupScene(entt::registry& registry, ResourceManager& resources);
```

### Updated src/engine/ecs/scene_setup.cpp

```cpp
#include "engine/ecs/scene_setup.h"
#include "engine/ecs/components.h"

void setupScene(entt::registry& registry, ResourceManager& resources)
{
    auto basicShader = resources.getShader("basic");
    auto texturedShader = resources.getShader("textured");
    auto wallTexture = resources.getTexture("wall");
    auto cubeMesh = resources.getMesh("cube");

    // Create a cube entity
    auto cube = registry.create();
    registry.emplace<Position>(cube, glm::vec3(0.0f, 0.0f, -3.0f));
    registry.emplace<MeshRenderer>(cube, cubeMesh->getVAO(),
                                    0u,
                                    basicShader->getId(),
                                    0u,
                                    true,
                                    cubeMesh->getIndexCount());

    // Create a textured wall
    auto wall = registry.create();
    registry.emplace<Position>(wall, glm::vec3(2.0f, 0.0f, -3.0f));
    registry.emplace<MeshRenderer>(wall, cubeMesh->getVAO(),
                                    0u,
                                    texturedShader->getId(),
                                    wallTexture->getId(),
                                    true,
                                    cubeMesh->getIndexCount());
}
```

Notice how much cleaner this is — `setupScene` just asks `ResourceManager` for assets by name. It doesn't need to know about file paths or mesh construction.

---

## Updated main.cpp

Here is the complete `main.cpp` after this chapter's changes. Compare it with the Chapter 5a version — the `MeshFactory`/`MeshData` code is gone, replaced by the OBJ-loaded `Mesh` via `ResourceManager`. The `setupScene()` call is simpler too, since it now just takes `resources`:

### src/main.cpp

```cpp
#include "engine/core/window.h"
#include "engine/core/input_manager.h"
#include "engine/core/resource_manager.h"
#include "engine/ecs/components.h"
#include "engine/ecs/scene_setup.h"
#include "engine/ecs/systems/render_system.h"
#include "engine/ecs/systems/movement_system.h"
#include "engine/renderer/camera.h"

#include <entt/entt.hpp>

int main()
{
    // ─── Core systems ────────────────────────────────────────
    Window window(1280, 720, "QEngine");

    InputManager input;
    input.init(window.getHandle());

    ResourceManager resources;

    // ─── Load resources ──────────────────────────────────────
    auto basicShader = resources.getShader("basic",
        "assets/shaders/basic.vert",
        "assets/shaders/basic.frag");

    auto texturedShader = resources.getShader("textured",
        "assets/shaders/textured.vert",
        "assets/shaders/textured.frag");

    auto wallTexture = resources.getTexture("wall", "assets/textures/wall.png");

    // Load the cube from the OBJ file we saved earlier
    auto cubeMesh = resources.getMesh("cube", "assets/models/cube.obj");

    // You can also create meshes procedurally and cache them:
    // auto boxMesh = std::make_shared<Mesh>(createBox(1.0f, 1.0f, 1.0f));
    // resources.storeMesh("box", boxMesh);

    // ─── Camera ──────────────────────────────────────────────
    Camera camera(glm::vec3(0.0f, 0.0f, 3.0f));

    // ─── ECS: Create the world ───────────────────────────────
    entt::registry registry;
    setupScene(registry, resources);

    // ─── Game Loop ───────────────────────────────────────────
    float deltaTime = 0.0f;
    float lastFrame = 0.0f;

    glEnable(GL_DEPTH_TEST);

    while (!window.shouldClose())
    {
        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        input.update();
        window.pollEvents();

        // ─── Input ───────────────────────────────────────────
        if (input.isKeyPressed(GLFW_KEY_ESCAPE))
            glfwSetWindowShouldClose(window.getHandle(), true);

        if (input.isKeyPressed(GLFW_KEY_W))
            camera.processKeyboard(Camera::FORWARD, deltaTime);
        if (input.isKeyPressed(GLFW_KEY_S))
            camera.processKeyboard(Camera::BACKWARD, deltaTime);
        if (input.isKeyPressed(GLFW_KEY_A))
            camera.processKeyboard(Camera::LEFT, deltaTime);
        if (input.isKeyPressed(GLFW_KEY_D))
            camera.processKeyboard(Camera::RIGHT, deltaTime);

        camera.processMouse(input.getMouseXOffset(), input.getMouseYOffset());

        // ─── ECS Systems (tick order!) ───────────────────────
        movementSystem(registry, deltaTime);
        // ... future systems go here ...

        // ─── Render ──────────────────────────────────────────
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        float aspectRatio = static_cast<float>(window.getWidth())
                          / static_cast<float>(window.getHeight());
        renderSystem(registry, camera, aspectRatio);

        window.swapBuffers();
    }

    resources.clear();

    return 0;
}
```

### What changed from Chapter 5a

| Before (Ch 5a) | After (Ch 6) |
|---|---|
| `#include "engine/core/mesh_factory.h"` | Removed — no longer needed |
| `MeshData triangleMesh = MeshFactory::createTriangleMesh();` | `auto cubeMesh = resources.getMesh("cube", "assets/models/cube.obj");` |
| `MeshData quadMesh = MeshFactory::createQuadMesh();` | Removed — cube replaces both |
| `setupScene(registry, triangleMesh, quadMesh, basicShader, texturedShader, wallTexture);` | `setupScene(registry, resources);` |
| `triangleMesh.destroy();` / `quadMesh.destroy();` | Removed — `Mesh` destructor handles cleanup |

Everything now goes through `resources` — shaders, textures, and meshes. No more manual `destroy()` calls.

---

## Update CMakeLists.txt

Add the new files and remove `mesh_factory.cpp` (retired this chapter):

```cmake
add_executable(QEngine
    src/main.cpp
    src/engine/core/input_manager.cpp
    src/engine/core/resource_manager.cpp
    src/engine/core/window.cpp
    src/engine/ecs/scene_setup.cpp
    src/engine/ecs/systems/movement_system.cpp
    src/engine/ecs/systems/render_system.cpp
    src/engine/renderer/camera.cpp
    src/engine/renderer/mesh.cpp          # NEW — replaces mesh_factory
    src/engine/renderer/obj_loader.cpp    # NEW
    src/engine/renderer/shader.cpp
    src/engine/renderer/stb_image_impl.cpp
    src/engine/renderer/texture.cpp
)
```

Note that `mesh_factory.cpp` is gone — the `Mesh` class and `createBox()` replace it entirely.

---

## What's Next

In **Chapter 7**, we'll add lighting — directional lights, point lights, and the Phong lighting model. This is what transforms flat-textured surfaces into a world with depth, shadow, and atmosphere. The normals we've been carefully passing through will finally be put to use.
