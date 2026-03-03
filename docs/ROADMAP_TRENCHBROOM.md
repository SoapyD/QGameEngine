# QEngine — TrenchBroom Integration Roadmap

A phased plan to go from QEngine's sector/portal level format to full TrenchBroom-authored levels. Each phase builds on the last, and each phase produces a working result.

---

## Background: How TrenchBroom Works

TrenchBroom is a free, open-source level editor originally made for Quake mapping. It lets you:

- Build rooms out of **brushes** — convex 3D shapes defined by intersecting planes
- Apply textures to brush faces with alignment, scale, and rotation controls
- Place **entities** — point entities (lights, spawn points, items) and brush entities (doors, platforms, triggers)
- Export to the **`.map` format** — a plain-text file describing everything

The `.map` format is the bridge between TrenchBroom and your engine. TrenchBroom doesn't care what engine loads the map — it just writes the file. Your engine's job is to parse it and turn it into renderable geometry, collision, and game objects.

---

## TrenchBroom Basics Tutorial

### Installation
1. Download TrenchBroom from https://trenchbroom.github.io/
2. Install and launch it
3. When prompted, select **Generic** as the game type (we'll set up a QEngine profile later)

### Core Concepts

**Brushes** are the building blocks. Everything solid is a brush:
```
┌─────────────────┐
│                 │  ← This room is made of 6 brushes:
│   ┌─────────┐   │     4 walls, 1 floor, 1 ceiling
│   │  empty  │   │
│   │  space  │   │     The "room" is the empty space
│   │         │   │     between the brushes (subtractive)
│   └─────────┘   │
│                 │
└─────────────────┘
```

Quake-style mapping is **subtractive** — you carve empty space out of an infinite solid. Each brush is a solid block. A room is the void between blocks.

### Your First Room
1. **Create a box brush**: Click and drag in the 2D viewport to create a large box (e.g. 512 x 512 x 256 units)
2. **Hollow it**: Select the box, then Edit > Make Hollow (thickness 32). This creates 6 thin brushes forming walls/floor/ceiling
3. **Texture it**: Select a face, choose a texture from the browser, apply
4. **Add a light**: Right-click in the 3D view > Create Point Entity > light
5. **Add a spawn point**: Right-click > Create Point Entity > info_player_start
6. **Save**: File > Save As > `test.map`

### Controls Quick Reference
| Action | Key |
|--------|-----|
| Move camera | WASD + right-click drag |
| Create brush | Left-click drag in 2D view |
| Select | Left-click |
| Move selection | Left-click drag on selection |
| Resize brush | Drag face handles |
| Rotate | R key + drag |
| Apply texture | Select face, click texture in browser |
| Clone brush | Ctrl+D |
| Vertex editing | V key |

### The .map File Format
When you save, TrenchBroom writes a `.map` file. Here's what it looks like:

```
// entity 0 — worldspawn (the level geometry itself)
{
"classname" "worldspawn"
// brush 0
{
( -64 -64 -16 ) ( -64 -63 -16 ) ( -64 -64 -15 ) floor1 0 0 0 1 1
( 64 -64 -16 ) ( 64 -64 -15 ) ( 64 -63 -16 ) floor1 0 0 0 1 1
( -64 -64 -16 ) ( -64 -64 -15 ) ( -63 -64 -16 ) floor1 0 0 0 1 1
( -64 64 -16 ) ( -63 64 -16 ) ( -64 64 -15 ) floor1 0 0 0 1 1
( -64 -64 -16 ) ( -63 -64 -16 ) ( -64 -63 -16 ) floor1 0 0 0 1 1
( -64 -64 16 ) ( -64 -63 16 ) ( -63 -64 16 ) floor1 0 0 0 1 1
}
}
// entity 1 — a light
{
"classname" "light"
"origin" "0 0 128"
"light" "200"
}
// entity 2 — player spawn
{
"classname" "info_player_start"
"origin" "0 0 32"
"angle" "90"
}
```

Each brush is defined by **planes** — three points per plane, plus texture info. A brush is the convex intersection of all its planes. To turn this into triangles, you compute plane-plane-plane intersections to find vertices.

---

## Phase 1: Basic .map Loader (Replace Chapter 8)

**Goal**: Load a TrenchBroom `.map` file and render the brushes as textured geometry.

### Steps

| Step | Task | Details |
|------|------|---------|
| 1.1 | Parse the `.map` format | Read entities, brushes, and planes from text. Each plane is 3 points + texture name + UV params |
| 1.2 | Convert planes to polygons | For each brush: intersect all plane combinations to find vertices, then build a polygon per face by collecting vertices that lie on that plane |
| 1.3 | Triangulate polygons | Fan triangulation (convex polygons only — brushes guarantee this) |
| 1.4 | Calculate normals | Each face's normal comes directly from the plane equation |
| 1.5 | Calculate UVs | Use the texture axis and offset/scale from the `.map` file |
| 1.6 | Build a Mesh | Pack vertices into a VBO/VAO, group by texture for efficient rendering |
| 1.7 | Load textures | Map texture names to image files in an `assets/textures/` directory |

### The Plane-to-Polygon Algorithm

This is the core maths of the loader. For a brush with N planes:

```
for each face plane P:
    polygon = infinite plane P
    for each other plane Q (Q != P):
        clip polygon by Q  (keep the side that's inside the brush)
    if polygon has >= 3 vertices:
        emit polygon as a face
```

In practice, you compute all triple-plane intersections first:
```cpp
// For planes i, j, k: solve the 3x3 system to find intersection point
// Keep the point only if it's on the inside of ALL other planes
glm::vec3 intersect(const Plane& a, const Plane& b, const Plane& c) {
    // Cramer's rule or matrix inverse
    glm::mat3 M(a.normal, b.normal, c.normal);
    float det = glm::determinant(M);
    if (std::abs(det) < 0.001f) return {}; // parallel planes

    glm::vec3 point = (glm::cross(b.normal, c.normal) * a.dist +
                        glm::cross(c.normal, a.normal) * b.dist +
                        glm::cross(a.normal, b.normal) * c.dist) / det;
    return point;
}
```

### What This Replaces
- Chapter 8's `Sector`, `Surface`, `Portal` structs → replaced by `Brush`, `BrushFace`, `MapEntity`
- Chapter 8's `LevelLoader` → replaced by `MapLoader`
- The render system draws brush meshes instead of sector surfaces
- Portals and PVS are dropped (frustum culling is sufficient for small-medium levels)

### What Stays the Same
- Chapters 9-11 (collision, physics, triggers) — AABB and spatial hashing work on brush geometry too
- You generate collision AABBs from brush bounding boxes
- Trigger volumes become brush entities with `classname "trigger_*"`

---

## Phase 2: Entity Mapping (Adapt Chapters 11-14)

**Goal**: Map TrenchBroom entities to QEngine ECS entities.

### Entity Definition File (FGD)

TrenchBroom uses `.fgd` files to know what entities are available. You create one for QEngine:

```
// QEngine.fgd — entity definitions for TrenchBroom

@PointClass base(Origin) size(-16 -16 -24, 16 16 32) color(0 255 0) = info_player_start : "Player spawn point"
[
    angle(integer) : "Facing angle" : 0
]

@PointClass base(Origin) size(-8 -8 -8, 8 8 8) color(255 255 0) = light : "Point light"
[
    light(integer) : "Brightness" : 200
    color(color255) : "Color" : "255 255 255"
]

@PointClass base(Origin) size(-16 -16 0, 16 16 32) color(0 255 255) = item_health : "Health pickup"
[
    amount(integer) : "Health amount" : 25
]

@PointClass base(Origin) size(-16 -16 0, 16 16 32) color(255 128 0) = item_ammo_shells : "Shell ammo"
[
    amount(integer) : "Ammo amount" : 20
]

@PointClass base(Origin) size(-16 -16 0, 16 16 64) color(255 0 0) = monster_grunt : "Grunt enemy"
[
    angle(integer) : "Facing angle" : 0
]

@SolidClass color(128 0 128) = func_door : "Sliding door"
[
    speed(integer) : "Movement speed" : 100
    wait(integer) : "Wait time (seconds)" : 3
    lip(integer) : "Lip (overshoot)" : 8
    angle(integer) : "Move direction" : 0
]

@SolidClass color(128 128 0) = trigger_once : "Trigger (fires once)"
[
    target(target_destination) : "Target entity"
]

@SolidClass color(128 128 0) = trigger_multiple : "Trigger (repeatable)"
[
    target(target_destination) : "Target entity"
    wait(integer) : "Reset time" : 1
]
```

### Entity Spawning

When loading the `.map`, each entity's `classname` maps to an ECS factory function:

```cpp
void spawnEntity(entt::registry& registry, const MapEntity& mapEntity) {
    const auto& classname = mapEntity.properties.at("classname");

    if (classname == "info_player_start") {
        spawnPlayer(registry, mapEntity);
    } else if (classname == "light") {
        spawnLight(registry, mapEntity);
    } else if (classname == "item_health") {
        spawnHealthPickup(registry, mapEntity);
    } else if (classname == "monster_grunt") {
        spawnGrunt(registry, mapEntity);
    } else if (classname == "func_door") {
        spawnDoor(registry, mapEntity);  // brush entity — has geometry
    } else if (classname.starts_with("trigger_")) {
        spawnTrigger(registry, mapEntity);  // brush entity — invisible
    }
}
```

### Brush Entities vs Point Entities
- **Point entities** (lights, spawns, items, enemies) have an `origin` but no geometry — they place ECS entities with components
- **Brush entities** (doors, platforms, triggers) have geometry — their brushes become renderable meshes AND collision volumes that can move

This maps directly to existing QEngine systems:
- `func_door` → `Mover` component from Chapter 11
- `trigger_once` → `TriggerVolume` component from Chapter 11
- `monster_grunt` → enemy factory from Chapter 14
- `item_health` → pickup factory from Chapter 13

---

## Phase 3: Collision from Brushes (Adapt Chapter 9)

**Goal**: Generate collision geometry from brush data instead of sector walls.

### Approach

Each brush is already a convex shape. For collision:

1. **AABB per brush**: compute the axis-aligned bounding box from all brush vertices — drop into the spatial hash (Ch 9)
2. **Detailed collision**: for precise swept-AABB tests, test against each brush face plane
3. **Brush entity collision**: doors and lifts have their own brush collision that moves with them

```cpp
struct BrushCollider {
    std::vector<Plane> planes;   // The defining planes (for detailed tests)
    AABB bounds;                 // For broad-phase spatial hash
};
```

### Swept-AABB vs Brush

The existing Minkowski-difference swept collision from Chapter 9 works against AABBs. For brush collision, you extend it:

```
For each brush near the player:
    For each face plane of the brush:
        Expand the plane outward by the player's half-extents (Minkowski)
        Find the nearest intersection along the movement vector
        If hit: clip movement, apply slide along the plane
```

This is the same algorithm Quake uses (clip against planes, slide along surfaces).

---

## Phase 4: TrenchBroom Game Configuration

**Goal**: Set up TrenchBroom to work seamlessly with QEngine.

### Game Configuration File

Create a TrenchBroom game config so it knows about QEngine:

**Directory structure for TrenchBroom integration:**
```
QEngine/
├── tb/
│   ├── GameConfig.cfg          ← TrenchBroom game config
│   ├── QEngine.fgd             ← Entity definitions
│   └── CompilationProfiles.cfg ← Optional: auto-compile maps
├── assets/
│   ├── textures/               ← TrenchBroom reads textures from here
│   │   ├── base/
│   │   │   ├── floor1.png
│   │   │   ├── wall1.png
│   │   │   └── ...
│   │   └── tech/
│   │       ├── metal1.png
│   │       └── ...
│   └── maps/
│       ├── e1m1.map            ← TrenchBroom source files
│       └── ...
```

### GameConfig.cfg
```json
{
    "version": 9,
    "name": "QEngine",
    "icon": "Icon.png",
    "fileformats": [
        { "format": "Standard" }
    ],
    "filesystem": {
        "searchpath": "assets"
    },
    "textures": {
        "root": "textures",
        "extensions": [ ".png", ".jpg", ".tga" ],
        "attribute": "_tb_textures"
    },
    "entities": {
        "definitions": [ "QEngine.fgd" ],
        "defaultcolor": "0.6 0.6 0.6 1.0"
    },
    "tags": {
        "brush": [
            {
                "name": "Trigger",
                "attribs": [ "transparent" ],
                "match": "classname",
                "pattern": "trigger_*"
            }
        ]
    }
}
```

### Installation
1. Copy the `tb/` folder contents to TrenchBroom's game directory
2. In TrenchBroom: File > Preferences > add QEngine game path pointing to QEngine project root
3. New maps will now show QEngine entities and textures

---

## Phase 5: Advanced Features

These are optional enhancements once the core pipeline works.

### 5.1 Lightmap Baking
Instead of per-vertex lighting, bake lightmaps from brush face data:
- Raycast from each lightmap texel to each light source
- Store results in a lightmap atlas texture
- Chapter 7's lighting concepts apply, just pre-computed instead of real-time

### 5.2 Detail Brushes
Mark some brushes as "detail" (non-structural):
- They render but don't affect collision
- Useful for decorative elements (pillars, trim, rubble)

### 5.3 Compile Step (Optional)
Add a map compilation step similar to Quake's tool chain:
- `qbsp` equivalent: convert brushes to a polygon soup, merge coplanar faces
- `vis` equivalent: compute PVS (potentially visible set) for occlusion culling
- `light` equivalent: bake lightmaps

This is complex but the map still works without it — just less optimised.

---

## Migration Summary

### What Changes from the Original Tutorials

| Original Chapter | Change Required |
|-----------------|----------------|
| Ch 8 (Level Geometry & BSP) | **Replaced** — sector/portal format replaced by .map brush loader |
| Ch 9 (Collision) | **Adapted** — collision against brush planes instead of sector walls. AABB and spatial hash remain |
| Ch 10 (Physics) | **Minimal** — movement code stays the same, just collides against brushes |
| Ch 11 (Triggers/Doors) | **Adapted** — trigger volumes and movers now come from brush entities in the .map file |
| Ch 7 (Lighting) | **Optional** — can enhance with lightmap baking, but real-time lighting still works |
| All other chapters | **No change** — weapons, items, AI, audio, networking, particles all work as-is |

### What You Gain
- Full 3D level design (rooms over rooms, arbitrary geometry)
- A professional-quality editor with undo, vertex editing, texture alignment
- A community of mappers who already know the tooling
- Focus on game code, not tool code

---

## Phased Implementation Order

```
Ch 16: Gameplay polish (crosshair, HUD, death/respawn, damage feedback)
    → Core gameplay loop is complete

Ch 17: .map parser + brush-to-mesh conversion  (Phase 1)
    → You can load and render a TrenchBroom map

Ch 18: Entity mapping + FGD file  (Phase 2)
    → Enemies, items, lights spawn from the map file

Ch 19: Brush collision  (Phase 3)
    → Player collides with brush geometry

Ch 20: TrenchBroom config + final level  (Phase 4)
    → TrenchBroom shows QEngine textures and entities natively
    → Complete playable level authored in TrenchBroom
```

Each chapter is self-contained. After Ch 19, you have a fully playable game with TrenchBroom-authored levels. Ch 20 is quality-of-life and the final integration level. Phase 5 (lightmaps, detail brushes, compilation) is optional future work.
