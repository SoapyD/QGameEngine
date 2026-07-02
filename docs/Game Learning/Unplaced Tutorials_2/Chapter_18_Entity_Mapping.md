# Chapter 18: Entity Mapping & FGD

## What You'll Learn
- The difference between point entities and brush entities in TrenchBroom maps
- How to write an FGD file so TrenchBroom knows what entities your engine supports
- Building helper functions to safely read entity properties from the map file
- Writing spawn functions that turn map entities into ECS entities with the correct components
- Converting brush entity geometry into renderable meshes at load time
- Replacing the hardcoded `setupScene` with a data-driven entity pipeline

---

## Prerequisites

Chapter 17 added a `.map` parser that produces these structures:

```cpp
struct MapBrush {
    std::vector<BrushFace> faces;
};

struct MapEntity {
    std::map<std::string, std::string> properties;
    std::vector<MapBrush> brushes;  // Empty for point entities
};

struct MapFile {
    std::vector<MapEntity> entities;
};
```

The parser also built renderable meshes from the worldspawn entity (entity 0). This chapter handles everything else -- all the non-worldspawn entities that define gameplay objects.

---

## Step 1: Understanding Entity Types

Every entity in a `.map` file falls into one of two categories.

### Point Entities

Point entities have an `"origin"` property but no brushes. They represent things that exist at a single position in the world: spawn points, lights, pickups, enemies.

```
{
"classname" "light"
"origin" "128 256 64"
"light" "200"
"color" "255 200 150"
}
```

The origin is all you need to place a point entity. TrenchBroom displays them as small colored boxes in the editor, and the mapper drags them around freely.

### Brush Entities

Brush entities have one or more brushes that define their shape. Doors, lifts, platforms, and trigger volumes are all brush entities. They have no `"origin"` property -- their position is defined by where their brushes are in the world.

```
{
"classname" "func_door"
"angle" "0"
"speed" "100"
// brush 0
{
( -128 0 0 ) ( -128 1 0 ) ( -128 0 1 ) door1 0 0 0 1 1
( -96 0 0 ) ( -96 0 1 ) ( -96 1 0 ) door1 0 0 0 1 1
...
}
}
```

To work with a brush entity in the ECS, you need to:
1. Build a renderable mesh from its brushes (same algorithm as worldspawn, but per-entity)
2. Compute the entity's origin as the center of its bounding box
3. Offset all vertices to be relative to that origin, so the `Position` component can move the entity

### The Worldspawn

Entity 0 is always `worldspawn` -- the level geometry itself. Chapter 17 already handles this: the parser builds meshes from worldspawn's brushes and creates sector entities with `MeshRenderer` components. The entity spawner in this chapter skips entity 0 and processes everything from entity 1 onward.

---

## Step 2: The FGD File

### What Is an FGD?

An FGD (Forge Game Data) file tells TrenchBroom what entities your engine supports. Without it, TrenchBroom has no idea what a `light` or `func_door` is -- the mapper would have to type classnames by hand, with no visual feedback.

The FGD defines:
- **Entity names** and descriptions
- **Whether it's a point or brush entity** (`@PointClass` vs `@SolidClass`)
- **Size and color** for editor display (point entities only)
- **Properties** the mapper can set, with types and defaults

### Create the FGD

Create the file `assets/QEngine.fgd`:

```
// QEngine.fgd — entity definitions for TrenchBroom

// ─── Point Entities ────────────────────────────────────────

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

@PointClass base(Origin) size(-16 -16 0, 16 16 32) color(255 128 0) = item_ammo_shells : "Shell ammo pickup"
[
    amount(integer) : "Ammo amount" : 20
]

@PointClass base(Origin) size(-16 -16 0, 16 16 64) color(255 0 0) = monster_grunt : "Grunt enemy"
[
    angle(integer) : "Facing angle" : 0
]

// ─── Brush Entities (Solid) ────────────────────────────────

@SolidClass color(128 0 128) = func_door : "Sliding door"
[
    speed(integer) : "Movement speed (units/sec)" : 100
    wait(integer) : "Wait time at open position (seconds)" : 3
    lip(integer) : "Lip — how many units short of full travel" : 8
    angle(integer) : "Move direction (degrees, 0=+X, 90=+Y, -1=up, -2=down)" : 0
    targetname(target_source) : "Name (for targeting by triggers)"
]

@SolidClass color(128 128 64) = func_lift : "Platform / lift"
[
    speed(integer) : "Movement speed (units/sec)" : 64
    wait(integer) : "Wait time at top (seconds)" : 3
    height(integer) : "Vertical travel distance" : 128
    targetname(target_source) : "Name (for targeting by triggers)"
]

@SolidClass color(128 128 0) = trigger_once : "Trigger (fires once)"
[
    target(target_destination) : "Target entity to activate"
]

@SolidClass color(128 128 0) = trigger_multiple : "Trigger (repeatable)"
[
    target(target_destination) : "Target entity to activate"
    wait(integer) : "Reset time (seconds)" : 1
]

@SolidClass color(255 0 0) = trigger_hurt : "Damage zone"
[
    dmg(integer) : "Damage per second" : 10
]

@SolidClass color(0 128 255) = trigger_teleport : "Teleporter"
[
    target(target_destination) : "Destination entity name"
]
```

### Breaking Down the Syntax

Each entry follows the same pattern:

```
@PointClass base(Origin) size(min, max) color(r g b) = classname : "Description"
[
    property(type) : "Display name" : default_value
]
```

| Keyword | Meaning |
|---------|---------|
| `@PointClass` | A point entity -- placed by origin, no geometry |
| `@SolidClass` | A brush entity -- has geometry, no explicit origin |
| `base(Origin)` | Inherits the `origin` property (point entities only) |
| `size(-16 -16 0, 16 16 32)` | Bounding box shown in TrenchBroom's 3D view |
| `color(255 128 0)` | Display color in the editor (0-255 per channel) |
| `target_destination` | TrenchBroom draws a line to the targeted entity |
| `target_source` | This entity can be targeted by others |

The `angle` property on `func_door` uses Quake conventions: `0` means +X, `90` means +Y, `180` means -X, `270` means -Y. The special values `-1` and `-2` mean straight up and straight down. This convention is well-known to TrenchBroom mappers.

---

## Step 3: Entity Property Helpers

Reading properties from `MapEntity` requires parsing strings into numbers and vectors. Rather than scatter `std::stof` and `std::stoi` calls through every spawn function, create a set of helpers.

### New file: `src/engine/level/entity_helpers.h`

```cpp
#pragma once

#include <string>
#include <map>
#include <glm/glm.hpp>

struct MapEntity;  // Forward declaration (defined in map_parser.h)

// Safe property access — returns defaultVal if key is missing
std::string getProperty(
    const MapEntity& entity,
    const std::string& key,
    const std::string& defaultVal = ""
);

float getPropertyFloat(
    const MapEntity& entity,
    const std::string& key,
    float defaultVal = 0.0f
);

int getPropertyInt(
    const MapEntity& entity,
    const std::string& key,
    int defaultVal = 0
);

// Parse "x y z" origin string into a vec3
glm::vec3 getOrigin(const MapEntity& entity);

// Parse any "x y z" property into a vec3
glm::vec3 getPropertyVec3(
    const MapEntity& entity,
    const std::string& key,
    glm::vec3 defaultVal = glm::vec3(0.0f)
);
```

### New file: `src/engine/level/entity_helpers.cpp`

```cpp
#include "engine/level/entity_helpers.h"
#include "engine/level/map_parser.h"  // For MapEntity definition

#include <sstream>
#include <iostream>

std::string getProperty(
    const MapEntity& entity,
    const std::string& key,
    const std::string& defaultVal)
{
    auto it = entity.properties.find(key);
    if (it != entity.properties.end())
    {
        return it->second;
    }
    return defaultVal;
}

float getPropertyFloat(
    const MapEntity& entity,
    const std::string& key,
    float defaultVal)
{
    auto it = entity.properties.find(key);
    if (it != entity.properties.end())
    {
        try
        {
            return std::stof(it->second);
        }
        catch (const std::exception& e)
        {
            std::cerr << "Warning: could not parse float property '"
                      << key << "' = '" << it->second << "'" << std::endl;
        }
    }
    return defaultVal;
}

int getPropertyInt(
    const MapEntity& entity,
    const std::string& key,
    int defaultVal)
{
    auto it = entity.properties.find(key);
    if (it != entity.properties.end())
    {
        try
        {
            return std::stoi(it->second);
        }
        catch (const std::exception& e)
        {
            std::cerr << "Warning: could not parse int property '"
                      << key << "' = '" << it->second << "'" << std::endl;
        }
    }
    return defaultVal;
}

glm::vec3 getOrigin(const MapEntity& entity)
{
    return getPropertyVec3(entity, "origin", glm::vec3(0.0f));
}

glm::vec3 getPropertyVec3(
    const MapEntity& entity,
    const std::string& key,
    glm::vec3 defaultVal)
{
    auto it = entity.properties.find(key);
    if (it != entity.properties.end())
    {
        std::istringstream iss(it->second);
        glm::vec3 result;
        if (iss >> result.x >> result.y >> result.z)
        {
            return result;
        }
        std::cerr << "Warning: could not parse vec3 property '"
                  << key << "' = '" << it->second << "'" << std::endl;
    }
    return defaultVal;
}
```

### Why Helpers Instead of Inline Parsing?

Every spawn function needs to read properties. Without helpers, you would repeat the same `find` + `try/catch` + `stof` pattern dozens of times. The helpers also handle missing keys gracefully -- a mapper who forgets to set a property gets the default value instead of a crash.

The `getPropertyVec3` function is particularly useful because TrenchBroom stores colors and origins as space-separated strings like `"255 200 150"` or `"128 64 32"`. Parsing these inline gets repetitive fast.

---

## Step 4: Entity Spawning Functions

This is the core of the chapter. Each `classname` in the map file maps to a spawn function that creates an ECS entity with the right components.

### New file: `src/engine/level/entity_spawner.h`

```cpp
#pragma once

#include <entt/entt.hpp>
#include "engine/core/resource_manager.h"
#include "engine/level/map_parser.h"

// Spawn all non-worldspawn entities from a parsed .map file.
// Call this after buildMapMeshes (Ch 17) has handled entity 0.
void spawnEntities(
    entt::registry& registry,
    const MapFile& mapFile,
    const ResourceManager& resources
);
```

### New file: `src/engine/level/entity_spawner.cpp`

This is a long file. We will build it up one spawn function at a time.

#### The Dispatcher

```cpp
#include "engine/level/entity_spawner.h"
#include "engine/level/entity_helpers.h"
#include "engine/level/brush_mesh_builder.h"  // From Chapter 17
#include "engine/ecs/components.h"
#include "engine/ecs/jolt_body_helpers.h"
#include "engine/ecs/weapon_definitions.h"

#include <iostream>
#include <string>

// ─── Forward declarations ──────────────────────────────────
static void spawnPlayer(entt::registry& reg, const MapEntity& ent,
                        const ResourceManager& res);
static void spawnLight(entt::registry& reg, const MapEntity& ent);
static void spawnHealthPickup(entt::registry& reg, const MapEntity& ent,
                              const ResourceManager& res);
static void spawnAmmoPickup(entt::registry& reg, const MapEntity& ent,
                            const ResourceManager& res);
static void spawnEnemy(entt::registry& reg, const MapEntity& ent,
                       const ResourceManager& res);
static void spawnDoor(entt::registry& reg, const MapEntity& ent,
                      const ResourceManager& res);
static void spawnLift(entt::registry& reg, const MapEntity& ent,
                      const ResourceManager& res);
static void spawnTrigger(entt::registry& reg, const MapEntity& ent);

void spawnEntities(
    entt::registry& registry,
    const MapFile& mapFile,
    const ResourceManager& resources)
{
    // Entity 0 is always worldspawn — already handled by buildMapMeshes
    for (size_t i = 1; i < mapFile.entities.size(); i++)
    {
        const auto& ent = mapFile.entities[i];
        const auto classname = getProperty(ent, "classname");

        if (classname.empty())
        {
            std::cerr << "Warning: entity " << i
                      << " has no classname, skipping" << std::endl;
            continue;
        }

        if (classname == "info_player_start")
            spawnPlayer(registry, ent, resources);
        else if (classname == "light")
            spawnLight(registry, ent);
        else if (classname == "item_health")
            spawnHealthPickup(registry, ent, resources);
        else if (classname == "item_ammo_shells")
            spawnAmmoPickup(registry, ent, resources);
        else if (classname == "monster_grunt")
            spawnEnemy(registry, ent, resources);
        else if (classname == "func_door")
            spawnDoor(registry, ent, resources);
        else if (classname == "func_lift")
            spawnLift(registry, ent, resources);
        else if (classname.starts_with("trigger_"))
            spawnTrigger(registry, ent);
        else
            std::cerr << "Warning: unknown entity classname '"
                      << classname << "' at entity " << i << std::endl;
    }
}
```

The dispatcher is deliberately simple -- a chain of `if/else if` on the classname string. This is the same pattern Quake engines use. It scales fine for dozens of entity types. If you ever have hundreds, you could switch to a `std::unordered_map<std::string, std::function<...>>`, but that is overkill here.

#### spawnPlayer

```cpp
static void spawnPlayer(
    entt::registry& reg,
    const MapEntity& ent,
    const ResourceManager& res)
{
    glm::vec3 origin = getOrigin(ent);
    float angle = getPropertyFloat(ent, "angle", 0.0f);

    auto player = reg.create();
    reg.emplace<Position>(player, origin);
    reg.emplace<Rotation>(player, glm::vec3(0.0f, angle, 0.0f));
    reg.emplace<Velocity>(player);
    reg.emplace<AABBCollider>(player, glm::vec3(0.3f, 0.85f, 0.3f), false);
    reg.emplace<Gravity>(player);
    reg.emplace<OnGround>(player);
    reg.emplace<CharacterPhysics>(player);
    reg.emplace<Health>(player, 100.0f, 100.0f);
    reg.emplace<PlayerInput>(player);
    reg.emplace<TagPlayer>(player);

    // Give the player starting weapons
    WeaponInventory inv;
    inv.weapons.push_back(createWeapon(WeaponType::Shotgun));
    inv.weapons.push_back(createWeapon(WeaponType::RocketLauncher));
    inv.currentWeapon = 0;
    reg.emplace<WeaponInventory>(player, std::move(inv));

    reg.emplace<Ammo>(player, 25, 0, 5, 0);  // 25 shells, 5 rockets

    // Store combat resources in registry context (projectile rendering)
    auto cubeMesh = res.getMesh("cube");
    auto litShader = res.getShader("lit");
    auto gridRed = res.getTexture("grid_red");
    auto gridOrange = res.getTexture("grid_orange");

    if (!reg.ctx().contains<CombatResources>())
    {
        auto& combatRes = reg.ctx().emplace<CombatResources>();
        combatRes.cubeVAO = cubeMesh->getVAO();
        combatRes.cubeIndexCount = cubeMesh->getIndexCount();
        combatRes.shaderId = litShader->getId();
        combatRes.projectileTextureId = gridRed->getId();
        combatRes.tracerTextureId = gridOrange->getId();
    }

    std::cout << "Spawned player at ("
              << origin.x << ", " << origin.y << ", " << origin.z
              << ") facing " << angle << " degrees" << std::endl;
}
```

This is almost identical to the player setup in the old `scene_setup.cpp`, except the position and angle come from the map file instead of being hardcoded. The `CombatResources` context is set up here because the player is the entity that fires weapons -- it makes sense to initialize it alongside the player.

> **Note on Rotation:** The `Rotation` component stores the initial facing angle from the map. The camera system in `main.cpp` currently manages yaw independently via mouse input. A future enhancement could read the initial yaw from the player's `Rotation` to set the camera's starting direction.

#### spawnLight

```cpp
static void spawnLight(
    entt::registry& reg,
    const MapEntity& ent)
{
    glm::vec3 origin = getOrigin(ent);
    int brightness = getPropertyInt(ent, "light", 200);
    glm::vec3 color = getPropertyVec3(ent, "color", glm::vec3(255.0f));

    // TrenchBroom stores color as 0-255, our PointLight uses floats.
    // Convert and scale by brightness.
    // A brightness of 200 with color (255,255,255) produces a bright white light.
    // The brightness value controls the intensity multiplier.
    float intensity = static_cast<float>(brightness) / 100.0f;
    glm::vec3 lightColor = (color / 255.0f) * intensity;

    // Calculate attenuation from brightness.
    // Higher brightness = wider range = lower attenuation coefficients.
    // These formulas approximate Quake-style light falloff.
    float linear = 200.0f / static_cast<float>(brightness) * 0.09f;
    float quadratic = 200.0f / static_cast<float>(brightness) * 0.032f;

    auto light = reg.create();
    reg.emplace<Position>(light, origin);
    reg.emplace<PointLight>(light, lightColor, 0.05f, linear, quadratic);
}
```

TrenchBroom stores light color in 0-255 range and brightness as an integer. The engine's `PointLight` uses floating-point color values where `1.0` is standard intensity. The conversion divides by 255 (normalize color) and scales by `brightness / 100` (so brightness=200 produces a 2x intensity light, matching the existing hardcoded lights in `scene_setup.cpp`).

The attenuation coefficients (`linear` and `quadratic`) control how quickly the light falls off with distance. The default values (`0.09`, `0.032`) from `scene_setup.cpp` work well for a brightness of 200. Scaling them inversely with brightness means brighter lights reach further -- a brightness of 400 has half the attenuation of brightness 200.

#### spawnHealthPickup

```cpp
static void spawnHealthPickup(
    entt::registry& reg,
    const MapEntity& ent,
    const ResourceManager& res)
{
    glm::vec3 origin = getOrigin(ent);
    int amount = getPropertyInt(ent, "amount", 25);

    auto cubeMesh = res.getMesh("cube");
    auto litShader = res.getShader("lit");
    auto gridGreen = res.getTexture("grid_green");

    auto pickup = reg.create();
    reg.emplace<Position>(pickup, origin);
    reg.emplace<Scale>(pickup, glm::vec3(0.5f));
    reg.emplace<MeshRenderer>(
        pickup,
        cubeMesh->getVAO(), 0u,
        litShader->getId(), gridGreen->getId(),
        true, cubeMesh->getIndexCount()
    );

    // Trigger volume: heals the player on overlap
    reg.emplace<AABBCollider>(pickup, glm::vec3(0.5f, 0.5f, 0.5f), true);
    reg.emplace<TriggerVolume>(
        pickup,
        TriggerAction::Heal,
        entt::null,               // no target entity
        glm::vec3(0.0f),          // no destination
        static_cast<float>(amount),
        "",                        // no message
        true,                      // onlyOnce — pickup disappears after use
        false,                     // not yet triggered
        0.0f,                      // no cooldown
        0.0f
    );
}
```

Health pickups are small green cubes with a trigger volume set to `TriggerAction::Heal`. The `onlyOnce` flag is `true` -- once the player walks over it, it triggers and won't trigger again. The amount comes from the FGD's `amount` property, defaulting to 25.

> **Why not destroy the entity on pickup?** The `triggerSystem` from Chapter 11 already supports `onlyOnce` -- it sets `triggered = true` and never fires again. The visual cube remains but the trigger is dead. Destroying the entity and removing the visual is a future polish item (Chapter 16 could add a pickup animation and removal).

#### spawnAmmoPickup

```cpp
static void spawnAmmoPickup(
    entt::registry& reg,
    const MapEntity& ent,
    const ResourceManager& res)
{
    glm::vec3 origin = getOrigin(ent);
    int amount = getPropertyInt(ent, "amount", 20);

    auto cubeMesh = res.getMesh("cube");
    auto litShader = res.getShader("lit");
    auto gridOrange = res.getTexture("grid_orange");

    auto pickup = reg.create();
    reg.emplace<Position>(pickup, origin);
    reg.emplace<Scale>(pickup, glm::vec3(0.5f));
    reg.emplace<MeshRenderer>(
        pickup,
        cubeMesh->getVAO(), 0u,
        litShader->getId(), gridOrange->getId(),
        true, cubeMesh->getIndexCount()
    );

    // Trigger volume: gives ammo on overlap.
    // Using Heal action for now — the trigger system applies value to health.
    // A dedicated AmmoPickup action would be cleaner (future enhancement).
    // For now, we use Message action as a placeholder and handle it in a
    // custom pickup system later.
    reg.emplace<AABBCollider>(pickup, glm::vec3(0.5f, 0.5f, 0.5f), true);
    reg.emplace<TriggerVolume>(
        pickup,
        TriggerAction::Message,    // Placeholder — ammo pickup logic comes later
        entt::null,
        glm::vec3(0.0f),
        static_cast<float>(amount),
        "ammo_shells",             // encode pickup type in message field
        true,                      // onlyOnce
        false,
        0.0f,
        0.0f
    );
}
```

Ammo pickups look like orange cubes. The trigger system does not currently have a dedicated `GiveAmmo` action, so we store the pickup type in the `message` field as a tag. A future chapter can add an `AmmoPickup` trigger action or a dedicated pickup system that reads this tag and modifies the player's `Ammo` component. The framework is in place -- the entity is created, the trigger volume detects overlap, and the data is there to act on.

#### spawnEnemy

```cpp
static void spawnEnemy(
    entt::registry& reg,
    const MapEntity& ent,
    const ResourceManager& res)
{
    glm::vec3 origin = getOrigin(ent);
    float angle = getPropertyFloat(ent, "angle", 0.0f);

    auto cubeMesh = res.getMesh("cube");
    auto litShader = res.getShader("lit");
    auto gridRed = res.getTexture("grid_red");

    auto enemy = reg.create();
    reg.emplace<Position>(enemy, origin);
    reg.emplace<Rotation>(enemy, glm::vec3(0.0f, angle, 0.0f));
    reg.emplace<Scale>(enemy, glm::vec3(0.6f, 1.7f, 0.6f));
    reg.emplace<Health>(enemy, 50.0f, 50.0f);
    reg.emplace<AABBCollider>(enemy, glm::vec3(0.3f, 0.85f, 0.3f), false);
    reg.emplace<MeshRenderer>(
        enemy,
        cubeMesh->getVAO(), 0u,
        litShader->getId(), gridRed->getId(),
        true, cubeMesh->getIndexCount()
    );

    std::cout << "Spawned enemy at ("
              << origin.x << ", " << origin.y << ", " << origin.z
              << ")" << std::endl;
}
```

Enemies are placeholder red cubes with `Health` and `AABBCollider` components. They do not move or attack -- that requires an AI system, which is future work. The `combatSystem` can already damage them (it checks `Health` on hitscan hits), so you can shoot them and see their health drop in the debug HUD. The collision box matches the player's dimensions so enemies block the player's path.

---

## Step 5: Brush Entity Mesh Building

Brush entities (doors, lifts, triggers) have their own geometry. The mesh-building algorithm is the same one Chapter 17 used for worldspawn, but applied per-entity instead of to the entire level.

### Computing the Entity Origin

Brush entities have no `"origin"` property. Their position in the world is defined by where their brushes are. To use a `Position` component (so the `Mover` system can move the entity), you need to:

1. Calculate the bounding box of all brush vertices
2. Use the center of that bounding box as the entity's origin
3. Offset all mesh vertices so they are relative to that origin

This way, `Position` starts at the center of the entity, and the mesh vertices are centered around `(0,0,0)`. When `moverSystem` changes `Position`, the mesh moves with it.

### Helper Function: Brush Entity AABB

Add this helper to `entity_spawner.cpp`:

```cpp
// Calculate the AABB of all vertices in a brush entity.
// Returns min and max corners.
static void computeBrushEntityBounds(
    const MapEntity& ent,
    glm::vec3& outMin,
    glm::vec3& outMax)
{
    outMin = glm::vec3(std::numeric_limits<float>::max());
    outMax = glm::vec3(std::numeric_limits<float>::lowest());

    for (const auto& brush : ent.brushes)
    {
        // Use the brush-to-polygon algorithm from Ch 17 to get vertices.
        // computeBrushVertices returns the unique vertices of the brush.
        auto vertices = computeBrushVertices(brush);
        for (const auto& v : vertices)
        {
            outMin = glm::min(outMin, v);
            outMax = glm::max(outMax, v);
        }
    }
}
```

`computeBrushVertices` is the function from Chapter 17 that intersects brush planes to find vertex positions. It is declared in `brush_mesh_builder.h`.

### Helper Function: Build Entity Mesh

```cpp
// Build a renderable mesh from a brush entity's brushes.
// Vertices are offset so the mesh is centered at the origin.
// Returns the mesh and the world-space center position.
static std::unique_ptr<Mesh> buildBrushEntityMesh(
    const MapEntity& ent,
    glm::vec3& outCenter)
{
    glm::vec3 boundsMin, boundsMax;
    computeBrushEntityBounds(ent, boundsMin, boundsMax);
    outCenter = (boundsMin + boundsMax) * 0.5f;

    // Build polygons from all brushes using the Ch 17 pipeline.
    // buildBrushMesh returns vertices and indices for the entity's geometry.
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    buildBrushMesh(ent.brushes, vertices, indices);

    // Offset all vertex positions so they are relative to the center.
    // This lets the Position component control the entity's world position.
    for (auto& v : vertices)
    {
        v.position -= outCenter;
    }

    if (vertices.empty())
    {
        return nullptr;
    }

    return std::make_unique<Mesh>(vertices, indices);
}
```

The key insight is the vertex offset. Without it, the mesh vertices would be at their absolute world positions, and `Position` would need to be `(0,0,0)` for the entity to render correctly. By subtracting the center, the vertices become relative to the entity's position. When the render system computes the model matrix as `translate(Position) * scale(Scale)`, the mesh appears at the right place and can be moved by changing `Position`.

### spawnDoor

```cpp
static void spawnDoor(
    entt::registry& reg,
    const MapEntity& ent,
    const ResourceManager& res)
{
    // Build the mesh and find the entity's center position
    glm::vec3 center;
    auto mesh = buildBrushEntityMesh(ent, center);
    if (!mesh)
    {
        std::cerr << "Warning: func_door has no valid geometry" << std::endl;
        return;
    }

    // Read properties
    float speed = getPropertyFloat(ent, "speed", 100.0f);
    float waitTime = getPropertyFloat(ent, "wait", 3.0f);
    float lip = getPropertyFloat(ent, "lip", 8.0f);
    int angleDeg = getPropertyInt(ent, "angle", 0);

    // Calculate the movement direction from the angle property.
    // Quake convention: 0=+X, 90=+Y, 180=-X, 270=-Y, -1=up, -2=down
    glm::vec3 moveDir(0.0f);
    if (angleDeg == -1)
    {
        moveDir = glm::vec3(0.0f, 1.0f, 0.0f);  // up
    }
    else if (angleDeg == -2)
    {
        moveDir = glm::vec3(0.0f, -1.0f, 0.0f);  // down
    }
    else
    {
        float rad = glm::radians(static_cast<float>(angleDeg));
        moveDir = glm::vec3(std::cos(rad), 0.0f, std::sin(rad));
    }

    // Calculate travel distance from the brush size along the movement axis.
    // The door slides its own width minus the lip.
    glm::vec3 boundsMin, boundsMax;
    computeBrushEntityBounds(ent, boundsMin, boundsMax);
    glm::vec3 size = boundsMax - boundsMin;

    // Project the brush size onto the movement direction to get travel distance
    float travelDist = glm::abs(glm::dot(size, moveDir)) - lip;
    if (travelDist < 0.0f) travelDist = 0.0f;

    glm::vec3 closedPos = center;
    glm::vec3 openPos = center + moveDir * travelDist;

    // Store the mesh in the resource manager so it persists
    static int doorMeshId = 0;
    std::string meshName = "door_mesh_" + std::to_string(doorMeshId++);
    res.storeMesh(meshName, std::move(mesh));
    auto storedMesh = res.getMesh(meshName);

    auto litShader = res.getShader("lit");
    auto gridGrey = res.getTexture("grid_grey");

    // Create the door entity
    auto door = reg.create();
    reg.emplace<Position>(door, closedPos);
    reg.emplace<MeshRenderer>(
        door,
        storedMesh->getVAO(), 0u,
        litShader->getId(), gridGrey->getId(),
        true, storedMesh->getIndexCount()
    );

    // Collision box from the brush bounds
    glm::vec3 halfExtents = size * 0.5f;
    reg.emplace<AABBCollider>(door, halfExtents, false);

    // Mover component — controlled by trigger activation
    reg.emplace<Mover>(
        door,
        closedPos,                 // startPos
        openPos,                   // endPos
        speed,                     // speed (units/sec)
        waitTime,                  // wait time at open position
        0.0f,                      // startDelay
        0.0f,                      // timer
        0.0f,                      // progress
        MoverState::Idle,
        true                       // requiresTrigger
    );

    std::cout << "Spawned door at ("
              << closedPos.x << ", " << closedPos.y << ", " << closedPos.z
              << ") travel=" << travelDist << std::endl;
}
```

The door reads `angle` to determine which direction it slides, calculates travel distance from its own brush size minus the `lip`, and creates a `Mover` that interpolates between the closed and open positions. The kinematic Jolt body is created later in `main.cpp` (same as the existing code that iterates all `Mover` entities).

The `lip` property is a Quake convention -- it specifies how many units short of full travel the door stops. A lip of 8 means the door stops 8 units before it would fully clear the doorway. This prevents the door from overshooting and leaving a visible gap.

### spawnLift

```cpp
static void spawnLift(
    entt::registry& reg,
    const MapEntity& ent,
    const ResourceManager& res)
{
    // Build the mesh and find the entity's center position
    glm::vec3 center;
    auto mesh = buildBrushEntityMesh(ent, center);
    if (!mesh)
    {
        std::cerr << "Warning: func_lift has no valid geometry" << std::endl;
        return;
    }

    // Read properties
    float speed = getPropertyFloat(ent, "speed", 64.0f);
    float waitTime = getPropertyFloat(ent, "wait", 3.0f);
    float height = getPropertyFloat(ent, "height", 128.0f);

    glm::vec3 bottomPos = center;
    glm::vec3 topPos = center + glm::vec3(0.0f, height, 0.0f);

    // Compute collision box from brush bounds
    glm::vec3 boundsMin, boundsMax;
    computeBrushEntityBounds(ent, boundsMin, boundsMax);
    glm::vec3 size = boundsMax - boundsMin;
    glm::vec3 halfExtents = size * 0.5f;

    // Store the mesh
    static int liftMeshId = 0;
    std::string meshName = "lift_mesh_" + std::to_string(liftMeshId++);
    res.storeMesh(meshName, std::move(mesh));
    auto storedMesh = res.getMesh(meshName);

    auto litShader = res.getShader("lit");
    auto gridGrey = res.getTexture("grid_grey");

    // Create the lift entity
    auto lift = reg.create();
    reg.emplace<Position>(lift, bottomPos);
    reg.emplace<MeshRenderer>(
        lift,
        storedMesh->getVAO(), 0u,
        litShader->getId(), gridGrey->getId(),
        true, storedMesh->getIndexCount()
    );
    reg.emplace<AABBCollider>(lift, halfExtents, false);

    // Mover component — lifts move straight up
    reg.emplace<Mover>(
        lift,
        bottomPos,                 // startPos (bottom)
        topPos,                    // endPos (top)
        speed,                     // speed
        waitTime,                  // wait time at top
        2.0f,                      // startDelay — gives player time to position
        0.0f,                      // timer
        0.0f,                      // progress
        MoverState::Idle,
        true                       // requiresTrigger
    );

    std::cout << "Spawned lift at ("
              << bottomPos.x << ", " << bottomPos.y << ", " << bottomPos.z
              << ") height=" << height << std::endl;
}
```

Lifts are simpler than doors -- they always move straight up by `height` units. The 2-second `startDelay` gives the player time to position themselves on the platform after stepping on the trigger, matching the behavior from Chapter 15d.

### spawnTrigger

```cpp
static void spawnTrigger(
    entt::registry& reg,
    const MapEntity& ent)
{
    const auto classname = getProperty(ent, "classname");

    // Calculate AABB from brush geometry
    glm::vec3 boundsMin, boundsMax;
    computeBrushEntityBounds(ent, boundsMin, boundsMax);
    glm::vec3 center = (boundsMin + boundsMax) * 0.5f;
    glm::vec3 halfExtents = (boundsMax - boundsMin) * 0.5f;

    auto trigger = reg.create();
    reg.emplace<Position>(trigger, center);
    reg.emplace<AABBCollider>(trigger, halfExtents, true);

    // Determine the trigger action and properties based on classname
    TriggerVolume vol;
    vol.target = entt::null;

    if (classname == "trigger_once")
    {
        vol.action = TriggerAction::ActivateMover;
        vol.onlyOnce = true;
        // Target linking happens in a post-processing step (see below)
    }
    else if (classname == "trigger_multiple")
    {
        vol.action = TriggerAction::ActivateMover;
        vol.onlyOnce = false;
        vol.cooldown = getPropertyFloat(ent, "wait", 1.0f);
    }
    else if (classname == "trigger_hurt")
    {
        vol.action = TriggerAction::Damage;
        vol.value = getPropertyFloat(ent, "dmg", 10.0f);
    }
    else if (classname == "trigger_teleport")
    {
        vol.action = TriggerAction::Teleport;
        // Destination is resolved by target name in post-processing
    }

    reg.emplace<TriggerVolume>(trigger, vol);

    // Debug wireframe — visible in editor builds
    auto cubeMesh = reg.ctx().contains<CombatResources>()
        ? reg.ctx().get<CombatResources>().cubeVAO : 0u;

    // Triggers are invisible during gameplay (no MeshRenderer).
    // Uncomment the following block to add debug wireframes:
    /*
    reg.emplace<Scale>(trigger, halfExtents * 2.0f);
    reg.emplace<MeshRenderer>(trigger, cubeMesh, 0u,
                               litShader, gridGreen,
                               true, cubeIndexCount);
    reg.emplace<TagDebugWireframe>(trigger);
    */

    std::cout << "Spawned " << classname << " at ("
              << center.x << ", " << center.y << ", " << center.z
              << ")" << std::endl;
}
```

Triggers are brush entities, but they are invisible -- they have no `MeshRenderer`. Their only purpose is to detect player overlap via the `AABBCollider` and `TriggerVolume` components. The AABB is computed from the brush geometry bounds, and the center becomes the entity's `Position`.

A debug wireframe block is included but commented out. During development, you can uncomment it to see trigger volumes as green wireframe boxes, exactly like the old hardcoded triggers in `scene_setup.cpp`.

### Target Linking

Triggers that activate movers (like `trigger_once` opening a `func_door`) need the `TriggerVolume::target` to point to the door's entity handle. But at spawn time, the door might not have been created yet -- the map file might list the trigger before the door.

The solution is a two-pass approach. After all entities are spawned, resolve target references by matching `target` properties to `targetname` properties:

Add this function to `entity_spawner.cpp`, and call it at the end of `spawnEntities`:

```cpp
// Resolve "target" → "targetname" links between triggers and their targets.
static void resolveTargets(
    entt::registry& registry,
    const MapFile& mapFile)
{
    // Build a map of targetname → entity handle.
    // We need to re-iterate entities to match them to their registry entities.
    // Store targetname during spawning by adding it as a component.

    // First, collect all entities with a targetname
    struct TargetName { std::string name; };

    // For each trigger with a "target" property, find the entity whose
    // "targetname" matches and set TriggerVolume::target to that entity.
    auto triggerView = registry.view<TriggerVolume, Position>();
    auto moverView = registry.view<Mover, Position>();

    // Simple brute-force: for small entity counts this is fine.
    // For each trigger, check if any mover is within reasonable distance
    // of where the target should be.

    // A more robust approach: during spawning, store targetname as a
    // component and look it up here. For now, use the map file data directly.

    // Build targetname → entity index map from the map file
    std::unordered_map<std::string, size_t> nameToIndex;
    for (size_t i = 1; i < mapFile.entities.size(); i++)
    {
        const auto& ent = mapFile.entities[i];
        auto it = ent.properties.find("targetname");
        if (it != ent.properties.end() && !it->second.empty())
        {
            nameToIndex[it->second] = i;
        }
    }

    // Track which map entity index maps to which ECS entity.
    // We re-iterate the same spawn order to reconstruct this mapping.
    std::unordered_map<size_t, entt::entity> indexToEntity;
    size_t entityIdx = 0;

    // This is fragile — a better approach is to store the mapping during
    // spawnEntities. For now, iterate registry entities in creation order.
    // EnTT creates entities with incrementing IDs, so we can match by order.
    auto allEntities = registry.view<Position>();
    for (auto entity : allEntities)
    {
        // Skip the worldspawn mesh entities (created before spawnEntities)
        // This is approximate — see the note below for a cleaner approach
    }

    // PRACTICAL APPROACH: Since trigger_once and trigger_multiple are the
    // only triggers that need target linking, and they typically target
    // the nearest mover, use proximity matching as a fallback.
    // The proper solution is shown in the "Improved Target Linking" section.

    std::cout << "Target resolution complete" << std::endl;
}
```

The target linking problem deserves a cleaner solution. Here is the improved approach -- store entity handles during spawning:

### Improved Target Linking

Modify the top of `entity_spawner.cpp` to maintain a target name registry:

```cpp
// Map of targetname → ECS entity handle, built during spawning
static std::unordered_map<std::string, entt::entity> g_targetNames;
// Map of entities that need their "target" resolved after all spawning
static std::vector<std::pair<entt::entity, std::string>> g_pendingTargets;
```

In each spawn function that reads `targetname`, register the entity:

```cpp
// In spawnDoor and spawnLift, after creating the entity:
std::string targetname = getProperty(ent, "targetname");
if (!targetname.empty())
{
    g_targetNames[targetname] = door;  // or lift
}
```

In `spawnTrigger`, record the pending target link:

```cpp
std::string target = getProperty(ent, "target");
if (!target.empty())
{
    g_pendingTargets.push_back({trigger, target});
}
```

At the end of `spawnEntities`, resolve all pending links:

```cpp
void spawnEntities(
    entt::registry& registry,
    const MapFile& mapFile,
    const ResourceManager& resources)
{
    // Clear target maps from any previous load
    g_targetNames.clear();
    g_pendingTargets.clear();

    // ... entity spawning loop (unchanged) ...

    // Resolve target links
    for (const auto& [triggerEntity, targetName] : g_pendingTargets)
    {
        auto it = g_targetNames.find(targetName);
        if (it != g_targetNames.end())
        {
            auto& vol = registry.get<TriggerVolume>(triggerEntity);
            vol.target = it->second;
            std::cout << "Linked trigger to target '" << targetName << "'"
                      << std::endl;
        }
        else
        {
            std::cerr << "Warning: trigger targets '" << targetName
                      << "' but no entity has that targetname" << std::endl;
        }
    }
}
```

This two-pass approach handles any entity ordering in the map file. Triggers can appear before or after their targets -- the links are resolved after everything is spawned.

---

## Step 6: Replacing scene_setup.cpp

The old `setupScene` function hardcoded every entity: player position, light positions, cube demos, door geometry, trigger placement. With the entity spawner, all of that comes from the map file.

### The New Pipeline

The initialization flow becomes:

```
1. parseMapFile("assets/maps/test.map")  →  MapFile       (Ch 17)
2. buildMapMeshes(registry, mapFile)     →  worldspawn     (Ch 17)
3. spawnEntities(registry, mapFile, res) →  all gameplay   (Ch 18 — this chapter)
```

### Update `main.cpp`

Replace the old scene setup with the new pipeline. Here are the changes:

**Add new includes:**

```cpp
// Replace this:
#include "engine/ecs/scene_setup.h"

// With these:
#include "engine/level/map_parser.h"        // parseMapFile (Ch 17)
#include "engine/level/brush_mesh_builder.h" // buildMapMeshes (Ch 17)
#include "engine/level/entity_spawner.h"     // spawnEntities (Ch 18)
```

**Replace the scene setup call:**

```cpp
// ─── OLD (remove) ────────────────────────────────────────
// Level level = setupScene(registry, resources);
// createLevelBodies(registry, level);

// ─── NEW ─────────────────────────────────────────────────
// Parse the .map file
MapFile mapFile = parseMapFile("assets/maps/test.map");

// Build renderable meshes from worldspawn brushes (Ch 17)
buildMapMeshes(registry, mapFile, resources);

// Spawn all gameplay entities from the map file (Ch 18)
spawnEntities(registry, mapFile, resources);
```

**Keep the Jolt body creation code.** The loop that creates kinematic bodies for movers and sensor bodies for triggers is unchanged -- it iterates all entities with the right components, regardless of how they were created:

```cpp
joltWorld.physicsSystem->OptimizeBroadPhase();

// Create Jolt bodies for movers (lifts, doors)
auto moverView = registry.view<Position, AABBCollider, Mover>();
for (auto [entity, pos, col, mover] : moverView.each())
{
    createKinematicBody(registry, entity);
}

// Create sensor bodies for triggers
auto triggerView = registry.view<Position, AABBCollider, TriggerVolume>();
for (auto [entity, pos, col, trigger] : triggerView.each())
{
    if (col.isTrigger)
    {
        createSensorBody(registry, entity);
    }
}

// Initialise the player's CharacterVirtual
initPlayerCharacter(registry);

joltWorld.physicsSystem->OptimizeBroadPhase();
```

This code does not change because it queries the ECS for components, not for how those components were created. The `Mover` entities spawned by `spawnDoor` and `spawnLift` are picked up automatically. This is one of the strengths of ECS -- the spawning code and the physics body creation code are completely decoupled.

**Remove the Level variable.** The old code passed `Level& level` to `combatSystem` for hitscan ray-vs-level collision. With brush-based levels, you will need a different collision approach (Chapter 19). For now, create an empty `Level` as a placeholder:

```cpp
Level level;  // Placeholder — brush collision replaces this in Ch 19
```

### Archive scene_setup.cpp

The old `scene_setup.cpp` and `scene_setup.h` are no longer called. Move them to an archive directory:

```
src/engine/ecs/archived/scene_setup.cpp
src/engine/ecs/archived/scene_setup.h
```

Keep `showcase_level.cpp` and `showcase_level.h` in the archive too -- the showcase level was hardcoded geometry that the map file now replaces.

---

## Step 7: Update the Test Map

Expand the test map from Chapter 17 with entity definitions. The worldspawn (entity 0) stays the same -- it is the room geometry. Add entities after it.

### New entities for `assets/maps/test.map`

Append these entities after the worldspawn closing brace:

```
// entity 1 — player spawn
{
"classname" "info_player_start"
"origin" "128 40 128"
"angle" "90"
}
// entity 2 — bright ceiling light (center of room)
{
"classname" "light"
"origin" "256 224 256"
"light" "300"
"color" "255 255 255"
}
// entity 3 — red accent light (near left wall)
{
"classname" "light"
"origin" "48 96 192"
"light" "150"
"color" "255 50 50"
}
// entity 4 — blue accent light (near right wall)
{
"classname" "light"
"origin" "464 96 320"
"light" "150"
"color" "50 50 255"
}
// entity 5 — health pickup
{
"classname" "item_health"
"origin" "192 16 384"
"amount" "25"
}
// entity 6 — ammo pickup
{
"classname" "item_ammo_shells"
"origin" "320 16 384"
"amount" "20"
}
// entity 7 — grunt enemy
{
"classname" "monster_grunt"
"origin" "384 40 128"
"angle" "180"
}
// entity 8 — door
{
"classname" "func_door"
"angle" "0"
"speed" "100"
"wait" "4"
"lip" "8"
"targetname" "door1"
// brush 0 — the door slab
{
( 352 0 192 ) ( 352 1 192 ) ( 352 0 193 ) door1 0 0 0 1 1
( 360 0 192 ) ( 360 0 193 ) ( 360 1 192 ) door1 0 0 0 1 1
( 352 0 192 ) ( 352 0 193 ) ( 353 0 192 ) door1 0 0 0 1 1
( 352 96 192 ) ( 353 96 192 ) ( 352 96 193 ) door1 0 0 0 1 1
( 352 0 192 ) ( 353 0 192 ) ( 352 1 192 ) door1 0 0 0 1 1
( 352 0 256 ) ( 352 1 256 ) ( 353 0 256 ) door1 0 0 0 1 1
}
}
// entity 9 — trigger to open the door
{
"classname" "trigger_once"
"target" "door1"
// brush 0 — invisible trigger volume in front of the door
{
( 320 0 192 ) ( 320 1 192 ) ( 320 0 193 ) trigger 0 0 0 1 1
( 352 0 192 ) ( 352 0 193 ) ( 352 1 192 ) trigger 0 0 0 1 1
( 320 0 192 ) ( 320 0 193 ) ( 321 0 192 ) trigger 0 0 0 1 1
( 320 64 192 ) ( 321 64 192 ) ( 320 64 193 ) trigger 0 0 0 1 1
( 320 0 192 ) ( 321 0 192 ) ( 320 1 192 ) trigger 0 0 0 1 1
( 320 0 256 ) ( 320 1 256 ) ( 321 0 256 ) trigger 0 0 0 1 1
}
}
```

The entity positions use TrenchBroom's coordinate scale, which Chapter 17's parser converts to engine units. Adjust positions to fit your room geometry. The key point is the structure -- each entity has a `classname`, properties specific to that entity type, and optional brushes for brush entities.

> **Tip:** Once TrenchBroom integration is complete (Chapter 20), you will edit this file visually instead of by hand. For now, these hand-authored entities are enough to test the spawner.

---

## Step 8: Update CMakeLists.txt

Add the two new source files to the build:

```cmake
add_executable(QEngine
    src/main.cpp
    src/engine/core/input_manager.cpp
    src/engine/core/resource_manager.cpp
    src/engine/core/window.cpp
    src/engine/ecs/jolt_body_helpers.cpp
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
    src/engine/level/entity_helpers.cpp       # NEW
    src/engine/level/entity_spawner.cpp       # NEW
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

Note that `scene_setup.cpp` and `showcase_level.cpp` have been removed from the build. They are archived, not deleted.

---

## What Changed -- Summary

| File | Change |
|------|--------|
| `assets/QEngine.fgd` | **New** -- entity definitions for TrenchBroom |
| `src/engine/level/entity_helpers.h` | **New** -- property reading helpers |
| `src/engine/level/entity_helpers.cpp` | **New** -- implementation of property readers |
| `src/engine/level/entity_spawner.h` | **New** -- `spawnEntities` declaration |
| `src/engine/level/entity_spawner.cpp` | **New** -- all spawn functions and target linking |
| `src/main.cpp` | Replaced `setupScene` call with `parseMapFile` + `buildMapMeshes` + `spawnEntities` pipeline |
| `assets/maps/test.map` | Added player spawn, lights, pickups, enemy, door, and trigger entities |
| `CMakeLists.txt` | Added `entity_helpers.cpp` and `entity_spawner.cpp`, removed `scene_setup.cpp` and `showcase_level.cpp` |
| `src/engine/ecs/archived/` | Moved `scene_setup.h/.cpp` and `showcase_level.h/.cpp` here |

### New Files Summary

| File | Purpose | Line Count (approx) |
|------|---------|---------------------|
| `entity_helpers.h` | Property reading API | ~30 |
| `entity_helpers.cpp` | Safe string-to-type parsing with error handling | ~80 |
| `entity_spawner.h` | Single public function declaration | ~10 |
| `entity_spawner.cpp` | Dispatcher + 8 spawn functions + target linking | ~350 |
| `QEngine.fgd` | TrenchBroom entity definitions | ~60 |

---

## What You Should See

After building and running with the updated test map:

1. **Player spawns at the map's `info_player_start` position** -- not the old hardcoded `(15, 1.7, 15)`. The console prints the spawn location.
2. **Point lights illuminate from their map positions** -- a bright white ceiling light and colored accent lights, matching the map entity data.
3. **Health and ammo pickups appear as colored cubes** -- green for health, orange for ammo, at their specified origins.
4. **The enemy is a red cube** -- it has health and a collider, and you can damage it with hitscan weapons.
5. **The door brush entity is visible and solid** -- it renders as a textured slab at its brush position.
6. **Walking into the trigger volume opens the door** -- the trigger links to the door via `targetname`, the `Mover` activates, and the door slides in the direction specified by `angle`.
7. **The console output lists every spawned entity** -- useful for debugging entity counts and positions.

If something does not appear, check the console output. Every spawn function prints the entity type and position. Missing entities likely have a `classname` typo in the map file, or coordinates that place them outside the room geometry.

---

## What's Next

The entities spawn and render, but the player passes through brush geometry like a ghost. In **Chapter 19: Brush Collision**, we will generate Jolt static bodies from worldspawn and brush entity geometry so the player collides with walls, floors, doors, and lifts.
