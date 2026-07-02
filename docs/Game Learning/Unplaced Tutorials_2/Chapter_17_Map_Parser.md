# Chapter 17: .map Parser & Brush Rendering

## What You'll Learn
- How the Quake/TrenchBroom `.map` format encodes level geometry as intersecting half-planes
- Parsing `.map` text into structured brush and entity data
- Converting plane definitions into renderable polygons using the Sutherland-Hodgman clipping algorithm
- Computing texture coordinates from `.map` UV parameters
- Building GPU meshes grouped by texture for efficient rendering
- Loading and rendering a complete TrenchBroom-authored level

This is the most technically important chapter in the TrenchBroom series. By the end, QEngine loads a `.map` file and renders it as textured geometry --- replacing the hardcoded sector/portal system entirely.

---

## Step 1: Understanding the .map Format

Before writing any code, you need to understand what a `.map` file contains and how it represents 3D geometry.

### Entities, Brushes, and Planes

A `.map` file is plain text. It contains a list of **entities**. Each entity has key-value properties (like `"classname" "worldspawn"`) and optionally one or more **brushes**. A brush is a convex 3D solid defined by the intersection of **planes**.

Here is a minimal `.map` file containing one room and one light:

```
// entity 0 -- worldspawn (the level geometry)
{
"classname" "worldspawn"
// brush 0 -- floor
{
( -256 0 -256 ) ( -256 0 256 ) ( 256 0 -256 ) floor1 0 0 0 1 1
( 256 -16 256 ) ( 256 -16 -256 ) ( -256 -16 256 ) floor1 0 0 0 1 1
( -256 -16 -256 ) ( -256 0 -256 ) ( 256 -16 -256 ) floor1 0 0 0 1 1
( 256 -16 256 ) ( -256 -16 256 ) ( -256 0 256 ) floor1 0 0 0 1 1
( -256 -16 256 ) ( -256 -16 -256 ) ( -256 0 256 ) floor1 0 0 0 1 1
( 256 -16 -256 ) ( 256 -16 256 ) ( 256 0 -256 ) floor1 0 0 0 1 1
}
}
// entity 1 -- a point light
{
"classname" "light"
"origin" "0 128 0"
"light" "300"
}
```

### Anatomy of a Plane Line

Each line inside a brush block defines one plane:

```
( x1 y1 z1 ) ( x2 y2 z2 ) ( x3 y3 z3 ) textureName offsetX offsetY rotation scaleX scaleY
```

- **Three points** `(x1 y1 z1)`, `(x2 y2 z2)`, `(x3 y3 z3)` --- these lie on the plane and define it uniquely
- **textureName** --- the texture to apply to this face (e.g. `floor1`, `wall_grey`)
- **offsetX, offsetY** --- texture offset in pixels
- **rotation** --- texture rotation in degrees
- **scaleX, scaleY** --- texture scale (1 = no scaling)

The plane's normal and distance are computed from the three points:

```
normal = normalize(cross(p2 - p1, p3 - p1))
dist   = dot(normal, p1)
```

### What Is a Brush?

A brush is a **convex** solid --- the intersection of all its half-spaces. Each plane divides space into two halves. The "inside" of the brush is the region that is on the back side of every plane (the side opposite the normal).

```
     plane normal points OUTWARD
           |
           v
    ───────┼───────
    inside │ outside
    (solid)│ (empty)
```

A box brush has 6 planes. More complex shapes (wedges, pillars, angled walls) use more planes. The key insight: you never store vertices in the `.map` file. You compute them by intersecting the planes.

### Point Entities vs Brush Entities

- **Brush entities** (`worldspawn`, `func_door`, `trigger_once`) contain brushes --- they have geometry
- **Point entities** (`light`, `info_player_start`, `item_health`) have an `"origin"` property but no brushes --- they place objects in the world

### Coordinate System

The Quake `.map` format and OpenGL both use right-handed coordinates. QEngine uses Y-up (OpenGL convention). TrenchBroom can be configured for either; with the "Standard" format and no special transforms, the coordinates map directly. We will not need any axis swaps for this chapter.

---

## Step 2: TrenchBroom Basics

Before writing the parser, install TrenchBroom and build a test map by hand. This gives you a concrete file to load and a feel for how the editor works.

### Installation

1. Download TrenchBroom from https://trenchbroom.github.io/
2. Install and launch it
3. When prompted for a game, select **Generic** (we set up a QEngine profile in Chapter 20)
4. Choose **Standard** map format when asked

### The Editor Layout

TrenchBroom shows four viewports: a 3D perspective view and three 2D orthographic views (top, front, side). The 3D view is where you preview your work. The 2D views are where you do precise building.

```
┌──────────────────┬──────────────────┐
│                  │                  │
│   3D Perspective │   Top (XZ)      │
│                  │                  │
├──────────────────┼──────────────────┤
│                  │                  │
│   Front (XY)    │   Side (YZ)     │
│                  │                  │
└──────────────────┴──────────────────┘
```

### Controls Quick Reference

| Action | Key/Mouse |
|--------|-----------|
| Move camera (3D) | WASD + right-click drag |
| Look around (3D) | Right-click drag |
| Zoom (2D) | Scroll wheel |
| Create brush | Left-click drag in a 2D view, then drag upward in another 2D view to set height |
| Select | Left-click in 3D or 2D view |
| Move selection | Left-click drag on the selection |
| Resize brush | Drag the face handles (small squares on edges) |
| Rotate | R key, then drag |
| Apply texture | Select a face (Shift+click in 3D), then click a texture in the texture browser |
| Clone brush | Ctrl+D |
| Vertex editing | V key |
| Delete | Delete key |
| Undo | Ctrl+Z |

### Building Your First Room

**Step A: Create the floor**

1. In the **top-down (XZ)** 2D view, left-click and drag to create a rectangle roughly 512 x 512 units
2. Switch to the **front (XY)** 2D view and drag the top edge of the new brush down so it is about 16 units thick
3. You now have a flat slab — this is your floor

**Step B: Hollow the room (the easy way)**

Instead of building 6 individual brushes, TrenchBroom can hollow a box:

1. Create a large box brush: 512 x 256 x 512 units
2. Select it, then go to **Edit > CSG > Hollow** (or press **Ctrl+Shift+H**)
3. Set wall thickness to **16** or **32**
4. TrenchBroom splits the single box into 6 thin brushes forming walls, floor, and ceiling
5. The interior is now empty space — your room

**Step C: Apply textures**

1. Open the texture browser (View > Toggle Texture Browser, or press **T**)
2. With Generic game selected, you will see a few default textures. Later (Ch 20) you will add QEngine's own textures
3. Select a face: hold **Shift** and left-click a face in the 3D view. The face highlights
4. Click a texture in the browser to apply it
5. You can select all faces of a brush by clicking the brush (no Shift) and then applying a texture — it applies to all faces

**Step D: Place a light**

1. Right-click in the 3D view where you want the light
2. Select **Create Point Entity > light**
3. A small light entity appears. In the properties panel (right side), you can set:
   - `light` — brightness (e.g. 200)
   - `_color` — light color as `R G B` (0-255 each)

**Step E: Place a player spawn**

1. Right-click in the 3D view on the floor inside your room
2. Select **Create Point Entity > info_player_start**
3. Position it so it is slightly above the floor (drag it up in the front view)
4. In properties, set `angle` to the facing direction in degrees (0 = east, 90 = north, 180 = west, 270 = south)

**Step F: Save**

1. File > Save As
2. Save to `assets/maps/test.map` inside your QEngine project directory
3. Open the file in a text editor — you should see the entity/brush/plane format described in Step 1

### Connecting Multiple Rooms

To add a second room connected by a doorway:

1. Create another hollow box adjacent to the first room
2. Delete the wall brushes where the rooms meet (select the brush, press Delete)
3. Or resize the brushes to leave a gap — this gap is your doorway

TrenchBroom's geometry is **subtractive** in concept: rooms are empty space between solid brushes. If two rooms share a wall, delete one copy of it.

### Brush Entities

To make a door in TrenchBroom:

1. Create a brush that fills the doorway
2. Select it
3. Right-click > **Create Brush Entity > func_door** (or whichever brush entity is available)
4. The brush turns into an entity with its own properties panel
5. Set properties like `speed`, `angle`, `lip` in the properties editor

> **Tip:** You do not need the QEngine game profile yet. The Generic profile lets you create worldspawn brushes and basic entities. Chapter 20 sets up the full QEngine profile so TrenchBroom shows all your custom entities and textures.

---

## Step 3: Data Structures

Create the header file that defines all the types needed to represent a parsed `.map` file.

### New file: `src/engine/level/map_parser.h`

```cpp
#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <map>

// ─── Raw parsed data from a .map file ────────────────────────────

struct MapPlane
{
    glm::vec3 points[3];     // Three points defining the plane
    glm::vec3 normal;        // Computed: normalize(cross(p2-p1, p3-p1))
    float dist;              // Computed: dot(normal, p1)
    std::string textureName;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    float rotation = 0.0f;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
};

struct MapBrushFace
{
    MapPlane plane;
    std::vector<glm::vec3> vertices;  // Computed polygon vertices (wound CCW)
    std::vector<glm::vec2> uvs;       // Computed UVs, one per vertex
};

struct MapBrush
{
    std::vector<MapBrushFace> faces;
};

struct MapEntity
{
    std::map<std::string, std::string> properties;
    std::vector<MapBrush> brushes;  // Empty for point entities
};

struct MapFile
{
    std::vector<MapEntity> entities;
};

// ─── Parsing ─────────────────────────────────────────────────────

MapFile parseMapFile(const std::string& filepath);

// ─── Geometry construction ───────────────────────────────────────

void buildBrushFaces(MapBrush& brush);
void computeUVs(MapBrushFace& face, int texWidth, int texHeight);
```

### Why These Structs?

The data flows through a pipeline:

1. **Parse** --- read the `.map` text into `MapPlane` data (three points, texture info)
2. **Build faces** --- compute actual polygon vertices for each plane by clipping against all other planes in the brush
3. **Compute UVs** --- use the texture parameters to generate texture coordinates per vertex
4. **Build meshes** --- triangulate the polygons and upload to the GPU

Each struct represents a stage of this pipeline. `MapPlane` is raw input. `MapBrushFace` adds the computed geometry. `MapBrush` groups faces into a convex solid. `MapEntity` groups brushes with their properties. `MapFile` is the whole level.

---

## Step 4: Parse the .map Text

The parser reads the `.map` file line by line and builds the `MapFile` structure. It handles:
- Entity blocks (outer `{ }`)
- Key-value properties (`"key" "value"`)
- Brush blocks (inner `{ }`)
- Plane lines (`( ... ) ( ... ) ( ... ) texture params`)
- Comments (`//`)

### New file: `src/engine/level/map_parser.cpp`

```cpp
#include "engine/level/map_parser.h"

#include <fstream>
#include <sstream>
#include <iostream>
#include <cmath>
#include <algorithm>

// ─── Plane-line parser ───────────────────────────────────────────

static bool parsePlaneLine(const std::string& line, MapPlane& plane)
{
    // Expected format:
    // ( x1 y1 z1 ) ( x2 y2 z2 ) ( x3 y3 z3 ) texName offX offY rot scX scY

    std::istringstream iss(line);
    char ch;

    // Read three points, each enclosed in parentheses
    for (int i = 0; i < 3; i++)
    {
        iss >> ch; // '('
        if (ch != '(') return false;

        iss >> plane.points[i].x >> plane.points[i].y >> plane.points[i].z;

        iss >> ch; // ')'
        if (ch != ')') return false;
    }

    // Texture name and UV parameters
    iss >> plane.textureName;
    iss >> plane.offsetX >> plane.offsetY;
    iss >> plane.rotation;
    iss >> plane.scaleX >> plane.scaleY;

    // Compute the plane equation from the three points
    glm::vec3 edge1 = plane.points[1] - plane.points[0];
    glm::vec3 edge2 = plane.points[2] - plane.points[0];
    glm::vec3 cross = glm::cross(edge1, edge2);

    float len = glm::length(cross);
    if (len < 0.0001f)
    {
        std::cerr << "WARNING: degenerate plane (collinear points)" << std::endl;
        return false;
    }

    plane.normal = cross / len;
    plane.dist = glm::dot(plane.normal, plane.points[0]);

    return true;
}

// ─── Top-level parser ────────────────────────────────────────────

MapFile parseMapFile(const std::string& filepath)
{
    MapFile mapFile;

    std::ifstream file(filepath);
    if (!file.is_open())
    {
        std::cerr << "ERROR: Could not open map file: " << filepath << std::endl;
        return mapFile;
    }

    std::string line;
    int depth = 0;              // Brace nesting depth
    bool inEntity = false;
    bool inBrush = false;

    MapEntity currentEntity;
    MapBrush currentBrush;

    while (std::getline(file, line))
    {
        // Trim leading whitespace
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        line = line.substr(start);

        // Skip empty lines and comments
        if (line.empty()) continue;
        if (line.size() >= 2 && line[0] == '/' && line[1] == '/') continue;

        if (line[0] == '{')
        {
            depth++;
            if (depth == 1)
            {
                // Entering an entity block
                inEntity = true;
                currentEntity = MapEntity{};
            }
            else if (depth == 2)
            {
                // Entering a brush block (inside an entity)
                inBrush = true;
                currentBrush = MapBrush{};
            }
            continue;
        }

        if (line[0] == '}')
        {
            if (depth == 2 && inBrush)
            {
                // Closing a brush block
                currentEntity.brushes.push_back(std::move(currentBrush));
                inBrush = false;
            }
            else if (depth == 1 && inEntity)
            {
                // Closing an entity block
                mapFile.entities.push_back(std::move(currentEntity));
                inEntity = false;
            }
            depth--;
            continue;
        }

        if (inBrush && line[0] == '(')
        {
            // This is a plane definition line
            MapPlane plane;
            if (parsePlaneLine(line, plane))
            {
                MapBrushFace face;
                face.plane = plane;
                currentBrush.faces.push_back(std::move(face));
            }
        }
        else if (inEntity && !inBrush && line[0] == '"')
        {
            // This is a key-value property line: "key" "value"
            size_t firstQuoteEnd = line.find('"', 1);
            if (firstQuoteEnd == std::string::npos) continue;

            std::string key = line.substr(1, firstQuoteEnd - 1);

            size_t secondQuoteStart = line.find('"', firstQuoteEnd + 1);
            if (secondQuoteStart == std::string::npos) continue;

            size_t secondQuoteEnd = line.find('"', secondQuoteStart + 1);
            if (secondQuoteEnd == std::string::npos) continue;

            std::string value = line.substr(
                secondQuoteStart + 1,
                secondQuoteEnd - secondQuoteStart - 1
            );

            currentEntity.properties[key] = value;
        }
    }

    std::cout << "Parsed map file: " << filepath
              << " (" << mapFile.entities.size() << " entities)" << std::endl;

    return mapFile;
}
```

### How It Works

The parser tracks brace nesting depth:
- **Depth 0**: outside everything
- **Depth 1**: inside an entity block --- read key-value properties
- **Depth 2**: inside a brush block --- read plane lines

When a closing brace drops us from depth 2 to 1, the completed brush is added to the current entity. When we drop from depth 1 to 0, the completed entity is added to the map file.

Plane lines are identified by starting with `(`. Property lines are identified by starting with `"`. Everything else (comments, blank lines) is skipped.

---

## Step 5: Plane-to-Polygon Conversion (The Core Algorithm)

This is the heart of the `.map` loader. We need to turn a set of planes into actual polygon vertices that can be sent to the GPU.

### The Problem

A brush is defined by N planes. Each plane is infinite. The brush is the finite convex region where all planes overlap. We need to find the polygon on each plane's surface that forms the boundary of this region.

```
    Plane A ─────────┐
    Plane B ─────┐   │
    Plane C ──┐  │   │
              v  v   v
           ┌──────────┐
           │          │  <-- This is the brush:
           │  convex  │      the intersection of all
           │  solid   │      half-spaces
           │          │
           └──────────┘
```

### The Approach: Clipping

For each face in a brush:
1. Create a large polygon lying on that face's plane
2. Clip this polygon against every **other** plane in the brush, keeping only the part that is inside the brush
3. What remains is the face polygon

This is conceptually simple and numerically robust. The alternative --- computing all triple-plane intersections and sorting them --- works too, but is harder to get right with floating-point edge cases.

### Sutherland-Hodgman Clipping

The clipping algorithm processes one plane at a time. For each edge of the polygon, it tests both endpoints against the clipping plane:

```
Case 1: Both inside  --> keep the second point
Case 2: Both outside --> keep nothing
Case 3: Inside->Out  --> keep the intersection point
Case 4: Outside->In  --> keep the intersection point AND the second point
```

```
                  clipping plane
                       |
    A -------- B       |        Case 3: A inside, B outside
         \             |        --> emit intersection point I
          \   I        |
           \--X--------|
              |  \     |
              |   B    |
```

### Triple-Plane Intersection (Alternative)

For completeness, here is the alternative approach. Given three planes A, B, C with normals and distances, the intersection point (if it exists) is:

```
        cross(B.normal, C.normal) * A.dist
      + cross(C.normal, A.normal) * B.dist
      + cross(A.normal, B.normal) * C.dist
point = ─────────────────────────────────────
               det(A.normal, B.normal, C.normal)
```

You would compute all (N choose 3) intersections, discard any point that lies outside any plane, then sort the remaining points per face into a polygon. This is O(N^3) vs the clipping approach's O(N^2), and the sorting step adds complexity. We use clipping instead.

### Implementation

Add the following functions to `map_parser.cpp`, above the `parseMapFile` function:

```cpp
// ─── Constants ───────────────────────────────────────────────────

static constexpr float EPSILON = 0.001f;
static constexpr float LARGE_POLYGON_SIZE = 65536.0f;

// ─── Classify a point relative to a plane ────────────────────────

static float planeDist(const glm::vec3& point, const glm::vec3& normal, float dist)
{
    return glm::dot(normal, point) - dist;
}

// ─── Create a large initial polygon on a plane ───────────────────
//
// We need a starting polygon that lies on the face's plane and is
// large enough to contain the entire brush face. We do this by
// choosing two axes perpendicular to the normal and creating a
// huge square.

static std::vector<glm::vec3> computeInitialPolygon(
    const glm::vec3& normal, float dist)
{
    // Choose an axis that is NOT parallel to the normal
    glm::vec3 up;
    if (std::abs(normal.y) < 0.99f)
        up = glm::vec3(0.0f, 1.0f, 0.0f);
    else
        up = glm::vec3(1.0f, 0.0f, 0.0f);

    // Build two axes on the plane
    glm::vec3 uAxis = glm::normalize(glm::cross(normal, up));
    glm::vec3 vAxis = glm::cross(normal, uAxis);

    // Centre point of the polygon (closest point on the plane to the origin)
    glm::vec3 centre = normal * dist;

    // Four corners of a large square on the plane
    float s = LARGE_POLYGON_SIZE;
    std::vector<glm::vec3> polygon;
    polygon.push_back(centre - uAxis * s - vAxis * s);
    polygon.push_back(centre + uAxis * s - vAxis * s);
    polygon.push_back(centre + uAxis * s + vAxis * s);
    polygon.push_back(centre - uAxis * s + vAxis * s);

    return polygon;
}

// ─── Sutherland-Hodgman: clip a polygon by a single plane ────────
//
// Keeps the part of the polygon that is on the BACK side of the
// clipping plane (inside the brush). The "back side" is where
// dot(normal, point) - dist < 0.
//
// For brush clipping, "inside" means the point satisfies the
// half-space inequality: dot(clipNormal, point) <= clipDist.

static std::vector<glm::vec3> clipPolygonByPlane(
    const std::vector<glm::vec3>& polygon,
    const glm::vec3& clipNormal,
    float clipDist)
{
    if (polygon.empty()) return {};

    std::vector<glm::vec3> result;
    int count = static_cast<int>(polygon.size());

    for (int i = 0; i < count; i++)
    {
        const glm::vec3& current = polygon[i];
        const glm::vec3& next = polygon[(i + 1) % count];

        float currentDist = planeDist(current, clipNormal, clipDist);
        float nextDist = planeDist(next, clipNormal, clipDist);

        bool currentInside = (currentDist <= EPSILON);
        bool nextInside = (nextDist <= EPSILON);

        if (currentInside)
        {
            result.push_back(current);

            if (!nextInside)
            {
                // Edge crosses from inside to outside -- emit intersection
                float t = currentDist / (currentDist - nextDist);
                glm::vec3 intersection = current + t * (next - current);
                result.push_back(intersection);
            }
        }
        else if (nextInside)
        {
            // Edge crosses from outside to inside -- emit intersection and next
            float t = currentDist / (currentDist - nextDist);
            glm::vec3 intersection = current + t * (next - current);
            result.push_back(intersection);
        }
        // Both outside: emit nothing
    }

    return result;
}

// ─── Build all face polygons for a brush ─────────────────────────

void buildBrushFaces(MapBrush& brush)
{
    int faceCount = static_cast<int>(brush.faces.size());

    for (int i = 0; i < faceCount; i++)
    {
        MapBrushFace& face = brush.faces[i];
        const MapPlane& facePlane = face.plane;

        // Step 1: Start with a large polygon on this face's plane
        std::vector<glm::vec3> polygon =
            computeInitialPolygon(facePlane.normal, facePlane.dist);

        // Step 2: Clip against every OTHER plane in the brush
        for (int j = 0; j < faceCount; j++)
        {
            if (i == j) continue;
            if (polygon.size() < 3) break;  // Nothing left to clip

            const MapPlane& clipPlane = brush.faces[j].plane;
            polygon = clipPolygonByPlane(
                polygon, clipPlane.normal, clipPlane.dist);
        }

        // Step 3: Store the result if valid
        if (polygon.size() >= 3)
        {
            face.vertices = std::move(polygon);
        }
        else
        {
            face.vertices.clear();
        }
    }
}
```

### Understanding the Clipping

Let's trace through a simple example. Imagine a 2D rectangle brush with 4 planes (top, bottom, left, right). We want the polygon for the "top" face:

```
Step 1: Create a huge line segment on the top plane

   <──────────────────────────────────────────>
                (very wide)

Step 2: Clip by the LEFT plane -- cuts off the left end

                ├─────────────────────────────>

Step 3: Clip by the RIGHT plane -- cuts off the right end

                ├──────────────────────┤

Step 4: Clip by the BOTTOM plane -- no effect (it's parallel)

                ├──────────────────────┤
                       (done!)
```

In 3D, the same process happens with polygons instead of line segments. Each clip can turn a polygon into a smaller polygon, potentially adding new vertices where edges cross the clipping plane.

### Why EPSILON Matters

Floating-point arithmetic is imprecise. Without an epsilon tolerance, vertices that should lie exactly on a plane might be classified as barely inside or barely outside, causing missing faces or slivers. The `EPSILON = 0.001f` gives a small margin that prevents these artefacts.

---

## Step 6: UV Calculation

Each face in a `.map` file carries texture alignment data: offset, rotation, and scale. We use these to compute texture coordinates for every vertex.

### How Quake UV Mapping Works

The `.map` format uses **axial projection**. The idea:
1. Look at the face normal to determine which axis it faces most
2. Choose two texture axes (U and V) based on that dominant direction
3. Project each vertex onto those axes to get raw UV
4. Apply rotation, scale, and offset

### Texture Axis Selection

The face normal tells us which direction the face points. We pick texture axes that are perpendicular to the dominant component of the normal:

```
If the face points mostly UP or DOWN   (|normal.y| is largest):
    U axis = (1, 0, 0)    (world X)
    V axis = (0, 0, -1)   (world -Z)

If the face points mostly RIGHT or LEFT (|normal.x| is largest):
    U axis = (0, 1, 0)    (world Y)
    V axis = (0, 0, -1)   (world -Z)

If the face points mostly FORWARD or BACK (|normal.z| is largest):
    U axis = (1, 0, 0)    (world X)
    V axis = (0, -1, 0)   (world -Y)
```

This ensures that textures on floors project from above, textures on walls project from the side, and so on. It matches how Quake and TrenchBroom work.

### Implementation

Add this function to `map_parser.cpp`:

```cpp
// ─── Compute UVs for a face polygon ─────────────────────────────

void computeUVs(MapBrushFace& face, int texWidth, int texHeight)
{
    if (face.vertices.empty()) return;

    const MapPlane& p = face.plane;

    // Choose texture axes based on the dominant normal component
    glm::vec3 uAxis, vAxis;

    float absX = std::abs(p.normal.x);
    float absY = std::abs(p.normal.y);
    float absZ = std::abs(p.normal.z);

    if (absY >= absX && absY >= absZ)
    {
        // Floor or ceiling -- project along Y
        uAxis = glm::vec3(1.0f, 0.0f, 0.0f);
        vAxis = glm::vec3(0.0f, 0.0f, -1.0f);
    }
    else if (absX >= absY && absX >= absZ)
    {
        // Left/right wall -- project along X
        uAxis = glm::vec3(0.0f, 1.0f, 0.0f);
        vAxis = glm::vec3(0.0f, 0.0f, -1.0f);
    }
    else
    {
        // Front/back wall -- project along Z
        uAxis = glm::vec3(1.0f, 0.0f, 0.0f);
        vAxis = glm::vec3(0.0f, -1.0f, 0.0f);
    }

    // Apply rotation to the texture axes
    if (std::abs(p.rotation) > 0.001f)
    {
        float rad = glm::radians(p.rotation);
        float cosR = std::cos(rad);
        float sinR = std::sin(rad);

        glm::vec3 rotatedU = uAxis * cosR - vAxis * sinR;
        glm::vec3 rotatedV = uAxis * sinR + vAxis * cosR;
        uAxis = rotatedU;
        vAxis = rotatedV;
    }

    // Compute UV for each vertex
    face.uvs.resize(face.vertices.size());

    float safeScaleX = (std::abs(p.scaleX) < 0.0001f) ? 1.0f : p.scaleX;
    float safeScaleY = (std::abs(p.scaleY) < 0.0001f) ? 1.0f : p.scaleY;

    for (size_t i = 0; i < face.vertices.size(); i++)
    {
        const glm::vec3& vertex = face.vertices[i];

        float u = glm::dot(vertex, uAxis) / safeScaleX + p.offsetX;
        float v = glm::dot(vertex, vAxis) / safeScaleY + p.offsetY;

        // Normalise by texture dimensions so UVs are in 0..1 range
        // (textures will typically tile via GL_REPEAT)
        if (texWidth > 0) u /= static_cast<float>(texWidth);
        if (texHeight > 0) v /= static_cast<float>(texHeight);

        face.uvs[i] = glm::vec2(u, v);
    }
}
```

### Why Safe Scale?

TrenchBroom allows a scale of 0, which would cause a division by zero. The `safeScaleX`/`safeScaleY` guards prevent this. In practice, TrenchBroom defaults scale to 1.0, so this is just defensive coding.

---

## Step 7: Build Renderable Meshes

Now we have polygons with UV coordinates for every face of every brush. The next step is to group them by texture, triangulate them, and upload them to the GPU as meshes.

### New file: `src/engine/level/map_renderer.h`

```cpp
#pragma once

#include "engine/level/map_parser.h"
#include "engine/core/resource_manager.h"
#include "engine/renderer/mesh.h"

#include <memory>
#include <string>
#include <vector>

struct MapMesh
{
    std::shared_ptr<Mesh> mesh;
    unsigned int textureId = 0;
    unsigned int shaderId = 0;
    std::string textureName;
};

// Process a parsed MapFile into renderable meshes.
// Groups faces by texture for efficient rendering.
// Each MapMesh holds one VAO with all geometry sharing a texture.
std::vector<MapMesh> buildMapMeshes(
    MapFile& mapFile,
    ResourceManager& resources,
    unsigned int defaultShaderId,
    const std::string& textureBasePath,
    int defaultTexSize = 64
);
```

### New file: `src/engine/level/map_renderer.cpp`

```cpp
#include "engine/level/map_renderer.h"
#include "engine/ecs/components.h"

#include <unordered_map>
#include <iostream>

// ─── Fan triangulation of a convex polygon ───────────────────────
//
// Given polygon vertices [v0, v1, v2, v3, v4, ...], produce
// triangles: (v0,v1,v2), (v0,v2,v3), (v0,v3,v4), ...
//
//      v3 ──── v2
//      │ \   / │       Triangles from v0:
//      │  \ /  │       (v0, v1, v2)
//      │   v0  │       (v0, v2, v3)
//      │  / \  │
//      │ /   \ │
//      v4 ──── v1

static void triangulateFace(
    const MapBrushFace& face,
    std::vector<Vertex>& outVertices,
    std::vector<unsigned int>& outIndices)
{
    if (face.vertices.size() < 3) return;

    unsigned int baseIndex = static_cast<unsigned int>(outVertices.size());

    // Add all vertices
    for (size_t i = 0; i < face.vertices.size(); i++)
    {
        Vertex v;
        v.position = face.vertices[i];
        v.normal = face.plane.normal;
        v.texCoords = (i < face.uvs.size())
            ? face.uvs[i]
            : glm::vec2(0.0f);
        outVertices.push_back(v);
    }

    // Fan triangulation from vertex 0
    for (size_t i = 1; i + 1 < face.vertices.size(); i++)
    {
        outIndices.push_back(baseIndex);
        outIndices.push_back(baseIndex + static_cast<unsigned int>(i));
        outIndices.push_back(baseIndex + static_cast<unsigned int>(i + 1));
    }
}

// ─── Build meshes from a parsed map file ─────────────────────────

std::vector<MapMesh> buildMapMeshes(
    MapFile& mapFile,
    ResourceManager& resources,
    unsigned int defaultShaderId,
    const std::string& textureBasePath,
    int defaultTexSize)
{
    // Collect all faces by texture name. This way, all geometry
    // sharing a texture goes into one draw call.
    struct TextureGroup
    {
        std::vector<Vertex> vertices;
        std::vector<unsigned int> indices;
    };

    std::unordered_map<std::string, TextureGroup> groups;

    for (auto& entity : mapFile.entities)
    {
        // Only process worldspawn brushes in this chapter.
        // Brush entities (func_door, triggers) are handled in Ch 18.
        auto it = entity.properties.find("classname");
        if (it == entity.properties.end()) continue;
        if (it->second != "worldspawn") continue;

        for (auto& brush : entity.brushes)
        {
            // Build the polygon geometry for this brush
            buildBrushFaces(brush);

            // Compute UVs and triangulate each face
            for (auto& face : brush.faces)
            {
                if (face.vertices.size() < 3) continue;

                // Look up or load the texture to get its dimensions
                std::string texName = face.plane.textureName;
                int texW = defaultTexSize;
                int texH = defaultTexSize;

                // Try to load the texture if not already cached
                std::string texPath = textureBasePath + texName + ".png";
                auto tex = resources.getTexture(texName, texPath);
                if (tex)
                {
                    texW = tex->getWidth();
                    texH = tex->getHeight();
                }

                computeUVs(face, texW, texH);

                triangulateFace(face, groups[texName].vertices,
                                      groups[texName].indices);
            }
        }
    }

    // Build a Mesh (VBO/VAO) per texture group
    std::vector<MapMesh> meshes;

    for (auto& [texName, group] : groups)
    {
        if (group.vertices.empty()) continue;

        MapMesh mapMesh;
        mapMesh.textureName = texName;
        mapMesh.shaderId = defaultShaderId;
        mapMesh.mesh = std::make_shared<Mesh>(group.vertices, group.indices);

        // Look up texture ID
        auto tex = resources.getTexture(texName);
        if (tex)
        {
            mapMesh.textureId = tex->getId();
        }

        meshes.push_back(std::move(mapMesh));

        std::cout << "  Map mesh: " << texName
                  << " (" << group.vertices.size() << " verts, "
                  << group.indices.size() / 3 << " tris)" << std::endl;
    }

    std::cout << "Built " << meshes.size()
              << " texture groups from map" << std::endl;

    return meshes;
}
```

### Design Decisions

**Grouping by texture** is the most important optimisation. Without it, you would issue one draw call per brush face --- potentially hundreds or thousands per frame. By batching all faces that share a texture into one mesh, you reduce draw calls dramatically. A typical room might have 3-5 texture groups (floor, walls, ceiling, trim, accent) instead of 100+ individual faces.

**Fan triangulation** is safe because all brush faces are convex polygons (brushes are convex by definition). A concave polygon would need ear-clipping or Delaunay triangulation, but we never encounter those with brushes.

**Texture loading** happens inside `buildMapMeshes`. When a face references `wall_grey`, we try to load `assets/textures/wall_grey.png`. If the texture is already cached in the `ResourceManager`, it returns the cached version. If the file does not exist, the texture pointer will be null and we fall back to default UV scaling.

---

## Step 8: Rendering Map Meshes

With the meshes built, we need to create ECS entities so the existing `renderSystem` draws them.

Each `MapMesh` becomes an entity with a `Position` at the origin and a `MeshRenderer`. Since the vertex positions from the `.map` file are already in world space, no transform is needed.

This code will live in the scene setup. Here is a helper function you can add to a new file or directly into your scene setup:

### New file: `src/engine/level/map_loader_helpers.h`

```cpp
#pragma once

#include "engine/level/map_parser.h"
#include "engine/level/map_renderer.h"
#include "engine/core/resource_manager.h"

#include <entt/entt.hpp>
#include <string>
#include <vector>

// Load a .map file, build meshes, and create ECS entities.
// Returns the list of MapMesh objects (caller may need them for collision later).
std::vector<MapMesh> loadAndSpawnMap(
    const std::string& mapPath,
    entt::registry& registry,
    ResourceManager& resources,
    unsigned int shaderId,
    const std::string& textureBasePath = "assets/textures/"
);
```

### New file: `src/engine/level/map_loader_helpers.cpp`

```cpp
#include "engine/level/map_loader_helpers.h"
#include "engine/ecs/components.h"

#include <iostream>

std::vector<MapMesh> loadAndSpawnMap(
    const std::string& mapPath,
    entt::registry& registry,
    ResourceManager& resources,
    unsigned int shaderId,
    const std::string& textureBasePath)
{
    // Step 1: Parse the .map file
    MapFile mapFile = parseMapFile(mapPath);

    // Step 2: Build renderable meshes (grouped by texture)
    std::vector<MapMesh> meshes = buildMapMeshes(
        mapFile, resources, shaderId, textureBasePath);

    // Step 3: Create ECS entities for each texture group
    for (const auto& mapMesh : meshes)
    {
        if (!mapMesh.mesh) continue;

        auto entity = registry.create();
        registry.emplace<Position>(entity, glm::vec3(0.0f));
        registry.emplace<MeshRenderer>(
            entity,
            mapMesh.mesh->getVAO(),
            0u,                              // vertexCount (unused with indices)
            mapMesh.shaderId,
            mapMesh.textureId,
            true,                            // useIndices
            mapMesh.mesh->getIndexCount()
        );
    }

    // Step 4: Spawn point entities (lights, player start)
    for (const auto& ent : mapFile.entities)
    {
        auto classIt = ent.properties.find("classname");
        if (classIt == ent.properties.end()) continue;

        const std::string& classname = classIt->second;

        // Parse origin if present
        glm::vec3 origin(0.0f);
        auto originIt = ent.properties.find("origin");
        if (originIt != ent.properties.end())
        {
            std::istringstream iss(originIt->second);
            iss >> origin.x >> origin.y >> origin.z;
        }

        if (classname == "light")
        {
            // Spawn a point light entity
            auto lightEntity = registry.create();
            registry.emplace<Position>(lightEntity, origin);

            // Parse light intensity (defaults to 300)
            float intensity = 300.0f;
            auto lightIt = ent.properties.find("light");
            if (lightIt != ent.properties.end())
            {
                intensity = std::stof(lightIt->second);
            }

            // Parse colour (defaults to white)
            glm::vec3 colour(1.0f);
            auto colourIt = ent.properties.find("color");
            if (colourIt != ent.properties.end())
            {
                std::istringstream iss(colourIt->second);
                iss >> colour.x >> colour.y >> colour.z;
                // Normalise from 0-255 to 0-1 if values are large
                if (colour.x > 1.0f || colour.y > 1.0f || colour.z > 1.0f)
                {
                    colour /= 255.0f;
                }
            }

            // Convert intensity to attenuation values.
            // Higher intensity = wider radius. These are approximate
            // mappings that look reasonable for indoor scenes.
            float linear = 200.0f / intensity;
            float quadratic = 400.0f / (intensity * intensity) * 50.0f;

            registry.emplace<PointLight>(
                lightEntity,
                colour * (intensity / 100.0f),   // scale colour by intensity
                0.05f,                            // ambient
                linear,
                quadratic
            );

            std::cout << "  Spawned light at ("
                      << origin.x << ", " << origin.y << ", "
                      << origin.z << ") intensity=" << intensity
                      << std::endl;
        }
        // info_player_start is handled by the caller (scene_setup)
        // to set up the player entity with all its components.
        // We just log it here.
        else if (classname == "info_player_start")
        {
            std::cout << "  Player start at ("
                      << origin.x << ", " << origin.y << ", "
                      << origin.z << ")" << std::endl;
        }
    }

    return meshes;
}
```

### Why Separate Files?

The map loading pipeline has three clear responsibilities:
1. **Parsing** (`map_parser`) --- text to data structures
2. **Mesh building** (`map_renderer`) --- data structures to GPU buffers
3. **Entity spawning** (`map_loader_helpers`) --- GPU buffers to ECS entities

Keeping them separate means you can re-parse without rebuilding meshes, or rebuild meshes without re-spawning entities. This matters later when you add hot-reloading or when brush entities (doors, lifts) need their own separate meshes.

---

## Step 9: Loading a Test Map

### Test map file: `assets/maps/test.map`

Create this file in your assets directory. It defines a simple room made of 6 brushes (floor, ceiling, 4 walls) plus a light and a player spawn point.

The room is 512 units wide, 256 units tall, and 512 units deep. Wall thickness is 16 units. The interior playable space is 480 x 240 x 480.

```
// QEngine test map -- a simple room
// Generated for Chapter 17

// entity 0 -- worldspawn
{
"classname" "worldspawn"

// brush 0 -- floor
{
( -256 -16 -256 ) ( -256 -16 256 ) ( 256 -16 -256 ) floor1 0 0 0 1 1
( 256 0 256 ) ( 256 0 -256 ) ( -256 0 256 ) floor1 0 0 0 1 1
( -256 -16 -256 ) ( 256 -16 -256 ) ( -256 0 -256 ) floor1 0 0 0 1 1
( -256 -16 256 ) ( -256 0 256 ) ( 256 -16 256 ) floor1 0 0 0 1 1
( -256 -16 -256 ) ( -256 0 -256 ) ( -256 -16 256 ) floor1 0 0 0 1 1
( 256 -16 256 ) ( 256 0 256 ) ( 256 -16 -256 ) floor1 0 0 0 1 1
}

// brush 1 -- ceiling
{
( -256 240 -256 ) ( 256 240 -256 ) ( -256 240 256 ) ceiling1 0 0 0 1 1
( 256 256 256 ) ( -256 256 256 ) ( 256 256 -256 ) ceiling1 0 0 0 1 1
( -256 240 -256 ) ( -256 240 256 ) ( -256 256 -256 ) ceiling1 0 0 0 1 1
( 256 240 256 ) ( 256 256 256 ) ( 256 240 -256 ) ceiling1 0 0 0 1 1
( -256 240 -256 ) ( 256 240 -256 ) ( -256 256 -256 ) ceiling1 0 0 0 1 1
( -256 240 256 ) ( 256 240 256 ) ( -256 256 256 ) ceiling1 0 0 0 1 1
}

// brush 2 -- north wall (z = -256 side)
{
( -256 0 -272 ) ( 256 0 -272 ) ( -256 240 -272 ) wall1 0 0 0 1 1
( 256 0 -256 ) ( 256 240 -256 ) ( -256 0 -256 ) wall1 0 0 0 1 1
( -256 0 -272 ) ( -256 240 -272 ) ( -256 0 -256 ) wall1 0 0 0 1 1
( 256 0 -256 ) ( 256 0 -272 ) ( 256 240 -256 ) wall1 0 0 0 1 1
( -256 0 -272 ) ( -256 0 -256 ) ( 256 0 -272 ) wall1 0 0 0 1 1
( -256 240 -256 ) ( -256 240 -272 ) ( 256 240 -256 ) wall1 0 0 0 1 1
}

// brush 3 -- south wall (z = 256 side)
{
( -256 0 256 ) ( -256 240 256 ) ( 256 0 256 ) wall1 0 0 0 1 1
( 256 0 272 ) ( 256 240 272 ) ( -256 0 272 ) wall1 0 0 0 1 1
( -256 0 256 ) ( -256 0 272 ) ( -256 240 256 ) wall1 0 0 0 1 1
( 256 0 272 ) ( 256 0 256 ) ( 256 240 272 ) wall1 0 0 0 1 1
( -256 0 256 ) ( 256 0 256 ) ( -256 0 272 ) wall1 0 0 0 1 1
( -256 240 272 ) ( -256 240 256 ) ( 256 240 272 ) wall1 0 0 0 1 1
}

// brush 4 -- west wall (x = -256 side)
{
( -272 0 -256 ) ( -272 240 -256 ) ( -272 0 256 ) wall1 0 0 0 1 1
( -256 0 256 ) ( -256 0 -256 ) ( -256 240 256 ) wall1 0 0 0 1 1
( -272 0 -256 ) ( -256 0 -256 ) ( -272 240 -256 ) wall1 0 0 0 1 1
( -272 0 256 ) ( -272 240 256 ) ( -256 0 256 ) wall1 0 0 0 1 1
( -272 0 -256 ) ( -272 0 256 ) ( -256 0 -256 ) wall1 0 0 0 1 1
( -272 240 256 ) ( -272 240 -256 ) ( -256 240 256 ) wall1 0 0 0 1 1
}

// brush 5 -- east wall (x = 256 side)
{
( 256 0 -256 ) ( 256 0 256 ) ( 256 240 -256 ) wall1 0 0 0 1 1
( 272 0 256 ) ( 272 240 256 ) ( 272 0 -256 ) wall1 0 0 0 1 1
( 256 0 -256 ) ( 256 240 -256 ) ( 272 0 -256 ) wall1 0 0 0 1 1
( 256 0 256 ) ( 272 0 256 ) ( 256 240 256 ) wall1 0 0 0 1 1
( 256 0 -256 ) ( 272 0 -256 ) ( 256 0 256 ) wall1 0 0 0 1 1
( 256 240 256 ) ( 272 240 256 ) ( 256 240 -256 ) wall1 0 0 0 1 1
}
}

// entity 1 -- ceiling light (centre of room)
{
"classname" "light"
"origin" "0 200 0"
"light" "400"
}

// entity 2 -- fill light (front-left)
{
"classname" "light"
"origin" "-128 160 128"
"light" "200"
}

// entity 3 -- fill light (back-right)
{
"classname" "light"
"origin" "128 160 -128"
"light" "200"
}

// entity 4 -- player spawn
{
"classname" "info_player_start"
"origin" "0 32 0"
"angle" "0"
}
```

### Understanding the Brush Geometry

Each brush is a box. Take the floor brush as an example:

```
    Top-down cross section of the room:

    x = -272                x = 272
        ┌────────────────────────┐  <- outer wall edge
        │ ┌────────────────────┐ │
        │ │                    │ │  <- inner wall face
        │ │    playable area   │ │     (480 x 480 units)
        │ │    480 x 480       │ │
        │ │                    │ │
        │ └────────────────────┘ │
        └────────────────────────┘
    z = -272                z = 272
```

The floor is a slab from y=-16 to y=0. The ceiling is a slab from y=240 to y=256. The walls are 16 units thick. The interior space runs from (-256, 0, -256) to (256, 240, 256).

### Update `scene_setup.cpp`

Replace the `createShowcaseLevel()` call with the map loader. The rest of the scene setup (player, demos, triggers) remains for now --- Chapter 18 will move those into the `.map` file.

At the top of `scene_setup.cpp`, add the new include:

```cpp
#include "engine/level/map_loader_helpers.h"
```

Then replace the level creation and sector entity spawning block:

```cpp
// ─── OLD: Create the showcase level ──────────────────────────────
//  Level level = createShowcaseLevel();
//
//  for (const auto& sector : level.sectors)
//  {
//      if (!sector.mesh) continue;
//      auto sectorEntity = registry.create();
//      registry.emplace<Position>(sectorEntity, glm::vec3(0.0f));
//      registry.emplace<MeshRenderer>(
//          sectorEntity,
//          sector.mesh->getVAO(), 0u,
//          litShader->getId(), gridGrey->getId(),
//          true, sector.mesh->getIndexCount()
//      );
//  }

// ─── NEW: Load a .map file ──────────────────────────────────────
auto mapMeshes = loadAndSpawnMap(
    "assets/maps/test.map",
    registry, const_cast<ResourceManager&>(resources),
    litShader->getId()
);
```

You will also need to create a `Level` object for systems that still depend on it (like `combatSystem`). For now, create an empty level:

```cpp
Level level;  // Empty -- no sectors. Combat raycasting against
              // level geometry will be updated in Chapter 19
              // when we add brush collision.
```

The full updated `setupScene` function signature stays the same. It still returns a `Level` --- but now it is an empty placeholder. The actual geometry comes from the `.map` file.

### Update Player Spawn Position

The player start in the `.map` file is at `(0, 32, 0)`. Update the player entity creation to match:

```cpp
registry.emplace<Position>(player, glm::vec3(0.0f, 32.0f, 0.0f));
```

Also update the camera starting position in `main.cpp`:

```cpp
Camera camera(glm::vec3(0.0f, 32.0f, 0.0f));
```

### Create Texture Files

The test map references two textures: `floor1` and `wall1`. You need image files for these:

```
assets/textures/floor1.png
assets/textures/wall1.png
```

Any 64x64 or 128x128 texture will work. If you do not have textures ready, you can temporarily use copies of the existing `grid_grey.png`:

```
copy assets/textures/grid_grey.png assets/textures/floor1.png
copy assets/textures/grid_grey.png assets/textures/wall1.png
```

The map renderer will find these by name and apply them to the correct faces.

---

## Step 10: Update CMakeLists.txt

Add the three new source files to the executable:

```cmake
add_executable(QEngine
    src/main.cpp
    src/engine/core/input_manager.cpp
    src/engine/core/resource_manager.cpp
    src/engine/core/window.cpp
    src/engine/ecs/jolt_body_helpers.cpp
    src/engine/ecs/scene_setup.cpp
    src/engine/ecs/showcase_level.cpp
    src/engine/ecs/systems/combat_system.cpp
    src/engine/ecs/systems/debug_hud_system.cpp
    src/engine/ecs/systems/demo_reset_system.cpp
    src/engine/ecs/systems/lifetime_system.cpp
    src/engine/ecs/systems/jolt_sync_system.cpp
    src/engine/ecs/systems/player_character_system.cpp
    src/engine/ecs/systems/mover_sync_system.cpp
    src/engine/ecs/systems/mover_system.cpp
    src/engine/ecs/systems/render_system.cpp
    src/engine/ecs/systems/trigger_system.cpp
    src/engine/level/level_loader.cpp
    src/engine/level/map_parser.cpp
    src/engine/level/map_renderer.cpp
    src/engine/level/map_loader_helpers.cpp
    src/engine/physics/jolt_world.cpp
    src/engine/physics/raycast.cpp
    src/engine/renderer/camera.cpp
    src/engine/renderer/mesh.cpp
    src/engine/renderer/obj_loader.cpp
    src/engine/renderer/shader.cpp
    src/engine/renderer/stb_image_impl.cpp
    src/engine/renderer/texture.cpp
)
```

The old `level_loader.cpp` and `showcase_level.cpp` remain in the build. They are still compilable and can be used as a fallback if you want to switch between the old and new level systems during development.

---

## What Changed --- Summary

| File | Change |
|------|--------|
| `src/engine/level/map_parser.h` | **New** --- MapPlane, MapBrushFace, MapBrush, MapEntity, MapFile structs; parse and geometry function declarations |
| `src/engine/level/map_parser.cpp` | **New** --- .map text parser, plane-to-polygon clipping algorithm, UV computation |
| `src/engine/level/map_renderer.h` | **New** --- MapMesh struct and buildMapMeshes declaration |
| `src/engine/level/map_renderer.cpp` | **New** --- face triangulation, texture grouping, GPU mesh construction |
| `src/engine/level/map_loader_helpers.h` | **New** --- loadAndSpawnMap declaration |
| `src/engine/level/map_loader_helpers.cpp` | **New** --- orchestrates parse + build + entity spawning, handles lights and player start |
| `assets/maps/test.map` | **New** --- test room with 6 brushes, 3 lights, 1 player spawn |
| `src/engine/ecs/scene_setup.cpp` | **Modified** --- replaced createShowcaseLevel with loadAndSpawnMap; Level is now an empty placeholder |
| `src/main.cpp` | **Modified** --- updated camera starting position |
| `CMakeLists.txt` | **Modified** --- added 3 new source files |

### Files kept but no longer primary

| File | Status |
|------|--------|
| `src/engine/level/level.h` | Kept --- Level/Sector/Surface structs still used by some systems |
| `src/engine/level/level_loader.h/.cpp` | Kept --- old sector-based loader, not called by default |
| `src/engine/ecs/showcase_level.h/.cpp` | Kept --- old hardcoded room, not called by default |

---

## What You Should See

After building and running:

1. **A large room rendered from the .map file** --- floor, ceiling, and four walls visible. The room is 512x240x512 units, much larger than the old 30x6x30 showcase room.
2. **Textured surfaces** --- each face has the texture specified in the `.map` file (`floor1` on the floor, `wall1` on walls and ceiling). UV mapping follows the texture alignment parameters.
3. **Point lights illuminate the room** --- the three lights from the `.map` file create bright spots on the ceiling and walls. The lighting system works exactly as before; only the geometry source changed.
4. **The player can look around** --- camera and mouselook work as before. The player spawns at the `info_player_start` position.
5. **No collision yet** --- the player will fall through the floor. This is expected. Brush collision is implemented in Chapter 19.

If the room appears inside-out (you can see the outside but not the inside), check that your plane normals point outward from the brush. The clipping algorithm expects outward-facing normals.

If textures appear stretched or misaligned, verify that your texture files exist at the paths the map references (`assets/textures/floor1.png`, `assets/textures/wall1.png`) and that their dimensions are powers of two (64, 128, 256, etc.).

---

## What's Next

In **Chapter 18: Entity Mapping & FGD**, we make TrenchBroom aware of QEngine's entity types. You will:
- Create a `.fgd` (Forge Game Data) file that tells TrenchBroom about `info_player_start`, `light`, `func_door`, `trigger_once`, and other entity classes
- Build an entity factory system that reads each map entity's `classname` and spawns the corresponding ECS entity with the correct components
- Move the player, lights, doors, triggers, and demo objects from hardcoded `scene_setup.cpp` code into the `.map` file --- authored entirely in TrenchBroom
