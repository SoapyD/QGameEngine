# Chapter 19: Brush Collision

## What You'll Learn
- Why brush geometry needs its own collision approach (and why the old level collision no longer applies)
- Creating Jolt `ConvexHullShape` bodies from brush vertices
- Generating static collision for worldspawn brushes
- Adding kinematic collision to brush entities (doors, lifts)
- Converting trigger brush entities into sensor bodies
- Removing the old `createLevelBodies` path and wiring up the new one
- Handling coordinate system differences between Quake/TrenchBroom and OpenGL

---

## Step 1: The Collision Problem

After Chapter 18, you can load a `.map` file from TrenchBroom, render textured brush geometry, and spawn entities from `classname` properties. But the player walks straight through everything. There is no collision.

The current Jolt collision comes from `createLevelBodies` in `jolt_body_helpers.cpp`. That function iterates the old `Level` struct (sectors and surfaces), computes an AABB for each surface quad, and creates a static box body. This approach was designed for the hardcoded `createShowcaseLevel` geometry from Chapter 8 -- flat walls defined by four vertices each. It knows nothing about brushes.

We need a new approach that generates collision directly from brush geometry.

### Two Approaches

There are two reasonable strategies for brush collision:

| Approach | How It Works | Pros | Cons |
|----------|-------------|------|------|
| **Convex hull per brush** | Collect all vertices from a brush, feed them to `ConvexHullShapeSettings` | Fast, exact match for brush shape, Jolt optimises convex-convex tests heavily | One body per brush (manageable for typical maps) |
| **Mesh shape from triangles** | Triangulate all faces, build a `MeshShape` from the triangle soup | Single body for the entire level | Slower collision queries, concave shapes need extra care, no moving mesh support |

We will use **convex hull per brush**. This is the natural choice because every brush in the `.map` format is convex by definition -- it is the intersection of half-planes. Jolt's `ConvexHullShapeSettings` accepts a point cloud and produces a tight convex hull, giving us collision geometry that exactly matches the visible brush.

---

## Step 2: Convex Hull Shapes from Brushes

The key insight is that each brush already has its face polygons computed from Chapter 17's plane intersection algorithm. Every `MapBrushFace` contains a `std::vector<glm::vec3> vertices`. To build a convex hull, we collect all vertices from all faces of a single brush and hand them to Jolt.

### New function: `createBrushShape`

Add this to `jolt_body_helpers.cpp`:

```cpp
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>

JPH::ShapeRefC createBrushShape(const MapBrush& brush)
{
    // Collect all vertices from all faces of this brush
    std::vector<JPH::Vec3> points;
    for (const auto& face : brush.faces)
    {
        for (const auto& v : face.vertices)
        {
            points.push_back(JPH::Vec3(v.x, v.y, v.z));
        }
    }

    if (points.size() < 4)
    {
        // A valid convex hull needs at least 4 non-coplanar points.
        // If a brush has fewer, it's degenerate — skip it.
        return nullptr;
    }

    JPH::ConvexHullShapeSettings settings(points.data(), (int)points.size());
    settings.mMaxConvexRadius = 0.05f; // Jolt's default convex radius

    auto result = settings.Create();
    if (result.HasError())
    {
        // ConvexHullShape creation can fail for degenerate geometry
        // (e.g. all points coplanar, or too thin).
        // Fall back to an AABB box shape.
        std::cout << "[Collision] ConvexHull failed for brush: "
                  << result.GetError() << " — falling back to AABB" << std::endl;

        // Compute AABB from points
        JPH::Vec3 bmin = points[0];
        JPH::Vec3 bmax = points[0];
        for (const auto& p : points)
        {
            bmin = JPH::Vec3::sMin(bmin, p);
            bmax = JPH::Vec3::sMax(bmax, p);
        }

        // Fatten thin dimensions so Jolt's convex radius doesn't collapse them
        JPH::Vec3 halfExtents = (bmax - bmin) * 0.5f;
        for (int i = 0; i < 3; i++)
        {
            if (halfExtents[i] < 0.1f)
                halfExtents.SetComponent(i, 0.1f);
        }

        JPH::BoxShapeSettings boxSettings(halfExtents);
        auto boxResult = boxSettings.Create();
        if (boxResult.HasError())
            return nullptr;

        return boxResult.Get();
    }

    return result.Get();
}
```

**Why convex hull instead of box?** A box shape only captures the AABB of the brush. For axis-aligned walls and floors, that is fine. But for angled geometry -- sloped ramps, wedge-shaped doorframes, diagonal walls -- the AABB is a poor fit. The convex hull matches the actual brush shape exactly, so the player slides along angled surfaces correctly.

**Why the fallback?** Degenerate brushes can appear in maps -- an extremely thin wall, or a brush where all vertices happen to be coplanar after floating-point rounding. Jolt's `ConvexHullShapeSettings::Create` will return an error for these. Rather than crashing, we fall back to a box shape computed from the AABB. This is an imperfect approximation, but it keeps the game running while you fix the map.

---

## Step 3: Creating Static Bodies for Worldspawn Brushes

Entity 0 in every `.map` file is the **worldspawn** -- it contains all the structural brushes that form the level (walls, floors, ceilings, pillars, stairs). These are static geometry that never moves.

### New function: `createMapCollision`

Add this to `jolt_body_helpers.h`:

```cpp
#include "engine/level/map_types.h"  // MapFile, MapBrush, etc.

void createMapCollision(entt::registry& registry, const MapFile& mapFile);
```

And the implementation in `jolt_body_helpers.cpp`:

```cpp
void createMapCollision(entt::registry& registry, const MapFile& mapFile)
{
    auto& jolt = registry.ctx().get<JoltWorld>();
    auto& bodyInterface = jolt.getBodyInterface();

    // Entity 0 is always the worldspawn
    if (mapFile.entities.empty()) return;

    const auto& worldspawn = mapFile.entities[0];
    int created = 0;
    int failed = 0;

    for (const auto& brush : worldspawn.brushes)
    {
        JPH::ShapeRefC shape = createBrushShape(brush);
        if (!shape)
        {
            failed++;
            continue;
        }

        // Worldspawn brushes are defined in world space.
        // The shape already contains the correct vertex positions,
        // so we place the body at the origin.
        // Note: ConvexHullShape centers itself internally —
        // Jolt computes the centroid and offsets the vertices.
        // The body position must be at the origin so the
        // hull vertices map to world coordinates correctly.

        JPH::BodyCreationSettings bodySettings(
            shape,
            JPH::RVec3(0.0f, 0.0f, 0.0f),
            JPH::Quat::sIdentity(),
            JPH::EMotionType::Static,
            Layers::NON_MOVING
        );

        JPH::BodyID bodyId = bodyInterface.CreateAndAddBody(
            bodySettings, JPH::EActivation::DontActivate
        );

        if (bodyId.IsInvalid())
        {
            failed++;
        }
        else
        {
            created++;
        }
    }

    std::cout << "[Collision] Worldspawn: " << created << " brush bodies created";
    if (failed > 0)
        std::cout << " (" << failed << " failed)";
    std::cout << std::endl;
}
```

### Important: How Jolt Handles Convex Hull Centering

When you create a `ConvexHullShapeSettings` from a set of points, Jolt internally computes the centroid of the hull and shifts the shape so that its center of mass is at the local origin. This means when you set the body position to `(0, 0, 0)`, the shape's vertices end up at their original world-space coordinates.

If your brush vertices are already in world space (which they are -- the `.map` parser from Chapter 17 computes them there), then placing the body at the origin is correct. Jolt's internal centering takes care of the offset.

> **Performance note:** For very large maps with hundreds of brushes, you could batch all worldspawn brushes into a single `JPH::StaticCompoundShapeSettings`. This reduces Jolt's body count and can improve broad-phase efficiency. For now, one body per brush is straightforward and performs well for typical Quake-sized maps (50-200 brushes in the worldspawn).

---

## Step 4: Brush Entity Collision

Brush entities like `func_door` and `func_lift` already have visible meshes from Chapter 18's entity spawning. Now they need collision bodies so the player cannot walk through them.

These entities are fundamentally different from worldspawn brushes:

| Property | Worldspawn Brush | Brush Entity (door/lift) |
|----------|-----------------|--------------------------|
| Motion type | Static | Kinematic |
| Object layer | `NON_MOVING` | `MOVING` |
| Activation | `DontActivate` | `Activate` |
| Movement | Never moves | Driven by `Mover` component via `MoveKinematic` |

The key difference is that brush entities **move**. A door slides open. A lift rises. They must be kinematic bodies so Jolt calculates velocity during the sweep and pushes the player's `CharacterVirtual` out of the way.

### New function: `createBrushEntityBody`

Add to `jolt_body_helpers.h`:

```cpp
void createBrushEntityBody(entt::registry& registry, entt::entity entity,
                           const MapBrush& brush, bool isKinematic);
```

Implementation in `jolt_body_helpers.cpp`:

```cpp
void createBrushEntityBody(entt::registry& registry, entt::entity entity,
                           const MapBrush& brush, bool isKinematic)
{
    auto& jolt = registry.ctx().get<JoltWorld>();
    auto& bodyInterface = jolt.getBodyInterface();
    auto& pos = registry.get<Position>(entity);

    JPH::ShapeRefC shape = createBrushShape(brush);
    if (!shape) return;

    // Brush entity vertices are in world space from the parser.
    // The entity's Position component holds the computed origin
    // (typically the center of the brush bounding box, set during
    // entity spawning in Ch 18).
    //
    // Since the convex hull was built from world-space vertices,
    // and Jolt centers the hull internally, we place the body
    // at the origin — same as worldspawn brushes.
    //
    // When the Mover component animates, moverSyncSystem calls
    // MoveKinematic to move the body. The offset between the
    // entity's logical position and the body's centroid stays
    // consistent because both were derived from the same vertices.

    JPH::EMotionType motionType = isKinematic
        ? JPH::EMotionType::Kinematic
        : JPH::EMotionType::Static;

    JPH::ObjectLayer layer = isKinematic
        ? Layers::MOVING
        : Layers::NON_MOVING;

    JPH::EActivation activation = isKinematic
        ? JPH::EActivation::Activate
        : JPH::EActivation::DontActivate;

    JPH::BodyCreationSettings bodySettings(
        shape,
        JPH::RVec3(0.0f, 0.0f, 0.0f),
        JPH::Quat::sIdentity(),
        motionType,
        layer
    );

    JPH::BodyID bodyId = bodyInterface.CreateAndAddBody(bodySettings, activation);
    if (!bodyId.IsInvalid())
    {
        registry.emplace<JoltBody>(entity, bodyId);
    }
}
```

### Updated Entity Spawning

In Chapter 18's entity spawning code (inside the `spawnDoor` / `spawnLift` factory functions), you already create the ECS entity with `Position`, `Mover`, `Scale`, and `MeshRenderer`. Now add the collision body.

**For doors** (in the entity spawning code):

```cpp
void spawnDoor(entt::registry& registry, const MapEntity& mapEntity,
               const ResourceManager& resources)
{
    // ... (existing code from Ch 18: parse properties, create entity,
    //      build mesh from brush faces, emplace Position, Mover,
    //      MeshRenderer, Scale) ...

    auto entity = registry.create();
    // ... emplace components as before ...

    // Add collision from the first brush
    if (!mapEntity.brushes.empty())
    {
        createBrushEntityBody(registry, entity, mapEntity.brushes[0], true);
    }
}
```

**For lifts** -- the same pattern:

```cpp
void spawnLift(entt::registry& registry, const MapEntity& mapEntity,
               const ResourceManager& resources)
{
    // ... existing setup ...

    if (!mapEntity.brushes.empty())
    {
        createBrushEntityBody(registry, entity, mapEntity.brushes[0], true);
    }
}
```

The `true` parameter means "kinematic" -- these bodies will be moved by `moverSyncSystem` each tick via `MoveKinematic`.

### How Movement Works

The chain is:

1. `triggerSystem` detects the player overlapping a trigger volume and sets `mover.state = MoverState::Moving`
2. `moverSystem` updates `mover.progress` and interpolates `Position` between `startPos` and `endPos`
3. `moverSyncSystem` reads the entity's `Position` and calls `bodyInterface.MoveKinematic(bodyId, newPos, identity, dt)`
4. Jolt's next `PhysicsSystem::Update` sweeps the kinematic body from old position to new position, pushing the player's `CharacterVirtual` if it is in the way

This is the same chain that already works for the hardcoded door and lift from the showcase level. The only difference is that the collision shape is now a convex hull instead of a box.

---

## Step 5: Trigger Volume Collision

Trigger brushes (`trigger_once`, `trigger_multiple`, `trigger_hurt`) detect when the player enters a volume. They need Jolt sensor bodies -- bodies that participate in overlap queries but do not generate a collision response.

For triggers, we use a simpler approach than convex hulls: compute the AABB of all brush vertices and create a box-shaped sensor. Triggers do not need precise shape matching because the player just needs to be "roughly inside" the volume. An AABB is faster and sufficient.

### New function: `createBrushSensorBody`

Add to `jolt_body_helpers.h`:

```cpp
void createBrushSensorBody(entt::registry& registry, entt::entity entity,
                           const MapBrush& brush);
```

Implementation in `jolt_body_helpers.cpp`:

```cpp
void createBrushSensorBody(entt::registry& registry, entt::entity entity,
                           const MapBrush& brush)
{
    auto& jolt = registry.ctx().get<JoltWorld>();
    auto& bodyInterface = jolt.getBodyInterface();

    // Compute AABB from all brush vertices
    glm::vec3 bmin(FLT_MAX);
    glm::vec3 bmax(-FLT_MAX);

    for (const auto& face : brush.faces)
    {
        for (const auto& v : face.vertices)
        {
            bmin = glm::min(bmin, v);
            bmax = glm::max(bmax, v);
        }
    }

    glm::vec3 halfExtents = (bmax - bmin) * 0.5f;
    glm::vec3 centre = (bmin + bmax) * 0.5f;

    // Fatten any near-zero dimensions
    for (int i = 0; i < 3; i++)
    {
        if (halfExtents[i] < 0.1f)
            halfExtents[i] = 0.1f;
    }

    JPH::BoxShapeSettings shapeSettings(
        JPH::Vec3(halfExtents.x, halfExtents.y, halfExtents.z)
    );
    shapeSettings.SetEmbedded();

    auto shapeResult = shapeSettings.Create();
    if (!shapeResult.IsValid()) return;

    JPH::BodyCreationSettings bodySettings(
        shapeResult.Get(),
        JPH::RVec3(centre.x, centre.y, centre.z),
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Static,
        Layers::SENSOR
    );
    bodySettings.mIsSensor = true;

    JPH::BodyID bodyId = bodyInterface.CreateAndAddBody(
        bodySettings, JPH::EActivation::DontActivate
    );

    if (!bodyId.IsInvalid())
    {
        registry.emplace<JoltBody>(entity, bodyId);
    }

    // Also set the AABBCollider so the ECS-based triggerSystem
    // can still do its AABB overlap check
    registry.emplace_or_replace<AABBCollider>(entity, halfExtents, true);
    registry.emplace_or_replace<Position>(entity, centre);
}
```

### Updated Trigger Spawning

In the entity spawning code for triggers:

```cpp
void spawnTrigger(entt::registry& registry, const MapEntity& mapEntity)
{
    auto entity = registry.create();

    // Parse trigger properties from the map entity
    TriggerAction action = TriggerAction::ActivateMover;
    const auto& classname = mapEntity.properties.at("classname");

    if (classname == "trigger_once")
    {
        action = TriggerAction::ActivateMover;
    }
    else if (classname == "trigger_hurt")
    {
        action = TriggerAction::Damage;
    }
    // ... other trigger types ...

    // Parse target, value, etc. from properties
    float value = 0.0f;
    if (mapEntity.properties.count("dmg"))
        value = std::stof(mapEntity.properties.at("dmg"));

    bool onlyOnce = (classname == "trigger_once");

    registry.emplace<TriggerVolume>(entity,
        action,
        entt::null,  // target resolved later by name lookup
        glm::vec3(0.0f),
        value,
        "",
        onlyOnce, false, 0.0f, 0.0f
    );

    // Create the sensor body from brush geometry
    if (!mapEntity.brushes.empty())
    {
        createBrushSensorBody(registry, entity, mapEntity.brushes[0]);
    }
}
```

> **Note on target resolution:** The `.map` format uses string-based `target`/`targetname` pairs to link triggers to their targets. In Chapter 18, you would have implemented a name resolution pass after all entities are spawned. The `entt::null` placeholder for `TriggerVolume.target` gets replaced with the actual entity handle during that pass. That mechanism does not change here -- we are only adding the collision body.

---

## Step 6: Removing Old Level Collision

The old `createLevelBodies` function served its purpose through Chapters 14-15. It created box shapes from the `Level` struct's sector surfaces. Now that collision comes from brush shapes, this function is no longer called.

### Update `main.cpp`

Remove the call to `createLevelBodies` and replace it with `createMapCollision`. The relevant section of `main.cpp` changes from:

```cpp
// OLD — remove this
Level level = setupScene(registry, resources);
createLevelBodies(registry, level);
```

To:

```cpp
// NEW — collision from brush geometry
MapFile mapFile = loadMapFile("assets/maps/e1m1.map");
setupSceneFromMap(registry, resources, mapFile);
createMapCollision(registry, mapFile);
```

The `setupSceneFromMap` function is the updated version of `setupScene` from Chapter 18 that takes a `MapFile` instead of building the showcase level. It handles brush mesh rendering, entity spawning (which now includes collision bodies for doors, lifts, and triggers via the functions we wrote in Steps 4 and 5), and player creation.

### Updated initialization order in `main.cpp`

The full initialization sequence becomes:

```cpp
// ─── ECS: Create the world ───────────────────────────────────
entt::registry registry;

auto& physicsConfig = registry.ctx().emplace<PhysicsConfig>();
FixedTimestep fixedTimestep(physicsConfig.fixedDeltaTime);

auto& joltWorld = registry.ctx().emplace<JoltWorld>();
joltWorld.init();

// Load and process the map file
MapFile mapFile = loadMapFile("assets/maps/e1m1.map");

// Spawn all entities (player, lights, doors, lifts, triggers)
// This also creates brush meshes for rendering
setupSceneFromMap(registry, resources, mapFile);

// Create static collision for all worldspawn brushes
createMapCollision(registry, mapFile);

// Optimise broad phase after adding static geometry
joltWorld.physicsSystem->OptimizeBroadPhase();

// Initialise the player's CharacterVirtual
// (must happen after static bodies exist so the player
//  spawns on solid ground)
initPlayerCharacter(registry);

// Final broad phase optimisation
joltWorld.physicsSystem->OptimizeBroadPhase();
```

Note that kinematic bodies (doors, lifts) and sensor bodies (triggers) are now created during `setupSceneFromMap` -- inside the `spawnDoor`, `spawnLift`, and `spawnTrigger` factory functions. This is cleaner than the old approach of creating entities first and then iterating views to add Jolt bodies in a separate pass.

### Keep `createLevelBodies` in the source

Do not delete `createLevelBodies` from `jolt_body_helpers.cpp`. It still serves as an educational reference for how the old level system worked. If you want to keep the showcase level as a fallback (for testing without a `.map` file), you can add a command-line flag:

```cpp
if (useMapFile)
{
    MapFile mapFile = loadMapFile(mapPath);
    setupSceneFromMap(registry, resources, mapFile);
    createMapCollision(registry, mapFile);
}
else
{
    Level level = setupScene(registry, resources);
    createLevelBodies(registry, level);
}
```

This way, both paths remain functional.

---

## Step 7: Coordinate System Considerations

TrenchBroom uses the Quake coordinate system. OpenGL uses a different one. If you did a coordinate swap in Chapter 17's parser, the collision vertices must receive the same transformation.

### The Two Coordinate Systems

| Axis | Quake/TrenchBroom | OpenGL |
|------|-------------------|--------|
| X | Right | Right |
| Y | Forward (into screen) | Up |
| Z | Up | Backward (out of screen) |

The conversion from Quake to OpenGL coordinates is:

```cpp
glm::vec3 quakeToOpenGL(const glm::vec3& q)
{
    return glm::vec3(q.x, q.z, -q.y);
}
```

This swaps Y and Z, and negates the new Z so that "forward" in Quake (positive Y) becomes "into the screen" in OpenGL (negative Z).

### Where the Conversion Happens

In Chapter 17, the `.map` parser converts vertices when computing face polygons. If you applied `quakeToOpenGL` to each vertex during polygon generation, then `MapBrushFace::vertices` is already in OpenGL space. The collision code in this chapter reads those same vertices, so the collision hulls automatically match the visible geometry.

If you did **not** convert coordinates in the parser (i.e., your renderer handles the swap in the shader or view matrix), then you must apply the same conversion when building collision shapes:

```cpp
// Inside createBrushShape, when collecting points:
for (const auto& face : brush.faces)
{
    for (const auto& v : face.vertices)
    {
        glm::vec3 converted = quakeToOpenGL(v);
        points.push_back(JPH::Vec3(converted.x, converted.y, converted.z));
    }
}
```

**The rule is simple: collision vertices must be in the same coordinate space as the physics world.** Jolt uses whatever coordinate system you give it -- its gravity is set to `(0, -20, 0)` which assumes Y-up. As long as collision vertices and rendered vertices agree on what Y-up means, everything works.

### Scale Factor

Quake maps are typically authored in units where 1 unit = roughly 1 inch (or approximately 1/32 of a meter). If your engine uses a different scale, apply a scale factor during parsing. For QEngine, we use a 1:1 mapping (1 map unit = 1 engine unit), so no scaling is needed.

If you do need scaling, apply it uniformly to all vertices during parsing -- both for rendering and collision.

---

## Step 8: Testing Collision

Build and run with a test map that has walls, a floor, a ceiling, a door, a lift, and a trigger. The test map from Chapters 17-18 works perfectly for this.

### What to Verify

| Test | Expected Result | If It Fails |
|------|----------------|-------------|
| Walk into a wall | Player stops, can slide along the surface | Check that worldspawn brushes have collision bodies (print the count from Step 3) |
| Walk on the floor | Player stays at ground level, no falling | Check that floor brushes exist and their normals point upward |
| Jump into the ceiling | Player bonks and falls back down | Check ceiling brush collision |
| Approach a door | Door trigger activates, door slides open | Check that `spawnDoor` calls `createBrushEntityBody` with `isKinematic = true` |
| Stand on a lift | Lift carries the player upward | Check that `moverSyncSystem` calls `MoveKinematic` on the lift's `JoltBody` |
| Walk into a trigger volume | Trigger action fires (damage, teleport, mover activation) | Check that `createBrushSensorBody` is called and the `TriggerVolume` component is emplaced |
| Walk along a ramp/slope | Player slides smoothly up the incline | Convex hull handles this correctly; AABB would block |

### Common Debugging Tips

**Player falls through the floor:**
- Check that the floor brush has at least 4 non-coplanar vertices. A perfectly flat brush (all vertices coplanar) may cause `ConvexHullShapeSettings::Create` to fail. The fallback AABB will still work, but check the console for error messages.
- Verify that the coordinate conversion matches between rendering and collision. If the floor renders at Y=0 but the collision hull is at Z=0, the player falls through.
- Ensure `createMapCollision` is called before `initPlayerCharacter`. The player's `CharacterVirtual` needs static bodies to exist so it lands on something.

**Player gets stuck in walls:**
- Check for degenerate brushes -- extremely thin geometry (less than 0.1 units thick) can cause the convex hull to collapse below Jolt's minimum convex radius. The AABB fallback handles this, but you may need to thicken the brush in TrenchBroom.
- Check for overlapping brushes that create a "pinch point" where two collision hulls press the player from both sides.

**CharacterVirtual does not collide with anything:**
- Check the collision layer filters. `CharacterVirtual::ExtendedUpdate` in `playerCharacterSystem` uses `Layers::MOVING` for its broad-phase and layer filters. The `NON_MOVING` layer collides with `MOVING` (see `ObjectLayerPairFilterImpl` in `jolt_setup.h`), so static brush bodies should block the player.
- Verify that `OptimizeBroadPhase` is called after creating static bodies. Without this, newly added bodies may not appear in broad-phase queries.

**Doors/lifts don't push the player:**
- Verify the door/lift entity has a `JoltBody` component (check with `registry.all_of<JoltBody>(entity)`).
- Verify the body is kinematic (`EMotionType::Kinematic`), not static.
- Verify `moverSyncSystem` runs after `moverSystem` in the tick order (it does -- check `main.cpp`).
- Verify `MoveKinematic` is being called with a non-zero delta time. If `dt` is 0, Jolt cannot compute velocity and the body does not sweep.

### Debug Visualisation

If you implemented the debug wireframe rendering from earlier chapters, you can temporarily add wireframe cubes at each brush body's AABB to visualise collision. For convex hulls, you would need to extract the hull vertices from Jolt and draw them as lines -- that is a useful exercise but beyond this chapter's scope.

A simpler debugging approach: temporarily print the centroid and extents of each created body:

```cpp
// After creating each worldspawn body:
JPH::AABox bounds = shape->GetLocalBounds();
JPH::Vec3 center = bounds.GetCenter();
JPH::Vec3 extent = bounds.GetExtent();
std::cout << "  Brush body at ("
          << center.GetX() << ", " << center.GetY() << ", " << center.GetZ()
          << ") extents ("
          << extent.GetX() << ", " << extent.GetY() << ", " << extent.GetZ()
          << ")" << std::endl;
```

---

## Step 9: Update CMakeLists.txt

No new source files are needed if you added the new functions to the existing `jolt_body_helpers.cpp`. The file is already in `CMakeLists.txt`:

```cmake
src/engine/ecs/jolt_body_helpers.cpp
```

However, you do need to ensure that the `ConvexHullShape` header is available. Add the include to `jolt_setup.h` alongside the existing shape includes:

```cpp
// In jolt_setup.h — add this include
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
```

The existing includes in `jolt_setup.h` already have `BoxShape.h` and `CapsuleShape.h`. Adding `ConvexHullShape.h` makes the type available everywhere that includes `jolt_setup.h` (which is everything that includes `jolt_world.h`).

If you chose to put the map collision functions in a separate file (e.g., `map_collision_helpers.h/.cpp`), add the new `.cpp` file to `CMakeLists.txt`:

```cmake
add_executable(QEngine
    # ... existing files ...
    src/engine/ecs/jolt_body_helpers.cpp
    src/engine/ecs/map_collision_helpers.cpp   # Only if you created a separate file
    # ... rest of files ...
)
```

---

## What Changed -- Summary

| File | Change |
|------|--------|
| `jolt_body_helpers.h` | Added declarations: `createBrushShape`, `createMapCollision`, `createBrushEntityBody`, `createBrushSensorBody` |
| `jolt_body_helpers.cpp` | Added 4 new functions for brush-based collision creation |
| `jolt_setup.h` | Added `#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>` |
| `main.cpp` | Replaced `createLevelBodies(registry, level)` with `createMapCollision(registry, mapFile)`. Updated initialization order |
| Entity spawning code | `spawnDoor` / `spawnLift` now call `createBrushEntityBody`. `spawnTrigger` now calls `createBrushSensorBody` |

### Functions Added

| Function | Purpose |
|----------|---------|
| `createBrushShape(brush)` | Builds a `ConvexHullShapeRefC` from a single brush's vertices. Falls back to AABB box on failure |
| `createMapCollision(registry, mapFile)` | Creates static bodies for all worldspawn brushes |
| `createBrushEntityBody(registry, entity, brush, isKinematic)` | Creates a kinematic or static body for a brush entity (door, lift, decoration) |
| `createBrushSensorBody(registry, entity, brush)` | Creates a sensor body from a brush's AABB for trigger detection |

### Functions Deprecated (Not Deleted)

| Function | Status |
|----------|--------|
| `createLevelBodies(registry, level)` | No longer called. Remains in source as reference for the old level system |

---

## What You Should See

After building and running:

1. **Player collides with all walls, floor, and ceiling** -- no walking through geometry, no falling through the world
2. **Ramps and angled surfaces work correctly** -- the convex hull matches the actual brush shape, so the player slides smoothly along slopes
3. **Doors physically interact with the player** -- when a door closes, it pushes the player out of the way (kinematic body + `MoveKinematic`)
4. **Lifts carry the player upward** -- stepping onto a lift trigger starts the mover, and the kinematic body sweeps the player up with it
5. **Trigger volumes activate on entry** -- walking into a `trigger_hurt` brush deals damage, walking into a `trigger_once` brush activates its target mover
6. **Console output shows collision statistics** -- "Worldspawn: N brush bodies created" confirms that collision geometry was generated

---

## What's Next

The level loads from TrenchBroom, renders correctly, spawns entities, and has solid collision. In **Chapter 20: TrenchBroom Config & Final Level**, we will set up TrenchBroom to work seamlessly with QEngine -- a `GameConfig.cfg` so TrenchBroom shows QEngine textures and entities natively, an `.fgd` file defining all available entity types, and a complete playable level authored entirely in the editor.
