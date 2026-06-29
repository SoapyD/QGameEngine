# Chapter 20: TrenchBroom Config & Final Level

## What You'll Learn
- Creating a TrenchBroom game configuration so the editor recognizes QEngine
- Organizing textures and assets for TrenchBroom's texture browser
- Installing the game config and setting up a new map project
- Mapping texture names from .map files to engine file paths
- Hot-reloading maps during development for rapid iteration
- Building a complete multi-room playable level in TrenchBroom
- Verifying the full pipeline from editor to running engine
- Archiving the old hardcoded level code

---

## Step 1: TrenchBroom Game Configuration

TrenchBroom is a generic brush-based editor. Out of the box, it knows about Quake, Half-Life, and a handful of other engines, but it does not know about QEngine. A **game configuration file** teaches TrenchBroom everything it needs: where to find textures, what entity types exist, what file format to use, and how to tag certain brushes for special rendering.

### The GameConfig.cfg format

TrenchBroom reads game configs as JSON files. The format has evolved over several versions -- we use version 9, which is the current stable format. Each field tells TrenchBroom something specific about your engine.

### Create the file

Create the directory `tb/` in your project root, then create `tb/GameConfig.cfg`:

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

### Field-by-field explanation

| Field | Value | What It Does |
|-------|-------|-------------|
| `version` | `9` | Tells TrenchBroom which config schema to expect. Version 9 is the current format. |
| `name` | `"QEngine"` | The name that appears in TrenchBroom's game selection list when creating a new map. |
| `icon` | `"Icon.png"` | Optional 32x32 PNG shown next to the game name. If the file does not exist, TrenchBroom uses a default icon. |
| `fileformats` | `[{ "format": "Standard" }]` | The .map file format variant. "Standard" is the original Quake format with 3-point plane definitions. This is what our Chapter 17 parser reads. |
| `filesystem.searchpath` | `"assets"` | The root directory for all game content, relative to the project root. TrenchBroom appends this to your game path when searching for files. |
| `textures.root` | `"textures"` | The texture directory, relative to the searchpath. So TrenchBroom looks in `<gamepath>/assets/textures/`. |
| `textures.extensions` | `[".png", ".jpg", ".tga"]` | Which image file extensions TrenchBroom should recognize as textures. |
| `textures.attribute` | `"_tb_textures"` | A worldspawn key that TrenchBroom uses internally to track texture collections. You generally do not need to touch this. |
| `entities.definitions` | `["QEngine.fgd"]` | Path to the FGD file, relative to the game config directory. TrenchBroom loads this to populate the entity browser. |
| `entities.defaultcolor` | `"0.6 0.6 0.6 1.0"` | RGBA colour for entities that don't specify one in the FGD. A neutral grey. |
| `tags.brush` | Trigger tag | Tags are visual hints in the editor. This tag makes any brush entity whose classname matches `trigger_*` render as transparent, so you can see through trigger volumes while editing. |

> **Why "Standard" format?** Quake's original .map format is the simplest and most widely understood. It uses three points per plane, texture name, and five numbers for UV alignment. More advanced formats (Valve 220, Quake 3) add features like independent UV axes, but our parser from Chapter 17 handles the Standard format, so we match the config to the parser.

---

## Step 2: Directory Structure for TrenchBroom

TrenchBroom discovers textures by combining the **game path** (set in preferences) with the **searchpath** and **texture root** from the config. If your game path is `C:/Projects/QEngine/`, TrenchBroom looks for textures in `C:/Projects/QEngine/assets/textures/`.

### Set up the directory structure

Organize your project like this:

```
QEngine/
├── tb/
│   ├── GameConfig.cfg        <-- TrenchBroom game config (Step 1)
│   └── Icon.png              <-- Optional 32x32 game icon
├── assets/
│   ├── QEngine.fgd           <-- Entity definitions (from Ch 18)
│   ├── textures/
│   │   ├── base/
│   │   │   ├── floor1.png
│   │   │   ├── wall1.png
│   │   │   ├── ceiling1.png
│   │   │   └── trim1.png
│   │   ├── tech/
│   │   │   ├── metal1.png
│   │   │   ├── panel1.png
│   │   │   └── grate1.png
│   │   └── dev/
│   │       ├── grid_orange.png
│   │       ├── grid_grey.png
│   │       └── grid_white.png
│   ├── maps/
│   │   ├── test.map          <-- Quick test maps
│   │   └── e1m1.map          <-- The final level (Step 6)
│   └── shaders/
│       └── ...               <-- Existing shaders
├── src/
│   └── ...
└── CMakeLists.txt
```

### How TrenchBroom resolves texture paths

When you apply a texture in TrenchBroom, it writes a **texture name** into the .map file. This name is the path relative to the texture root, without the file extension. For example:

| File on disk | Texture name in .map file |
|-------------|--------------------------|
| `assets/textures/base/floor1.png` | `base/floor1` |
| `assets/textures/tech/metal1.png` | `tech/metal1` |
| `assets/textures/dev/grid_orange.png` | `dev/grid_orange` |

Your engine's texture loader must reverse this mapping: take the texture name from the .map file, prepend the texture path, and append the extension.

### Where does the FGD go?

The FGD file goes in `assets/` (inside the searchpath). When you install the game config into TrenchBroom's game directory (Step 3), TrenchBroom finds the FGD by combining the game path + the definition path from the config. Since our config says `"definitions": ["QEngine.fgd"]` and the searchpath is `"assets"`, TrenchBroom expects the FGD at `<gamepath>/QEngine.fgd`. However, TrenchBroom also searches in the game config directory itself. The simplest approach: put a copy of the FGD in both `assets/` (for your engine) and alongside the GameConfig.cfg in TrenchBroom's games directory (for the editor).

### Recommended prototype textures

For development and testing, you want simple, grid-aligned textures that make it easy to spot alignment issues, scale problems, and UV seams. **Kenney.nl** provides free CC0 prototype textures:

- Visit https://kenney.nl/assets/prototype-textures
- Download the pack and extract the PNG files you want into `assets/textures/dev/`
- These textures have grid lines, colour coding, and embedded scale indicators

Alternatively, create your own with any image editor: a 256x256 PNG with a visible grid pattern and a distinct colour. The key properties are:

1. **Power-of-two dimensions** (128, 256, 512) -- ensures clean mipmapping
2. **Visible grid lines** -- makes it obvious when textures are misaligned
3. **Distinct colours per category** -- orange for floors, grey for walls, white for ceilings

---

## Step 3: Installing the Game Config

TrenchBroom stores game configs in a platform-specific directory. You need to copy your GameConfig.cfg (and the FGD) there so TrenchBroom knows QEngine exists.

### Step 3a: Find TrenchBroom's game config directory

| Platform | Path |
|----------|------|
| Windows | `%APPDATA%/TrenchBroom/games/QEngine/` |
| Linux | `~/.TrenchBroom/games/QEngine/` |
| macOS | `~/Library/Application Support/TrenchBroom/games/QEngine/` |

On Windows, `%APPDATA%` typically expands to `C:\Users\<YourName>\AppData\Roaming`. You can paste `%APPDATA%\TrenchBroom\games\` into the File Explorer address bar to navigate directly.

### Step 3b: Create the QEngine directory and copy files

Create the `QEngine/` folder inside the `games/` directory, then copy:

```
TrenchBroom/games/QEngine/
├── GameConfig.cfg    <-- from tb/GameConfig.cfg
├── QEngine.fgd       <-- from assets/QEngine.fgd
└── Icon.png          <-- optional, from tb/Icon.png
```

The FGD must be alongside the GameConfig.cfg because the config references it by relative path (`"definitions": ["QEngine.fgd"]`).

### Step 3c: Launch TrenchBroom and create a new map

1. Open TrenchBroom
2. Go to **File > New Map**
3. In the game selection dialog, you should see **"QEngine"** in the list. Select it.
4. Choose **Standard** as the map format (the only option, matching our config).
5. Click **OK**

You should now see an empty map with QEngine selected as the game.

### Step 3d: Set the game path

TrenchBroom needs to know where your QEngine project lives on disk so it can find textures and the FGD.

1. Go to **View > Preferences** (or TrenchBroom > Preferences on macOS)
2. Select **QEngine** in the left panel
3. Under **Game Path**, click the browse button and navigate to your QEngine project root (the directory containing `assets/`, `src/`, `CMakeLists.txt`)
4. Click **Apply** and close Preferences

### Step 3e: Verify textures

1. In the editor, look at the **Texture Browser** panel (usually on the right side, or access via View > Texture Browser)
2. You should see your texture categories: `base/`, `tech/`, `dev/`
3. Click on any category to expand it and see the texture thumbnails

If textures don't appear, check:
- The game path points to the correct directory
- The `assets/textures/` directory exists and contains image files
- The image files have one of the extensions listed in the config (`.png`, `.jpg`, `.tga`)

### Step 3f: Verify entities

1. Right-click in the 3D viewport
2. Select **Create Point Entity**
3. You should see all entities from your FGD: `info_player_start`, `light`, `item_health`, `item_ammo_shells`, `monster_grunt`, etc.
4. Right-click again and select **Create Brush Entity**
5. You should see brush entities: `func_door`, `trigger_once`, `trigger_multiple`, etc.

If entities don't appear, verify that `QEngine.fgd` is present in both:
- The TrenchBroom games directory alongside `GameConfig.cfg`
- Your project's `assets/` directory

---

## Step 4: Texture Workflow

When TrenchBroom writes a texture name like `base/floor1` into the .map file, our engine needs to find the actual image file on disk. The Chapter 17 map parser already extracts texture names from brush faces -- now we need to connect those names to file loading.

### The texture name mapping

The mapping rule is straightforward:

```
texture name in .map  -->  assets/textures/<name>.<ext>
```

For example, `base/floor1` becomes `assets/textures/base/floor1.png`. If the `.png` does not exist, try `.jpg`, then `.tga`. If none exist, fall back to a checkerboard pattern.

### Update the texture loading code

In the map renderer code from Chapter 17, update the texture loading function to handle TrenchBroom texture paths. If you have a `MapRenderer` or `BrushMeshBuilder` class, find where textures are loaded and update it.

**New file: `src/engine/level/texture_loader.h`**

```cpp
#pragma once

#include <string>
#include <unordered_map>
#include <glad/glad.h>

class TextureLoader
{
public:
    // Load a texture by its .map name (e.g. "base/floor1")
    // Returns the OpenGL texture ID, or the fallback texture if not found
    unsigned int loadMapTexture(const std::string& textureName);

    // Get or create the checkerboard fallback texture
    unsigned int getFallbackTexture();

    // Clear all cached textures (call when reloading a map)
    void clearCache();

private:
    unsigned int createFallbackTexture();
    unsigned int loadImageFile(const std::string& path);

    std::unordered_map<std::string, unsigned int> m_cache;
    unsigned int m_fallbackTexture = 0;

    // Base path for textures, relative to working directory
    std::string m_basePath = "assets/textures/";

    // Extensions to try, in order of preference
    static constexpr const char* EXTENSIONS[] = { ".png", ".jpg", ".tga" };
    static constexpr int NUM_EXTENSIONS = 3;
};
```

**New file: `src/engine/level/texture_loader.cpp`**

```cpp
#include "engine/level/texture_loader.h"

#include <stb_image.h>
#include <iostream>
#include <filesystem>

unsigned int TextureLoader::loadMapTexture(const std::string& textureName)
{
    // Check cache first
    auto it = m_cache.find(textureName);
    if (it != m_cache.end())
        return it->second;

    // Try each extension
    for (int i = 0; i < NUM_EXTENSIONS; ++i)
    {
        std::string fullPath = m_basePath + textureName + EXTENSIONS[i];

        if (std::filesystem::exists(fullPath))
        {
            unsigned int texId = loadImageFile(fullPath);
            if (texId != 0)
            {
                m_cache[textureName] = texId;
                return texId;
            }
        }
    }

    // Not found — use fallback
    std::cerr << "Warning: texture '" << textureName
              << "' not found, using fallback\n";

    unsigned int fallback = getFallbackTexture();
    m_cache[textureName] = fallback;
    return fallback;
}

unsigned int TextureLoader::getFallbackTexture()
{
    if (m_fallbackTexture == 0)
        m_fallbackTexture = createFallbackTexture();
    return m_fallbackTexture;
}

void TextureLoader::clearCache()
{
    // Don't delete the fallback — it persists across reloads
    for (auto& [name, texId] : m_cache)
    {
        if (texId != m_fallbackTexture)
            glDeleteTextures(1, &texId);
    }
    m_cache.clear();
}

unsigned int TextureLoader::createFallbackTexture()
{
    // 8x8 magenta-black checkerboard — immediately visible as "missing"
    constexpr int SIZE = 8;
    unsigned char pixels[SIZE * SIZE * 3];

    for (int y = 0; y < SIZE; ++y)
    {
        for (int x = 0; x < SIZE; ++x)
        {
            int idx = (y * SIZE + x) * 3;
            bool checker = ((x + y) % 2 == 0);
            pixels[idx + 0] = checker ? 255 : 0;   // R
            pixels[idx + 1] = 0;                     // G
            pixels[idx + 2] = checker ? 255 : 0;    // B
        }
    }

    unsigned int texId;
    glGenTextures(1, &texId);
    glBindTexture(GL_TEXTURE_2D, texId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, SIZE, SIZE, 0,
                 GL_RGB, GL_UNSIGNED_BYTE, pixels);

    // Nearest filtering so the checkerboard stays crisp
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    return texId;
}

unsigned int TextureLoader::loadImageFile(const std::string& path)
{
    stbi_set_flip_vertically_on_load(true);

    int width, height, channels;
    unsigned char* data = stbi_load(path.c_str(), &width, &height,
                                     &channels, 0);
    if (!data)
    {
        std::cerr << "Failed to load image: " << path << "\n";
        return 0;
    }

    GLenum format = GL_RGB;
    if (channels == 4) format = GL_RGBA;
    else if (channels == 1) format = GL_RED;

    unsigned int texId;
    glGenTextures(1, &texId);
    glBindTexture(GL_TEXTURE_2D, texId);
    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0,
                 format, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    stbi_image_free(data);
    return texId;
}
```

### Integrate with the map renderer

In your brush mesh builder (from Chapter 17), replace any existing texture loading with the `TextureLoader`. Where you previously loaded textures by full path, use the texture name from the .map file:

```cpp
// Before (hardcoded path):
// unsigned int texId = loadTexture("assets/textures/wall.png");

// After (using TextureLoader):
unsigned int texId = textureLoader.loadMapTexture(face.textureName);
```

The `TextureLoader` handles extension resolution, caching, and fallback automatically.

### Adding new textures

The workflow for adding textures to your project:

1. Create or download the image file (PNG recommended, power-of-two dimensions)
2. Drop it into `assets/textures/<category>/` (e.g. `assets/textures/base/brick1.png`)
3. In TrenchBroom, click the refresh button in the Texture Browser (or restart the editor)
4. The texture appears as `base/brick1` and is ready to use
5. When you save the .map and load it in QEngine, the engine finds `assets/textures/base/brick1.png` automatically

> **Why categories?** Subdirectories under `textures/` serve as categories in TrenchBroom's texture browser. This makes it easy to find textures when your collection grows. Common categories: `base/` (generic stone/concrete), `tech/` (metal/sci-fi), `nature/` (grass/rock/wood), `dev/` (prototype grids), `liquid/` (water/lava).

---

## Step 5: Hot Reload (Optional Enhancement)

Switching between TrenchBroom and QEngine to test every change is tedious. A hot reload system watches the .map file for changes and automatically reloads everything when you save in the editor. This turns your iteration loop from "save, alt-tab, close engine, rebuild, relaunch" into "save, wait one second, see changes."

### The approach

The simplest hot reload uses `std::filesystem::last_write_time`. Each frame (or every N frames, to avoid excessive disk access), check if the .map file's modification time has changed. If it has, tear down the current level and rebuild everything from the file.

### New file: `src/engine/level/map_hot_reload.h`

```cpp
#pragma once

#include <string>
#include <filesystem>
#include <entt/entt.hpp>

class MapHotReload
{
public:
    // Initialize with the path to the .map file
    void init(const std::string& mapPath);

    // Call each frame. Returns true if the map was reloaded.
    bool checkAndReload(entt::registry& registry);

private:
    std::string m_mapPath;
    std::filesystem::file_time_type m_lastWriteTime;
    int m_frameCounter = 0;

    // Only check every N frames to avoid hammering the filesystem
    static constexpr int CHECK_INTERVAL = 30; // ~0.5 sec at 60fps
};
```

### New file: `src/engine/level/map_hot_reload.cpp`

```cpp
#include "engine/level/map_hot_reload.h"
#include "engine/level/map_loader.h"
#include "engine/level/brush_mesh_builder.h"
#include "engine/level/texture_loader.h"
#include "engine/ecs/entity_spawner.h"
#include "engine/ecs/jolt_body_helpers.h"
#include "engine/ecs/components.h"
#include "engine/physics/jolt_world.h"

#include <iostream>

void MapHotReload::init(const std::string& mapPath)
{
    m_mapPath = mapPath;

    if (std::filesystem::exists(mapPath))
    {
        m_lastWriteTime = std::filesystem::last_write_time(mapPath);
    }
}

bool MapHotReload::checkAndReload(entt::registry& registry)
{
    // Throttle filesystem checks
    if (++m_frameCounter < CHECK_INTERVAL)
        return false;
    m_frameCounter = 0;

    // Check if the file has been modified
    if (!std::filesystem::exists(m_mapPath))
        return false;

    auto currentWriteTime = std::filesystem::last_write_time(m_mapPath);
    if (currentWriteTime == m_lastWriteTime)
        return false;

    m_lastWriteTime = currentWriteTime;

    std::cout << "[HotReload] Map file changed, reloading...\n";

    // --- Phase 1: Remember the player state ---
    glm::vec3 playerPos{0.0f};
    float playerHealth = 100.0f;
    float playerMaxHealth = 100.0f;

    auto playerView = registry.view<TagPlayer, Position, Health>();
    for (auto [entity, pos, health] : playerView.each())
    {
        playerPos = pos.value;
        playerHealth = health.current;
        playerMaxHealth = health.max;
    }

    // --- Phase 2: Destroy all non-player entities ---
    // Collect entities to destroy (can't destroy while iterating)
    std::vector<entt::entity> toDestroy;
    registry.each([&](entt::entity entity)
    {
        if (!registry.all_of<TagPlayer>(entity))
        {
            // Remove Jolt bodies before destroying the entity
            if (registry.all_of<JoltBody>(entity))
            {
                auto& joltWorld = registry.ctx().get<JoltWorld>();
                auto& body = registry.get<JoltBody>(entity);
                joltWorld.getBodyInterface().RemoveBody(body.id);
                joltWorld.getBodyInterface().DestroyBody(body.id);
            }
            toDestroy.push_back(entity);
        }
    });

    for (auto entity : toDestroy)
    {
        registry.destroy(entity);
    }

    // --- Phase 3: Re-parse the .map file ---
    MapLoader loader;
    auto mapData = loader.load(m_mapPath);
    if (!mapData.has_value())
    {
        std::cerr << "[HotReload] Failed to parse map file!\n";
        return false;
    }

    // --- Phase 4: Rebuild meshes ---
    BrushMeshBuilder meshBuilder;
    TextureLoader& texLoader = registry.ctx().get<TextureLoader>();
    texLoader.clearCache();

    meshBuilder.buildFromMap(registry, mapData.value(), texLoader);

    // --- Phase 5: Spawn entities from the map ---
    EntitySpawner spawner;
    spawner.spawnAllEntities(registry, mapData.value());

    // --- Phase 6: Create collision bodies ---
    createBrushCollisionBodies(registry, mapData.value());

    // --- Phase 7: Restore player position ---
    // (Don't teleport to spawn point on reload — keep current position)
    for (auto [entity, pos, health] : playerView.each())
    {
        pos.value = playerPos;
        health.current = playerHealth;
        health.max = playerMaxHealth;
    }

    std::cout << "[HotReload] Map reloaded successfully.\n";
    return true;
}
```

### Important notes about the hot reload implementation

The function names (`MapLoader::load`, `BrushMeshBuilder::buildFromMap`, `EntitySpawner::spawnAllEntities`, `createBrushCollisionBodies`) correspond to the systems you built in Chapters 17-19. Your actual function signatures may differ -- adapt the hot reload code to match your implementation.

The key steps in the reload sequence are:

1. **Save the player state** -- position, health, and any other state you want to preserve across reloads
2. **Destroy all non-player entities** -- this includes level geometry, lights, pickups, enemies, doors, lifts, triggers, and their Jolt bodies
3. **Re-parse the .map file** -- read the file again from disk
4. **Rebuild render meshes** -- convert brush geometry to VAOs
5. **Re-spawn entities** -- create ECS entities from map entity definitions
6. **Recreate collision** -- build Jolt static bodies from brush planes
7. **Restore the player** -- put them back where they were, not at the spawn point

### Wire hot reload into the game loop

In `main.cpp`, create the hot reload instance and check it each frame:

```cpp
#include "engine/level/map_hot_reload.h"

// After loading the initial map:
MapHotReload hotReload;
hotReload.init("assets/maps/e1m1.map");

// In the main loop, before the fixed timestep:
if (hotReload.checkAndReload(registry))
{
    // Map was reloaded — any one-time setup can go here
}
```

> **Why not watch the file with OS notifications?** Operating system file watchers (`inotify`, `ReadDirectoryChangesW`, `FSEvents`) are more efficient than polling, but they add platform-specific complexity. For a development tool, polling every half-second is perfectly adequate and works identically on every platform.

> **Is hot reload safe for the final game?** Hot reload is a development convenience. In a shipped game, you would load the map once at level start and never poll for changes. You can guard the hot reload behind a `#ifdef QENGINE_DEV` preprocessor flag, or simply not create the `MapHotReload` instance in release builds.

---

## Step 6: Building the Final Level

This is where everything comes together. You will build a complete, playable level in TrenchBroom that exercises every feature QEngine supports: rendering, lighting, collision, physics, doors, lifts, triggers, pickups, weapons, and the HUD.

### Level overview

The level has five rooms connected by corridors, each showcasing different engine capabilities:

```
                 ┌──────────┐
                 │  Room 3  │  (Elevated area)
                 │  Bridge  │
                 └────┬─────┘
                      │ stairs/lift
     ┌────────────────┼────────────────┐
     │                │                │
     │             Room 2              │
     │           Main Hall             │
     │                                 │
     └───┬────────────────────────┬────┘
         │                        │
    ┌────┴─────┐            ┌─────┴────┐
    │  Room 1  │            │  Room 4  │
    │  Spawn   │            │  Danger  │
    └──────────┘            └──────────┘
         │
    ┌────┴─────┐
    │  Room 5  │
    │  Arena   │
    └──────────┘
```

### Room 1: Spawn Room

A small, well-lit room where the player begins. This room establishes the visual tone and lets the player orient themselves before encountering any challenges.

**In TrenchBroom:**

1. Create a box brush: 256 x 256 x 192 units (width x depth x height)
2. Select the box and use **Edit > Make Hollow** with thickness 16 to create walls, floor, and ceiling
3. Apply textures: `base/floor1` on the floor, `base/wall1` on walls, `base/ceiling1` on the ceiling
4. Place an `info_player_start` entity at the centre, 32 units above the floor (so the player spawns standing, not embedded in the floor)
5. Place two `light` entities near the ceiling corners:
   - Brightness (`light` key): 200
   - Colour: warm white `255 240 220`
6. Create a doorway in the wall facing Room 2: delete or resize one wall brush to leave a 64x112 opening (64 wide, 112 tall -- slightly larger than the player)
7. Add a `func_door` brush entity in the doorway:
   - Create a brush that fills the opening (64 x 16 x 112)
   - Select it, right-click > Create Brush Entity > `func_door`
   - Set `speed` to 200, `wait` to 3, `angle` to 90 (opens upward or sideways depending on your door implementation)

### Room 2: Main Hall

The central hub connecting all other rooms. Large and open with colourful lighting.

**In TrenchBroom:**

1. Create a large room: 512 x 512 x 256 units, hollowed with thickness 16
2. Connect to Room 1 via a short corridor (128 x 64 x 128, just a few brushes forming walls/floor/ceiling between the rooms)
3. Place 4 `light` entities at different positions:
   - Centre ceiling: brightness 300, colour white `255 255 255`
   - North wall: brightness 150, colour blue `100 150 255`
   - South wall: brightness 150, colour warm `255 200 100`
   - East wall: brightness 150, colour green `100 255 150`
4. Scatter pickups:
   - 2x `item_health` (amount 25) -- one near each exit
   - 1x `item_ammo_shells` (amount 20) -- centre of the room
5. Add openings to Room 3 (north), Room 4 (east), and Room 5 (south, through a corridor)

### Room 3: Elevated Area

An upper platform accessible via stairs from Room 2, or via a lift. Overlooks the Main Hall through an opening in the wall.

**In TrenchBroom:**

1. Create the room: 384 x 256 x 192 units, hollowed with thickness 16
2. Position it so its floor is 128 units above Room 2's floor
3. Build stairs connecting Room 2 to Room 3:
   - 8 steps, each 16 units tall and 32 units deep
   - Each step is a separate brush: the first is 16 units tall, the second is 32 units tall, etc. (each step extends down to the floor, not floating)
   - Width: 96 units
4. Add a lift as an alternative way up:
   - Create a 64 x 64 x 8 brush (the platform)
   - Make it a `func_door` with properties:
     - `speed`: 100
     - `wait`: 3
     - `angle`: -1 (Quake convention for "up")
   - Place a `trigger_once` or `trigger_multiple` volume at the base of the lift shaft to activate it
5. Place a `light` (brightness 200) on the ceiling
6. Cut an opening in the south wall so you can look down into Room 2

### Room 4: Danger Room

A hazardous room with a lava floor and a narrow safe path. Tests trigger volumes and damage systems.

**In TrenchBroom:**

1. Create the room: 384 x 384 x 192 units, hollowed with thickness 16
2. Connect to Room 2 via a corridor (128 x 64 x 128)
3. Create the lava floor:
   - The floor brush stays as normal geometry (rendered with a lava/orange texture like `dev/grid_orange`)
   - Create a `trigger_hurt` brush entity covering the entire floor area, 32 units tall
   - This is a brush entity with classname `trigger_hurt` (or `trigger_multiple` with `TriggerAction::Damage`)
   - Set the damage value (e.g. `damage` key = 10 for 10 damage per second)
4. Build the safe path:
   - Create a series of narrow platform brushes (32 x 32 x 8) raised 16 units above the floor
   - Space them so the player can jump between them (64-96 units apart)
   - These are worldspawn brushes (regular geometry), so they have collision automatically
5. Place an `item_health` (amount 50) on the last platform as a reward
6. Add 2 `light` entities with red-orange colour `255 100 50` for atmosphere

### Room 5: Arena

A large combat space connected to Room 1 (the spawn room wraps around). This is where the player tests weapons against enemies and can teleport back to the start.

**In TrenchBroom:**

1. Create a large room: 640 x 640 x 320 units, hollowed with thickness 16
2. Connect to Room 2 via a corridor from the south
3. Place enemies (if implemented in your FGD):
   - 3x `monster_grunt` at different positions around the room
   - Give them varied facing angles so they don't all look the same direction
4. Scatter pickups:
   - 2x `item_health` (amount 25) -- at opposite corners
   - 2x `item_ammo_shells` (amount 30) -- near the centre
5. Create a teleporter back to Room 1:
   - Build a 64 x 64 x 128 alcove in one corner
   - Place a `trigger_teleport` brush entity filling the alcove (or a `trigger_once` with `TriggerAction::Teleport`)
   - Set the destination to the spawn room coordinates
   - Optionally, add a distinctive texture on the teleporter walls (e.g. `dev/grid_white`) so the player knows it is special
6. Place 4 `light` entities:
   - High ceiling light: brightness 400, white
   - Corner accent lights: brightness 100, varied colours

### Building tips

When building in TrenchBroom, keep these guidelines in mind:

- **Snap to grid**: Keep grid snapping on (default 16 units). This ensures brushes align cleanly and prevents micro-gaps that cause visual artifacts.
- **No overlapping worldspawn brushes**: Overlapping brushes are technically valid but can cause rendering artifacts. Brushes should meet at faces, not overlap.
- **Seal the level**: Every room must be fully enclosed by brushes. Any gap in the walls/floor/ceiling creates a "leak" -- the player can see into the void. TrenchBroom highlights leaks if you have a compile step, but for our engine, just make sure walls are thick enough and meet at edges.
- **Test incrementally**: After building each room, save the .map and load it in QEngine (or let hot reload pick it up) to check that everything looks right before moving on.

### The complete .map file

Below is a reference .map file for the final level. This is a simplified version that demonstrates the structure -- you should build your own in TrenchBroom for the full experience, but this file can be loaded directly if you want to test the pipeline immediately.

Save this as `assets/maps/e1m1.map`:

```
// QEngine Final Level — e1m1
// Built for Chapter 20: TrenchBroom Config & Final Level
// 5 rooms: spawn, main hall, elevated area, danger room, arena

// entity 0 — worldspawn
{
"classname" "worldspawn"

// ============================================================
// Room 1: Spawn Room (256 x 256 x 192, origin at 0,0,0)
// ============================================================

// Floor
{
( -128 -128 0 ) ( 128 -128 0 ) ( 128 128 0 ) base/floor1 0 0 0 1 1
( -128 128 -16 ) ( 128 128 -16 ) ( 128 -128 -16 ) base/floor1 0 0 0 1 1
( -128 -128 -16 ) ( -128 -128 0 ) ( 128 -128 0 ) base/floor1 0 0 0 1 1
( 128 -128 -16 ) ( 128 -128 0 ) ( 128 128 0 ) base/floor1 0 0 0 1 1
( 128 128 -16 ) ( 128 128 0 ) ( -128 128 0 ) base/floor1 0 0 0 1 1
( -128 128 -16 ) ( -128 128 0 ) ( -128 -128 0 ) base/floor1 0 0 0 1 1
}

// Ceiling
{
( -128 -128 192 ) ( -128 128 192 ) ( 128 128 192 ) base/ceiling1 0 0 0 1 1
( -128 128 176 ) ( -128 -128 176 ) ( 128 -128 176 ) base/ceiling1 0 0 0 1 1
( -128 -128 176 ) ( -128 -128 192 ) ( 128 -128 192 ) base/ceiling1 0 0 0 1 1
( 128 -128 176 ) ( 128 -128 192 ) ( 128 128 192 ) base/ceiling1 0 0 0 1 1
( 128 128 176 ) ( 128 128 192 ) ( -128 128 192 ) base/ceiling1 0 0 0 1 1
( -128 128 176 ) ( -128 128 192 ) ( -128 -128 192 ) base/ceiling1 0 0 0 1 1
}

// West wall (solid)
{
( -144 -128 0 ) ( -144 128 0 ) ( -144 128 192 ) base/wall1 0 0 0 1 1
( -128 -128 0 ) ( -128 -128 192 ) ( -128 128 192 ) base/wall1 0 0 0 1 1
( -144 -128 0 ) ( -128 -128 0 ) ( -128 -128 192 ) base/wall1 0 0 0 1 1
( -144 128 0 ) ( -144 128 192 ) ( -128 128 192 ) base/wall1 0 0 0 1 1
( -144 -128 0 ) ( -144 128 0 ) ( -128 128 0 ) base/wall1 0 0 0 1 1
( -144 -128 192 ) ( -128 -128 192 ) ( -128 128 192 ) base/wall1 0 0 0 1 1
}

// East wall (with doorway to Main Hall — split into 3 brushes around the opening)
// East wall: left of door
{
( 128 -128 0 ) ( 128 -128 192 ) ( 144 -128 192 ) base/wall1 0 0 0 1 1
( 128 -128 0 ) ( 144 -128 0 ) ( 144 -32 0 ) base/wall1 0 0 0 1 1
( 128 -128 192 ) ( 128 -32 192 ) ( 144 -32 192 ) base/wall1 0 0 0 1 1
( 128 -32 0 ) ( 128 -32 192 ) ( 144 -32 192 ) base/wall1 0 0 0 1 1
( 128 -128 0 ) ( 128 -128 192 ) ( 128 -32 192 ) base/wall1 0 0 0 1 1
( 144 -128 0 ) ( 144 -32 0 ) ( 144 -32 192 ) base/wall1 0 0 0 1 1
}

// East wall: right of door
{
( 128 32 0 ) ( 128 32 192 ) ( 144 32 192 ) base/wall1 0 0 0 1 1
( 128 128 0 ) ( 144 128 0 ) ( 144 32 0 ) base/wall1 0 0 0 1 1
( 128 128 192 ) ( 128 32 192 ) ( 144 32 192 ) base/wall1 0 0 0 1 1
( 128 128 0 ) ( 128 128 192 ) ( 144 128 192 ) base/wall1 0 0 0 1 1
( 128 32 0 ) ( 128 128 0 ) ( 128 128 192 ) base/wall1 0 0 0 1 1
( 144 32 0 ) ( 144 128 0 ) ( 144 128 192 ) base/wall1 0 0 0 1 1
}

// East wall: above door
{
( 128 -32 128 ) ( 128 -32 192 ) ( 144 -32 192 ) base/wall1 0 0 0 1 1
( 128 32 128 ) ( 144 32 128 ) ( 144 -32 128 ) base/wall1 0 0 0 1 1
( 128 32 192 ) ( 128 -32 192 ) ( 144 -32 192 ) base/wall1 0 0 0 1 1
( 128 32 128 ) ( 128 32 192 ) ( 144 32 192 ) base/wall1 0 0 0 1 1
( 128 -32 128 ) ( 128 32 128 ) ( 128 32 192 ) base/wall1 0 0 0 1 1
( 144 -32 128 ) ( 144 32 128 ) ( 144 32 192 ) base/wall1 0 0 0 1 1
}

// South wall
{
( -128 -144 0 ) ( 128 -144 0 ) ( 128 -144 192 ) base/wall1 0 0 0 1 1
( -128 -128 0 ) ( -128 -128 192 ) ( 128 -128 192 ) base/wall1 0 0 0 1 1
( -128 -144 0 ) ( -128 -128 0 ) ( -128 -128 192 ) base/wall1 0 0 0 1 1
( 128 -144 0 ) ( 128 -144 192 ) ( 128 -128 192 ) base/wall1 0 0 0 1 1
( -128 -144 0 ) ( 128 -144 0 ) ( 128 -128 0 ) base/wall1 0 0 0 1 1
( -128 -144 192 ) ( -128 -128 192 ) ( 128 -128 192 ) base/wall1 0 0 0 1 1
}

// North wall
{
( -128 128 0 ) ( -128 128 192 ) ( 128 128 192 ) base/wall1 0 0 0 1 1
( -128 144 0 ) ( 128 144 0 ) ( 128 144 192 ) base/wall1 0 0 0 1 1
( -128 128 0 ) ( -128 144 0 ) ( -128 144 192 ) base/wall1 0 0 0 1 1
( 128 128 0 ) ( 128 128 192 ) ( 128 144 192 ) base/wall1 0 0 0 1 1
( -128 128 0 ) ( 128 128 0 ) ( 128 144 0 ) base/wall1 0 0 0 1 1
( -128 128 192 ) ( 128 128 192 ) ( 128 144 192 ) base/wall1 0 0 0 1 1
}

// ============================================================
// Corridor: Spawn Room to Main Hall (between east wall and Main Hall)
// ============================================================

// Corridor floor
{
( 144 -32 0 ) ( 208 -32 0 ) ( 208 32 0 ) base/floor1 0 0 0 1 1
( 144 32 -16 ) ( 208 32 -16 ) ( 208 -32 -16 ) base/floor1 0 0 0 1 1
( 144 -32 -16 ) ( 144 -32 0 ) ( 208 -32 0 ) base/floor1 0 0 0 1 1
( 208 -32 -16 ) ( 208 -32 0 ) ( 208 32 0 ) base/floor1 0 0 0 1 1
( 208 32 -16 ) ( 208 32 0 ) ( 144 32 0 ) base/floor1 0 0 0 1 1
( 144 32 -16 ) ( 144 32 0 ) ( 144 -32 0 ) base/floor1 0 0 0 1 1
}

// Corridor ceiling
{
( 144 -32 128 ) ( 144 32 128 ) ( 208 32 128 ) base/ceiling1 0 0 0 1 1
( 144 32 144 ) ( 144 -32 144 ) ( 208 -32 144 ) base/ceiling1 0 0 0 1 1
( 144 -32 128 ) ( 144 -32 144 ) ( 208 -32 144 ) base/ceiling1 0 0 0 1 1
( 208 -32 128 ) ( 208 -32 144 ) ( 208 32 144 ) base/ceiling1 0 0 0 1 1
( 208 32 128 ) ( 208 32 144 ) ( 144 32 144 ) base/ceiling1 0 0 0 1 1
( 144 32 128 ) ( 144 32 144 ) ( 144 -32 144 ) base/ceiling1 0 0 0 1 1
}

// Corridor south wall
{
( 144 -32 0 ) ( 144 -32 128 ) ( 208 -32 128 ) base/wall1 0 0 0 1 1
( 144 -48 0 ) ( 208 -48 0 ) ( 208 -48 128 ) base/wall1 0 0 0 1 1
( 144 -48 0 ) ( 144 -32 0 ) ( 144 -32 128 ) base/wall1 0 0 0 1 1
( 208 -48 0 ) ( 208 -48 128 ) ( 208 -32 128 ) base/wall1 0 0 0 1 1
( 144 -48 0 ) ( 208 -48 0 ) ( 208 -32 0 ) base/wall1 0 0 0 1 1
( 144 -48 128 ) ( 144 -32 128 ) ( 208 -32 128 ) base/wall1 0 0 0 1 1
}

// Corridor north wall
{
( 144 32 0 ) ( 208 32 0 ) ( 208 32 128 ) base/wall1 0 0 0 1 1
( 144 48 0 ) ( 144 48 128 ) ( 208 48 128 ) base/wall1 0 0 0 1 1
( 144 32 0 ) ( 144 48 0 ) ( 144 48 128 ) base/wall1 0 0 0 1 1
( 208 32 0 ) ( 208 32 128 ) ( 208 48 128 ) base/wall1 0 0 0 1 1
( 144 32 0 ) ( 208 32 0 ) ( 208 48 0 ) base/wall1 0 0 0 1 1
( 144 32 128 ) ( 208 32 128 ) ( 208 48 128 ) base/wall1 0 0 0 1 1
}

// ============================================================
// Room 2: Main Hall (512 x 512 x 256, east of the corridor)
// Origin at (464, 0, 0) — so X spans 208 to 720
// ============================================================

// Main Hall Floor
{
( 208 -256 0 ) ( 720 -256 0 ) ( 720 256 0 ) base/floor1 0 0 0 1 1
( 208 256 -16 ) ( 720 256 -16 ) ( 720 -256 -16 ) base/floor1 0 0 0 1 1
( 208 -256 -16 ) ( 208 -256 0 ) ( 720 -256 0 ) base/floor1 0 0 0 1 1
( 720 -256 -16 ) ( 720 -256 0 ) ( 720 256 0 ) base/floor1 0 0 0 1 1
( 720 256 -16 ) ( 720 256 0 ) ( 208 256 0 ) base/floor1 0 0 0 1 1
( 208 256 -16 ) ( 208 256 0 ) ( 208 -256 0 ) base/floor1 0 0 0 1 1
}

// Main Hall Ceiling
{
( 208 -256 256 ) ( 208 256 256 ) ( 720 256 256 ) base/ceiling1 0 0 0 1 1
( 208 256 240 ) ( 208 -256 240 ) ( 720 -256 240 ) base/ceiling1 0 0 0 1 1
( 208 -256 240 ) ( 208 -256 256 ) ( 720 -256 256 ) base/ceiling1 0 0 0 1 1
( 720 -256 240 ) ( 720 -256 256 ) ( 720 256 256 ) base/ceiling1 0 0 0 1 1
( 720 256 240 ) ( 720 256 256 ) ( 208 256 256 ) base/ceiling1 0 0 0 1 1
( 208 256 240 ) ( 208 256 256 ) ( 208 -256 256 ) base/ceiling1 0 0 0 1 1
}

// Main Hall West wall (with opening for corridor to Room 1)
// Left of corridor opening
{
( 208 -256 0 ) ( 208 -256 256 ) ( 192 -256 256 ) base/wall1 0 0 0 1 1
( 208 -32 0 ) ( 192 -32 0 ) ( 192 -32 256 ) base/wall1 0 0 0 1 1
( 208 -256 256 ) ( 208 -32 256 ) ( 192 -32 256 ) base/wall1 0 0 0 1 1
( 208 -256 0 ) ( 192 -256 0 ) ( 192 -32 0 ) base/wall1 0 0 0 1 1
( 208 -256 0 ) ( 208 -256 256 ) ( 208 -32 256 ) base/wall1 0 0 0 1 1
( 192 -256 0 ) ( 192 -32 0 ) ( 192 -32 256 ) base/wall1 0 0 0 1 1
}

// Right of corridor opening
{
( 208 32 0 ) ( 192 32 0 ) ( 192 32 256 ) base/wall1 0 0 0 1 1
( 208 256 0 ) ( 208 256 256 ) ( 192 256 256 ) base/wall1 0 0 0 1 1
( 208 32 256 ) ( 208 256 256 ) ( 192 256 256 ) base/wall1 0 0 0 1 1
( 208 32 0 ) ( 192 32 0 ) ( 192 256 0 ) base/wall1 0 0 0 1 1
( 208 32 0 ) ( 208 256 0 ) ( 208 256 256 ) base/wall1 0 0 0 1 1
( 192 32 0 ) ( 192 256 0 ) ( 192 256 256 ) base/wall1 0 0 0 1 1
}

// Main Hall East wall
{
( 720 -256 0 ) ( 736 -256 0 ) ( 736 -256 256 ) base/wall1 0 0 0 1 1
( 720 256 0 ) ( 720 256 256 ) ( 736 256 256 ) base/wall1 0 0 0 1 1
( 720 -256 256 ) ( 720 256 256 ) ( 736 256 256 ) base/wall1 0 0 0 1 1
( 720 -256 0 ) ( 736 -256 0 ) ( 736 256 0 ) base/wall1 0 0 0 1 1
( 720 -256 0 ) ( 720 -256 256 ) ( 720 256 256 ) base/wall1 0 0 0 1 1
( 736 -256 0 ) ( 736 256 0 ) ( 736 256 256 ) base/wall1 0 0 0 1 1
}

// Main Hall South wall
{
( 208 -256 0 ) ( 208 -256 256 ) ( 720 -256 256 ) base/wall1 0 0 0 1 1
( 208 -272 0 ) ( 720 -272 0 ) ( 720 -272 256 ) base/wall1 0 0 0 1 1
( 208 -272 0 ) ( 208 -256 0 ) ( 208 -256 256 ) base/wall1 0 0 0 1 1
( 720 -272 0 ) ( 720 -272 256 ) ( 720 -256 256 ) base/wall1 0 0 0 1 1
( 208 -272 0 ) ( 720 -272 0 ) ( 720 -256 0 ) base/wall1 0 0 0 1 1
( 208 -272 256 ) ( 208 -256 256 ) ( 720 -256 256 ) base/wall1 0 0 0 1 1
}

// Main Hall North wall
{
( 208 256 0 ) ( 720 256 0 ) ( 720 256 256 ) base/wall1 0 0 0 1 1
( 208 272 0 ) ( 208 272 256 ) ( 720 272 256 ) base/wall1 0 0 0 1 1
( 208 256 0 ) ( 208 272 0 ) ( 208 272 256 ) base/wall1 0 0 0 1 1
( 720 256 0 ) ( 720 256 256 ) ( 720 272 256 ) base/wall1 0 0 0 1 1
( 208 256 0 ) ( 720 256 0 ) ( 720 272 0 ) base/wall1 0 0 0 1 1
( 208 256 256 ) ( 720 256 256 ) ( 720 272 256 ) base/wall1 0 0 0 1 1
}

// ============================================================
// Room 3: Elevated Area (384 x 256 x 192, above Room 2 north side)
// Floor at Y=128, origin shifted up by 128 units
// ============================================================

// Elevated room floor
{
( 272 128 128 ) ( 656 128 128 ) ( 656 384 128 ) tech/metal1 0 0 0 1 1
( 272 384 112 ) ( 656 384 112 ) ( 656 128 112 ) tech/metal1 0 0 0 1 1
( 272 128 112 ) ( 272 128 128 ) ( 656 128 128 ) tech/metal1 0 0 0 1 1
( 656 128 112 ) ( 656 128 128 ) ( 656 384 128 ) tech/metal1 0 0 0 1 1
( 656 384 112 ) ( 656 384 128 ) ( 272 384 128 ) tech/metal1 0 0 0 1 1
( 272 384 112 ) ( 272 384 128 ) ( 272 128 128 ) tech/metal1 0 0 0 1 1
}

// Elevated room ceiling
{
( 272 128 320 ) ( 272 384 320 ) ( 656 384 320 ) tech/metal1 0 0 0 1 1
( 272 384 304 ) ( 272 128 304 ) ( 656 128 304 ) tech/metal1 0 0 0 1 1
( 272 128 304 ) ( 272 128 320 ) ( 656 128 320 ) tech/metal1 0 0 0 1 1
( 656 128 304 ) ( 656 128 320 ) ( 656 384 320 ) tech/metal1 0 0 0 1 1
( 656 384 304 ) ( 656 384 320 ) ( 272 384 320 ) tech/metal1 0 0 0 1 1
( 272 384 304 ) ( 272 384 320 ) ( 272 128 320 ) tech/metal1 0 0 0 1 1
}

// Elevated room walls (simplified — 4 walls enclosing the elevated area)
// South wall (with opening looking down into Main Hall)
// Left section
{
( 272 128 128 ) ( 272 128 320 ) ( 400 128 320 ) tech/metal1 0 0 0 1 1
( 272 112 128 ) ( 400 112 128 ) ( 400 112 320 ) tech/metal1 0 0 0 1 1
( 272 112 128 ) ( 272 128 128 ) ( 272 128 320 ) tech/metal1 0 0 0 1 1
( 400 112 128 ) ( 400 112 320 ) ( 400 128 320 ) tech/metal1 0 0 0 1 1
( 272 112 128 ) ( 400 112 128 ) ( 400 128 128 ) tech/metal1 0 0 0 1 1
( 272 112 320 ) ( 272 128 320 ) ( 400 128 320 ) tech/metal1 0 0 0 1 1
}

// Right section
{
( 528 128 128 ) ( 528 128 320 ) ( 656 128 320 ) tech/metal1 0 0 0 1 1
( 528 112 128 ) ( 656 112 128 ) ( 656 112 320 ) tech/metal1 0 0 0 1 1
( 528 112 128 ) ( 528 128 128 ) ( 528 128 320 ) tech/metal1 0 0 0 1 1
( 656 112 128 ) ( 656 112 320 ) ( 656 128 320 ) tech/metal1 0 0 0 1 1
( 528 112 128 ) ( 656 112 128 ) ( 656 128 128 ) tech/metal1 0 0 0 1 1
( 528 112 320 ) ( 528 128 320 ) ( 656 128 320 ) tech/metal1 0 0 0 1 1
}

// North wall
{
( 272 384 128 ) ( 656 384 128 ) ( 656 384 320 ) tech/metal1 0 0 0 1 1
( 272 400 128 ) ( 272 400 320 ) ( 656 400 320 ) tech/metal1 0 0 0 1 1
( 272 384 128 ) ( 272 400 128 ) ( 272 400 320 ) tech/metal1 0 0 0 1 1
( 656 384 128 ) ( 656 384 320 ) ( 656 400 320 ) tech/metal1 0 0 0 1 1
( 272 384 128 ) ( 656 384 128 ) ( 656 400 128 ) tech/metal1 0 0 0 1 1
( 272 384 320 ) ( 656 384 320 ) ( 656 400 320 ) tech/metal1 0 0 0 1 1
}

// East wall
{
( 656 128 128 ) ( 656 128 320 ) ( 672 128 320 ) tech/metal1 0 0 0 1 1
( 656 384 128 ) ( 672 384 128 ) ( 672 384 320 ) tech/metal1 0 0 0 1 1
( 656 128 320 ) ( 656 384 320 ) ( 672 384 320 ) tech/metal1 0 0 0 1 1
( 656 128 128 ) ( 672 128 128 ) ( 672 384 128 ) tech/metal1 0 0 0 1 1
( 656 128 128 ) ( 656 128 320 ) ( 656 384 320 ) tech/metal1 0 0 0 1 1
( 672 128 128 ) ( 672 384 128 ) ( 672 384 320 ) tech/metal1 0 0 0 1 1
}

// West wall
{
( 256 128 128 ) ( 256 128 320 ) ( 272 128 320 ) tech/metal1 0 0 0 1 1
( 256 384 128 ) ( 272 384 128 ) ( 272 384 320 ) tech/metal1 0 0 0 1 1
( 256 128 320 ) ( 256 384 320 ) ( 272 384 320 ) tech/metal1 0 0 0 1 1
( 256 128 128 ) ( 272 128 128 ) ( 272 384 128 ) tech/metal1 0 0 0 1 1
( 256 128 128 ) ( 256 128 320 ) ( 256 384 320 ) tech/metal1 0 0 0 1 1
( 272 128 128 ) ( 272 384 128 ) ( 272 384 320 ) tech/metal1 0 0 0 1 1
}

// Stairs from Main Hall (Y=64) up to Elevated Area (Y=128, height=128)
// 8 steps, each 16 units tall, 32 units deep
// Step 1 (Y=64-96, height 0-16)
{
( 352 64 0 ) ( 448 64 0 ) ( 448 96 0 ) base/floor1 0 0 0 1 1
( 352 64 16 ) ( 352 96 16 ) ( 448 96 16 ) base/floor1 0 0 0 1 1
( 352 64 0 ) ( 352 64 16 ) ( 448 64 16 ) base/floor1 0 0 0 1 1
( 448 64 0 ) ( 448 96 0 ) ( 448 96 16 ) base/floor1 0 0 0 1 1
( 448 96 0 ) ( 352 96 0 ) ( 352 96 16 ) base/floor1 0 0 0 1 1
( 352 96 0 ) ( 352 64 0 ) ( 352 64 16 ) base/floor1 0 0 0 1 1
}
// Step 2 (Y=64-128, height 0-32)
{
( 352 96 0 ) ( 448 96 0 ) ( 448 128 0 ) base/floor1 0 0 0 1 1
( 352 96 32 ) ( 352 128 32 ) ( 448 128 32 ) base/floor1 0 0 0 1 1
( 352 96 0 ) ( 352 96 32 ) ( 448 96 32 ) base/floor1 0 0 0 1 1
( 448 96 0 ) ( 448 128 0 ) ( 448 128 32 ) base/floor1 0 0 0 1 1
( 448 128 0 ) ( 352 128 0 ) ( 352 128 32 ) base/floor1 0 0 0 1 1
( 352 96 0 ) ( 352 128 0 ) ( 352 128 32 ) base/floor1 0 0 0 1 1
}
// Step 3
{
( 352 128 0 ) ( 448 128 0 ) ( 448 160 0 ) base/floor1 0 0 0 1 1
( 352 128 48 ) ( 352 160 48 ) ( 448 160 48 ) base/floor1 0 0 0 1 1
( 352 128 0 ) ( 352 128 48 ) ( 448 128 48 ) base/floor1 0 0 0 1 1
( 448 128 0 ) ( 448 160 0 ) ( 448 160 48 ) base/floor1 0 0 0 1 1
( 448 160 0 ) ( 352 160 0 ) ( 352 160 48 ) base/floor1 0 0 0 1 1
( 352 128 0 ) ( 352 160 0 ) ( 352 160 48 ) base/floor1 0 0 0 1 1
}
// Step 4
{
( 352 160 0 ) ( 448 160 0 ) ( 448 192 0 ) base/floor1 0 0 0 1 1
( 352 160 64 ) ( 352 192 64 ) ( 448 192 64 ) base/floor1 0 0 0 1 1
( 352 160 0 ) ( 352 160 64 ) ( 448 160 64 ) base/floor1 0 0 0 1 1
( 448 160 0 ) ( 448 192 0 ) ( 448 192 64 ) base/floor1 0 0 0 1 1
( 448 192 0 ) ( 352 192 0 ) ( 352 192 64 ) base/floor1 0 0 0 1 1
( 352 160 0 ) ( 352 192 0 ) ( 352 192 64 ) base/floor1 0 0 0 1 1
}
// Step 5
{
( 352 192 0 ) ( 448 192 0 ) ( 448 224 0 ) base/floor1 0 0 0 1 1
( 352 192 80 ) ( 352 224 80 ) ( 448 224 80 ) base/floor1 0 0 0 1 1
( 352 192 0 ) ( 352 192 80 ) ( 448 192 80 ) base/floor1 0 0 0 1 1
( 448 192 0 ) ( 448 224 0 ) ( 448 224 80 ) base/floor1 0 0 0 1 1
( 448 224 0 ) ( 352 224 0 ) ( 352 224 80 ) base/floor1 0 0 0 1 1
( 352 192 0 ) ( 352 224 0 ) ( 352 224 80 ) base/floor1 0 0 0 1 1
}
// Step 6
{
( 352 224 0 ) ( 448 224 0 ) ( 448 256 0 ) base/floor1 0 0 0 1 1
( 352 224 96 ) ( 352 256 96 ) ( 448 256 96 ) base/floor1 0 0 0 1 1
( 352 224 0 ) ( 352 224 96 ) ( 448 224 96 ) base/floor1 0 0 0 1 1
( 448 224 0 ) ( 448 256 0 ) ( 448 256 96 ) base/floor1 0 0 0 1 1
( 448 256 0 ) ( 352 256 0 ) ( 352 256 96 ) base/floor1 0 0 0 1 1
( 352 224 0 ) ( 352 256 0 ) ( 352 256 96 ) base/floor1 0 0 0 1 1
}
// Step 7
{
( 352 256 0 ) ( 448 256 0 ) ( 448 288 0 ) base/floor1 0 0 0 1 1
( 352 256 112 ) ( 352 288 112 ) ( 448 288 112 ) base/floor1 0 0 0 1 1
( 352 256 0 ) ( 352 256 112 ) ( 448 256 112 ) base/floor1 0 0 0 1 1
( 448 256 0 ) ( 448 288 0 ) ( 448 288 112 ) base/floor1 0 0 0 1 1
( 448 288 0 ) ( 352 288 0 ) ( 352 288 112 ) base/floor1 0 0 0 1 1
( 352 256 0 ) ( 352 288 0 ) ( 352 288 112 ) base/floor1 0 0 0 1 1
}
// Step 8 (final, reaches the elevated floor at 128)
{
( 352 288 0 ) ( 448 288 0 ) ( 448 320 0 ) base/floor1 0 0 0 1 1
( 352 288 128 ) ( 352 320 128 ) ( 448 320 128 ) base/floor1 0 0 0 1 1
( 352 288 0 ) ( 352 288 128 ) ( 448 288 128 ) base/floor1 0 0 0 1 1
( 448 288 0 ) ( 448 320 0 ) ( 448 320 128 ) base/floor1 0 0 0 1 1
( 448 320 0 ) ( 352 320 0 ) ( 352 320 128 ) base/floor1 0 0 0 1 1
( 352 288 0 ) ( 352 320 0 ) ( 352 320 128 ) base/floor1 0 0 0 1 1
}

// ============================================================
// Room 4: Danger Room (384 x 384 x 192, east of Main Hall)
// Connected via corridor at east side of Main Hall
// Origin around X=880
// ============================================================

// Danger Room Floor (uses orange dev texture for "lava" look)
{
( 800 -192 0 ) ( 1184 -192 0 ) ( 1184 192 0 ) dev/grid_orange 0 0 0 1 1
( 800 192 -16 ) ( 1184 192 -16 ) ( 1184 -192 -16 ) dev/grid_orange 0 0 0 1 1
( 800 -192 -16 ) ( 800 -192 0 ) ( 1184 -192 0 ) dev/grid_orange 0 0 0 1 1
( 1184 -192 -16 ) ( 1184 -192 0 ) ( 1184 192 0 ) dev/grid_orange 0 0 0 1 1
( 1184 192 -16 ) ( 1184 192 0 ) ( 800 192 0 ) dev/grid_orange 0 0 0 1 1
( 800 192 -16 ) ( 800 192 0 ) ( 800 -192 0 ) dev/grid_orange 0 0 0 1 1
}

// Danger Room Ceiling
{
( 800 -192 192 ) ( 800 192 192 ) ( 1184 192 192 ) base/ceiling1 0 0 0 1 1
( 800 192 176 ) ( 800 -192 176 ) ( 1184 -192 176 ) base/ceiling1 0 0 0 1 1
( 800 -192 176 ) ( 800 -192 192 ) ( 1184 -192 192 ) base/ceiling1 0 0 0 1 1
( 1184 -192 176 ) ( 1184 -192 192 ) ( 1184 192 192 ) base/ceiling1 0 0 0 1 1
( 1184 192 176 ) ( 1184 192 192 ) ( 800 192 192 ) base/ceiling1 0 0 0 1 1
( 800 192 176 ) ( 800 192 192 ) ( 800 -192 192 ) base/ceiling1 0 0 0 1 1
}

// Danger Room Walls (4 walls)
// West wall (opening to Main Hall corridor already exists)
{
( 784 -192 0 ) ( 784 -192 192 ) ( 800 -192 192 ) base/wall1 0 0 0 1 1
( 784 192 0 ) ( 800 192 0 ) ( 800 192 192 ) base/wall1 0 0 0 1 1
( 784 -192 192 ) ( 784 192 192 ) ( 800 192 192 ) base/wall1 0 0 0 1 1
( 784 -192 0 ) ( 800 -192 0 ) ( 800 192 0 ) base/wall1 0 0 0 1 1
( 784 -192 0 ) ( 784 -192 192 ) ( 784 192 192 ) base/wall1 0 0 0 1 1
( 800 -192 0 ) ( 800 192 0 ) ( 800 192 192 ) base/wall1 0 0 0 1 1
}
// East wall
{
( 1184 -192 0 ) ( 1200 -192 0 ) ( 1200 -192 192 ) base/wall1 0 0 0 1 1
( 1184 192 0 ) ( 1184 192 192 ) ( 1200 192 192 ) base/wall1 0 0 0 1 1
( 1184 -192 192 ) ( 1184 192 192 ) ( 1200 192 192 ) base/wall1 0 0 0 1 1
( 1184 -192 0 ) ( 1200 -192 0 ) ( 1200 192 0 ) base/wall1 0 0 0 1 1
( 1184 -192 0 ) ( 1184 -192 192 ) ( 1184 192 192 ) base/wall1 0 0 0 1 1
( 1200 -192 0 ) ( 1200 192 0 ) ( 1200 192 192 ) base/wall1 0 0 0 1 1
}
// South wall
{
( 800 -208 0 ) ( 1184 -208 0 ) ( 1184 -208 192 ) base/wall1 0 0 0 1 1
( 800 -192 0 ) ( 800 -192 192 ) ( 1184 -192 192 ) base/wall1 0 0 0 1 1
( 800 -208 0 ) ( 800 -192 0 ) ( 800 -192 192 ) base/wall1 0 0 0 1 1
( 1184 -208 0 ) ( 1184 -208 192 ) ( 1184 -192 192 ) base/wall1 0 0 0 1 1
( 800 -208 0 ) ( 1184 -208 0 ) ( 1184 -192 0 ) base/wall1 0 0 0 1 1
( 800 -208 192 ) ( 800 -192 192 ) ( 1184 -192 192 ) base/wall1 0 0 0 1 1
}
// North wall
{
( 800 192 0 ) ( 800 192 192 ) ( 1184 192 192 ) base/wall1 0 0 0 1 1
( 800 208 0 ) ( 1184 208 0 ) ( 1184 208 192 ) base/wall1 0 0 0 1 1
( 800 192 0 ) ( 800 208 0 ) ( 800 208 192 ) base/wall1 0 0 0 1 1
( 1184 192 0 ) ( 1184 192 192 ) ( 1184 208 192 ) base/wall1 0 0 0 1 1
( 800 192 0 ) ( 1184 192 0 ) ( 1184 208 0 ) base/wall1 0 0 0 1 1
( 800 192 192 ) ( 1184 192 192 ) ( 1184 208 192 ) base/wall1 0 0 0 1 1
}

// Safe path platforms across the lava (3 stepping stone platforms)
// Platform 1 (near entrance)
{
( 832 -16 0 ) ( 864 -16 0 ) ( 864 16 0 ) tech/metal1 0 0 0 1 1
( 832 -16 24 ) ( 832 16 24 ) ( 864 16 24 ) tech/metal1 0 0 0 1 1
( 832 -16 0 ) ( 832 -16 24 ) ( 864 -16 24 ) tech/metal1 0 0 0 1 1
( 864 -16 0 ) ( 864 16 0 ) ( 864 16 24 ) tech/metal1 0 0 0 1 1
( 864 16 0 ) ( 832 16 0 ) ( 832 16 24 ) tech/metal1 0 0 0 1 1
( 832 16 0 ) ( 832 -16 0 ) ( 832 -16 24 ) tech/metal1 0 0 0 1 1
}
// Platform 2 (middle)
{
( 944 -16 0 ) ( 976 -16 0 ) ( 976 16 0 ) tech/metal1 0 0 0 1 1
( 944 -16 24 ) ( 944 16 24 ) ( 976 16 24 ) tech/metal1 0 0 0 1 1
( 944 -16 0 ) ( 944 -16 24 ) ( 976 -16 24 ) tech/metal1 0 0 0 1 1
( 976 -16 0 ) ( 976 16 0 ) ( 976 16 24 ) tech/metal1 0 0 0 1 1
( 976 16 0 ) ( 944 16 0 ) ( 944 16 24 ) tech/metal1 0 0 0 1 1
( 944 16 0 ) ( 944 -16 0 ) ( 944 -16 24 ) tech/metal1 0 0 0 1 1
}
// Platform 3 (near exit, with health pickup)
{
( 1056 -16 0 ) ( 1088 -16 0 ) ( 1088 16 0 ) tech/metal1 0 0 0 1 1
( 1056 -16 24 ) ( 1056 16 24 ) ( 1088 16 24 ) tech/metal1 0 0 0 1 1
( 1056 -16 0 ) ( 1056 -16 24 ) ( 1088 -16 24 ) tech/metal1 0 0 0 1 1
( 1088 -16 0 ) ( 1088 16 0 ) ( 1088 16 24 ) tech/metal1 0 0 0 1 1
( 1088 16 0 ) ( 1056 16 0 ) ( 1056 16 24 ) tech/metal1 0 0 0 1 1
( 1056 16 0 ) ( 1056 -16 0 ) ( 1056 -16 24 ) tech/metal1 0 0 0 1 1
}

// ============================================================
// Room 5: Arena (640 x 640 x 320, south of Spawn Room)
// ============================================================

// Arena Floor
{
( -320 -576 0 ) ( 320 -576 0 ) ( 320 -256 0 ) base/floor1 0 0 0 1 1
( -320 -256 -16 ) ( 320 -256 -16 ) ( 320 -576 -16 ) base/floor1 0 0 0 1 1
( -320 -576 -16 ) ( -320 -576 0 ) ( 320 -576 0 ) base/floor1 0 0 0 1 1
( 320 -576 -16 ) ( 320 -576 0 ) ( 320 -256 0 ) base/floor1 0 0 0 1 1
( 320 -256 -16 ) ( 320 -256 0 ) ( -320 -256 0 ) base/floor1 0 0 0 1 1
( -320 -256 -16 ) ( -320 -256 0 ) ( -320 -576 0 ) base/floor1 0 0 0 1 1
}

// Arena Ceiling
{
( -320 -576 320 ) ( -320 -256 320 ) ( 320 -256 320 ) base/ceiling1 0 0 0 1 1
( -320 -256 304 ) ( -320 -576 304 ) ( 320 -576 304 ) base/ceiling1 0 0 0 1 1
( -320 -576 304 ) ( -320 -576 320 ) ( 320 -576 320 ) base/ceiling1 0 0 0 1 1
( 320 -576 304 ) ( 320 -576 320 ) ( 320 -256 320 ) base/ceiling1 0 0 0 1 1
( 320 -256 304 ) ( 320 -256 320 ) ( -320 -256 320 ) base/ceiling1 0 0 0 1 1
( -320 -256 304 ) ( -320 -256 320 ) ( -320 -576 320 ) base/ceiling1 0 0 0 1 1
}

// Arena Walls
// West wall
{
( -336 -576 0 ) ( -336 -576 320 ) ( -320 -576 320 ) base/wall1 0 0 0 1 1
( -336 -256 0 ) ( -320 -256 0 ) ( -320 -256 320 ) base/wall1 0 0 0 1 1
( -336 -576 320 ) ( -336 -256 320 ) ( -320 -256 320 ) base/wall1 0 0 0 1 1
( -336 -576 0 ) ( -320 -576 0 ) ( -320 -256 0 ) base/wall1 0 0 0 1 1
( -336 -576 0 ) ( -336 -576 320 ) ( -336 -256 320 ) base/wall1 0 0 0 1 1
( -320 -576 0 ) ( -320 -256 0 ) ( -320 -256 320 ) base/wall1 0 0 0 1 1
}
// East wall
{
( 320 -576 0 ) ( 336 -576 0 ) ( 336 -576 320 ) base/wall1 0 0 0 1 1
( 320 -256 0 ) ( 320 -256 320 ) ( 336 -256 320 ) base/wall1 0 0 0 1 1
( 320 -576 320 ) ( 320 -256 320 ) ( 336 -256 320 ) base/wall1 0 0 0 1 1
( 320 -576 0 ) ( 336 -576 0 ) ( 336 -256 0 ) base/wall1 0 0 0 1 1
( 320 -576 0 ) ( 320 -576 320 ) ( 320 -256 320 ) base/wall1 0 0 0 1 1
( 336 -576 0 ) ( 336 -256 0 ) ( 336 -256 320 ) base/wall1 0 0 0 1 1
}
// South wall
{
( -320 -592 0 ) ( 320 -592 0 ) ( 320 -592 320 ) base/wall1 0 0 0 1 1
( -320 -576 0 ) ( -320 -576 320 ) ( 320 -576 320 ) base/wall1 0 0 0 1 1
( -320 -592 0 ) ( -320 -576 0 ) ( -320 -576 320 ) base/wall1 0 0 0 1 1
( 320 -592 0 ) ( 320 -592 320 ) ( 320 -576 320 ) base/wall1 0 0 0 1 1
( -320 -592 0 ) ( 320 -592 0 ) ( 320 -576 0 ) base/wall1 0 0 0 1 1
( -320 -592 320 ) ( -320 -576 320 ) ( 320 -576 320 ) base/wall1 0 0 0 1 1
}
// North wall (with opening to Spawn Room corridor)
// Left section
{
( -320 -256 0 ) ( -320 -256 320 ) ( -32 -256 320 ) base/wall1 0 0 0 1 1
( -320 -240 0 ) ( -32 -240 0 ) ( -32 -240 320 ) base/wall1 0 0 0 1 1
( -320 -256 0 ) ( -320 -240 0 ) ( -320 -240 320 ) base/wall1 0 0 0 1 1
( -32 -256 0 ) ( -32 -256 320 ) ( -32 -240 320 ) base/wall1 0 0 0 1 1
( -320 -256 0 ) ( -32 -256 0 ) ( -32 -240 0 ) base/wall1 0 0 0 1 1
( -320 -256 320 ) ( -320 -240 320 ) ( -32 -240 320 ) base/wall1 0 0 0 1 1
}
// Right section
{
( 32 -256 0 ) ( 32 -256 320 ) ( 320 -256 320 ) base/wall1 0 0 0 1 1
( 32 -240 0 ) ( 320 -240 0 ) ( 320 -240 320 ) base/wall1 0 0 0 1 1
( 32 -256 0 ) ( 32 -240 0 ) ( 32 -240 320 ) base/wall1 0 0 0 1 1
( 320 -256 0 ) ( 320 -256 320 ) ( 320 -240 320 ) base/wall1 0 0 0 1 1
( 32 -256 0 ) ( 320 -256 0 ) ( 320 -240 0 ) base/wall1 0 0 0 1 1
( 32 -256 320 ) ( 32 -240 320 ) ( 320 -240 320 ) base/wall1 0 0 0 1 1
}

}

// ============================================================
// Entity 1: Player spawn (Room 1)
// ============================================================
{
"classname" "info_player_start"
"origin" "0 0 32"
"angle" "0"
}

// ============================================================
// Lights
// ============================================================

// Room 1 lights
{
"classname" "light"
"origin" "-64 -64 160"
"light" "200"
"color" "255 240 220"
}
{
"classname" "light"
"origin" "64 64 160"
"light" "200"
"color" "255 240 220"
}

// Main Hall lights
{
"classname" "light"
"origin" "464 0 220"
"light" "300"
"color" "255 255 255"
}
{
"classname" "light"
"origin" "300 0 200"
"light" "150"
"color" "100 150 255"
}
{
"classname" "light"
"origin" "628 0 200"
"light" "150"
"color" "255 200 100"
}
{
"classname" "light"
"origin" "464 200 200"
"light" "150"
"color" "100 255 150"
}

// Elevated Area light
{
"classname" "light"
"origin" "464 256 280"
"light" "200"
"color" "200 200 255"
}

// Danger Room lights (red-orange atmosphere)
{
"classname" "light"
"origin" "900 0 160"
"light" "200"
"color" "255 100 50"
}
{
"classname" "light"
"origin" "1080 0 160"
"light" "150"
"color" "255 80 30"
}

// Arena lights
{
"classname" "light"
"origin" "0 -416 280"
"light" "400"
"color" "255 255 255"
}
{
"classname" "light"
"origin" "-240 -500 200"
"light" "100"
"color" "255 100 100"
}
{
"classname" "light"
"origin" "240 -500 200"
"light" "100"
"color" "100 100 255"
}

// ============================================================
// Pickups
// ============================================================

// Main Hall pickups
{
"classname" "item_health"
"origin" "300 -100 16"
"amount" "25"
}
{
"classname" "item_health"
"origin" "628 100 16"
"amount" "25"
}
{
"classname" "item_ammo_shells"
"origin" "464 0 16"
"amount" "20"
}

// Danger Room reward (on last platform)
{
"classname" "item_health"
"origin" "1072 0 40"
"amount" "50"
}

// Arena pickups
{
"classname" "item_health"
"origin" "-240 -500 16"
"amount" "25"
}
{
"classname" "item_health"
"origin" "240 -340 16"
"amount" "25"
}
{
"classname" "item_ammo_shells"
"origin" "-100 -416 16"
"amount" "30"
}
{
"classname" "item_ammo_shells"
"origin" "100 -416 16"
"amount" "30"
}

// ============================================================
// Danger Room trigger_hurt (lava damage volume)
// ============================================================
{
"classname" "trigger_hurt"
"damage" "10"
// brush covering the floor area
{
( 800 -192 0 ) ( 1184 -192 0 ) ( 1184 192 0 ) trigger 0 0 0 1 1
( 800 192 -1 ) ( 1184 192 -1 ) ( 1184 -192 -1 ) trigger 0 0 0 1 1
( 800 -192 -1 ) ( 800 -192 32 ) ( 1184 -192 32 ) trigger 0 0 0 1 1
( 1184 -192 -1 ) ( 1184 -192 32 ) ( 1184 192 32 ) trigger 0 0 0 1 1
( 1184 192 -1 ) ( 1184 192 32 ) ( 800 192 32 ) trigger 0 0 0 1 1
( 800 192 -1 ) ( 800 192 32 ) ( 800 -192 32 ) trigger 0 0 0 1 1
}
}

// ============================================================
// Door (between Spawn Room and corridor)
// ============================================================
{
"classname" "func_door"
"speed" "200"
"wait" "3"
"lip" "8"
"angle" "-1"
// Door brush filling the doorway
{
( 128 -32 0 ) ( 144 -32 0 ) ( 144 32 0 ) base/wall1 0 0 0 1 1
( 128 -32 128 ) ( 128 32 128 ) ( 144 32 128 ) base/wall1 0 0 0 1 1
( 128 -32 0 ) ( 128 -32 128 ) ( 144 -32 128 ) base/wall1 0 0 0 1 1
( 144 32 0 ) ( 144 32 128 ) ( 144 -32 128 ) base/wall1 0 0 0 1 1
( 128 32 0 ) ( 128 32 128 ) ( 144 32 128 ) base/wall1 0 0 0 1 1
( 128 -32 0 ) ( 128 32 0 ) ( 144 32 0 ) base/wall1 0 0 0 1 1
}
}

// ============================================================
// Arena enemies (if monster_grunt is defined in your FGD)
// ============================================================
{
"classname" "monster_grunt"
"origin" "-160 -416 32"
"angle" "90"
}
{
"classname" "monster_grunt"
"origin" "160 -480 32"
"angle" "180"
}
{
"classname" "monster_grunt"
"origin" "0 -320 32"
"angle" "270"
}

// ============================================================
// Teleporter in Arena (back to Spawn Room)
// ============================================================
{
"classname" "trigger_teleport"
"target_x" "0"
"target_y" "0"
"target_z" "32"
// Brush in the corner of the arena
{
( 256 -560 0 ) ( 320 -560 0 ) ( 320 -496 0 ) dev/grid_white 0 0 0 1 1
( 256 -496 -1 ) ( 320 -496 -1 ) ( 320 -560 -1 ) dev/grid_white 0 0 0 1 1
( 256 -560 -1 ) ( 256 -560 128 ) ( 320 -560 128 ) dev/grid_white 0 0 0 1 1
( 320 -560 -1 ) ( 320 -560 128 ) ( 320 -496 128 ) dev/grid_white 0 0 0 1 1
( 320 -496 -1 ) ( 320 -496 128 ) ( 256 -496 128 ) dev/grid_white 0 0 0 1 1
( 256 -496 -1 ) ( 256 -496 128 ) ( 256 -560 128 ) dev/grid_white 0 0 0 1 1
}
}

// ============================================================
// Directional light (global sun)
// ============================================================
{
"classname" "light_environment"
"direction" "-0.2 -1.0 -0.3"
"color" "200 200 220"
"ambient" "0.15"
}
```

> **Note on the .map file above:** This is a structural reference. When you build the level yourself in TrenchBroom, the editor generates brush plane definitions automatically -- you never hand-write plane equations. The file above demonstrates the format and can be loaded by your parser for immediate testing. If some brushes don't render correctly, the issue is likely plane winding order; TrenchBroom always generates valid brush definitions, so prefer building in the editor over hand-editing .map files.

---

## Step 7: Final Integration Check

This step verifies the complete pipeline from TrenchBroom to a running game. Walk through each stage and confirm it works.

### The pipeline

```
TrenchBroom                 QEngine
─────────────              ──────────────────────────────────
Design level          -->  Save .map file
                           │
                           ├── 1. Parse .map  (Ch 17: MapLoader)
                           ├── 2. Build meshes (Ch 17: BrushMeshBuilder)
                           ├── 3. Load textures (Ch 20: TextureLoader)
                           ├── 4. Spawn entities (Ch 18: EntitySpawner)
                           ├── 5. Create collision (Ch 19: Jolt bodies)
                           └── 6. Game loop runs
                                ├── Player spawns at info_player_start
                                ├── Walk around, collide with brushes
                                ├── Doors open, lifts move
                                ├── Triggers fire (damage, teleport)
                                ├── Pickups collect
                                ├── Weapons fire
                                ├── HUD displays health/ammo
                                └── Death triggers respawn
```

### Verification checklist

Test each of these in order:

| Test | What to Check | Pass Criteria |
|------|--------------|---------------|
| Map loads | No parse errors in console | "Map loaded: X brushes, Y entities" |
| Geometry renders | All rooms visible, no holes | Walk through every room, check walls/floor/ceiling |
| Textures display | Correct textures on faces | No magenta checkerboard (fallback) on intended surfaces |
| Lighting works | Point lights illuminate, directional light casts | Rooms are not uniformly dark or uniformly bright |
| Collision works | Player cannot walk through walls | Push against every wall, try to clip through corners |
| Player spawns | Player starts at info_player_start | Check initial position matches the entity in the .map |
| Door opens | func_door moves when triggered/approached | Walk to the door, it opens, wait for it to close |
| Stairs work | Player can walk up stairs smoothly | No jittering, no getting stuck on step edges |
| Lava damages | trigger_hurt reduces health | Stand on lava, watch health bar decrease |
| Lava knockback | Player gets pushed upward by lava | Slight upward kick each time damage ticks |
| Damage flash | Screen flashes red when taking damage | Visible red overlay that fades quickly |
| Death respawn | Health reaching 0 resets player | Die in lava, respawn at start with full health |
| Invulnerability | 1 second of protection after respawn | Respawn, walk onto lava, no damage for 1 second |
| Pickups work | item_health and item_ammo_shells collect | Walk over them, health/ammo increases |
| Enemies present | monster_grunt entities spawn (if implemented) | Enemies visible in the arena |
| Teleporter works | trigger_teleport sends player to destination | Step into teleporter, appear in spawn room |
| HUD displays | Crosshair, health bar, ammo counter visible | All HUD elements render correctly |
| Weapons fire | Hitscan and projectile weapons work | Shoot at walls, enemies; see tracers/projectiles |
| Hot reload | Changing .map in TrenchBroom reloads in engine | Save in TrenchBroom, level updates within 1 second |

### Update main.cpp to load the map

Update `main.cpp` to load the TrenchBroom map as the default level. Replace the old showcase level setup with map loading:

```cpp
#include "engine/level/map_loader.h"
#include "engine/level/brush_mesh_builder.h"
#include "engine/level/texture_loader.h"
#include "engine/ecs/entity_spawner.h"
#include "engine/ecs/jolt_body_helpers.h"
#include "engine/level/map_hot_reload.h"

// ... inside main(), after ECS and Jolt initialization:

// ─── Load the map ──────────────────────────────────────────
std::string mapPath = "assets/maps/e1m1.map";

// Allow command-line override: ./QEngine my_map.map
if (argc > 1)
{
    mapPath = std::string("assets/maps/") + argv[1];
}

MapLoader mapLoader;
auto mapData = mapLoader.load(mapPath);
if (!mapData.has_value())
{
    std::cerr << "Failed to load map: " << mapPath << std::endl;
    return -1;
}

// Register TextureLoader as a registry context object
registry.ctx().emplace<TextureLoader>();
auto& texLoader = registry.ctx().get<TextureLoader>();

// Build renderable meshes from brush geometry
BrushMeshBuilder meshBuilder;
meshBuilder.buildFromMap(registry, mapData.value(), texLoader);

// Spawn all entities (player, lights, pickups, enemies, doors, triggers)
EntitySpawner spawner;
spawner.spawnAllEntities(registry, mapData.value());

// Create Jolt collision bodies from brush geometry
createBrushCollisionBodies(registry, mapData.value());

// Initialize the player's Jolt CharacterVirtual
initPlayerCharacter(registry);

std::cout << "Map loaded: " << mapPath << std::endl;

// ─── Hot reload (development only) ────────────────────────
MapHotReload hotReload;
hotReload.init(mapPath);

// ─── Main loop ─────────────────────────────────────────────
while (!window.shouldClose())
{
    // ... input handling, delta time ...

    // Check for map file changes
    hotReload.checkAndReload(registry);

    // ... fixed timestep, rendering ...
}
```

### Command-line map selection

The command-line argument lets you quickly test different maps:

```bash
# Default: loads assets/maps/e1m1.map
./QEngine

# Load a specific map
./QEngine test.map

# Load another map
./QEngine arena_test.map
```

This is a small quality-of-life feature but invaluable during development. You can have multiple test maps for different features and switch between them without recompiling.

---

## Step 8: Archiving Old Level Code

The old hardcoded level system has been completely replaced by the TrenchBroom pipeline. The sector/portal level format from Chapter 8, the procedural level builder, and the manual entity spawning code are no longer used. Archive them for tutorial reference rather than deleting them.

### Create the archive directory

```
src/engine/ecs/archived/
src/engine/level/archived/
```

The `systems/archived/` directory already exists from Chapter 15d. Now create matching archive directories for the ECS helpers and level code.

### Move these files

| File | What It Did | Replaced By |
|------|-------------|-------------|
| `src/engine/ecs/showcase_level.h/.cpp` | Hardcoded room geometry using Sector/Surface structs | TrenchBroom-authored .map files parsed by `MapLoader` |
| `src/engine/ecs/scene_setup.h/.cpp` | Manual entity spawning (player, lights, cubes, doors, lifts, triggers) | `EntitySpawner` driven by .map entity definitions |
| `src/engine/level/level_loader.h/.cpp` | Loaded the sector/portal level format from code | `MapLoader` parses .map files from disk |
| `src/engine/level/level.h` | Defined `Sector`, `Surface`, `Portal` structs | `MapData`, `Brush`, `BrushFace`, `MapEntity` from the map parser |

Move these into their respective `archived/` directories.

> **Why keep them?** These files document the journey. A reader following the tutorial series can compare the hand-built approach (Chapters 8-15) with the TrenchBroom approach (Chapters 17-20) and understand why editor-authored content is superior for real game development. The archived code also serves as a fallback if someone wants to follow just the early chapters without installing TrenchBroom.

### Update CMakeLists.txt

Remove the archived source files and add any remaining new files from this chapter:

```cmake
# REMOVE these from the source list:
# src/engine/ecs/showcase_level.cpp
# src/engine/ecs/scene_setup.cpp
# src/engine/level/level_loader.cpp

# ADD these (if not already present from earlier chapters):
src/engine/level/texture_loader.cpp
src/engine/level/map_hot_reload.cpp
```

### Final project structure

After archiving, the active source tree looks like this:

```
src/
├── main.cpp
└── engine/
    ├── core/
    │   ├── window.h/.cpp
    │   ├── input.h/.cpp
    │   ├── resource_manager.h/.cpp
    │   └── fixed_timestep.h/.cpp
    ├── ecs/
    │   ├── components.h
    │   ├── entity_spawner.h/.cpp         (Ch 18)
    │   ├── jolt_body_helpers.h/.cpp      (Ch 15d)
    │   ├── weapon_definitions.h          (Ch 12)
    │   ├── systems/
    │   │   ├── weapon_switch_system.h
    │   │   ├── player_character_system.h/.cpp
    │   │   ├── mover_system.h/.cpp
    │   │   ├── mover_sync_system.h/.cpp
    │   │   ├── jolt_sync_system.h/.cpp
    │   │   ├── combat_system.h/.cpp
    │   │   ├── lifetime_system.h/.cpp
    │   │   ├── trigger_system.h/.cpp
    │   │   ├── player_death_system.h/.cpp
    │   │   ├── demo_reset_system.h/.cpp
    │   │   ├── render_system.h/.cpp
    │   │   ├── debug_hud_system.h/.cpp
    │   │   └── archived/
    │   │       ├── collision_system.h/.cpp
    │   │       ├── physics_system.h/.cpp
    │   │       ├── movement_system.h/.cpp
    │   │       └── player_movement_system.h/.cpp
    │   └── archived/
    │       ├── showcase_level.h/.cpp
    │       └── scene_setup.h/.cpp
    ├── physics/
    │   ├── jolt_world.h/.cpp
    │   ├── jolt_layers.h
    │   └── physics_config.h
    ├── renderer/
    │   ├── camera.h/.cpp
    │   ├── shader.h/.cpp
    │   ├── texture.h/.cpp
    │   └── mesh.h/.cpp
    └── level/
        ├── map_loader.h/.cpp             (Ch 17)
        ├── brush_mesh_builder.h/.cpp     (Ch 17)
        ├── texture_loader.h/.cpp         (Ch 20)
        ├── map_hot_reload.h/.cpp         (Ch 20)
        └── archived/
            ├── level.h
            └── level_loader.h/.cpp
```

---

## What Changed -- Summary

| File | Change |
|------|--------|
| `tb/GameConfig.cfg` | **New file** -- TrenchBroom game configuration for QEngine |
| `tb/Icon.png` | **New file** (optional) -- 32x32 game icon for TrenchBroom |
| `assets/textures/base/` | **New directory** -- base texture set (floor, wall, ceiling, trim) |
| `assets/textures/tech/` | **New directory** -- tech/metal texture set |
| `assets/textures/dev/` | **New directory** -- developer prototype textures (grids) |
| `assets/maps/e1m1.map` | **New file** -- the final playable level |
| `src/engine/level/texture_loader.h/.cpp` | **New files** -- maps .map texture names to image files, caching, fallback checkerboard |
| `src/engine/level/map_hot_reload.h/.cpp` | **New files** -- watches .map file for changes, reloads level automatically |
| `main.cpp` | Replaced showcase level setup with MapLoader pipeline. Added command-line map selection. Added hot reload. |
| `CMakeLists.txt` | Removed archived source files. Added `texture_loader.cpp` and `map_hot_reload.cpp`. |

### Files archived

| File | Was | Archived To |
|------|-----|-------------|
| `showcase_level.h/.cpp` | Hardcoded room geometry (Ch 8/15d) | `ecs/archived/` |
| `scene_setup.h/.cpp` | Manual entity spawning (Ch 11-15) | `ecs/archived/` |
| `level_loader.h/.cpp` | Sector/portal level loader (Ch 8) | `level/archived/` |
| `level.h` | Sector/Surface/Portal structs (Ch 8) | `level/archived/` |

---

## What You Should See

After building and running:

1. **TrenchBroom recognizes QEngine** -- it appears in the game selection list when creating a new map
2. **Textures appear in TrenchBroom's browser** -- organized by category (base, tech, dev)
3. **Entities are available in TrenchBroom** -- all FGD entities show up in the entity browser
4. **The final level loads in QEngine** -- all five rooms render correctly with proper textures
5. **Lighting works throughout the level** -- each room has its own lighting atmosphere
6. **Collision is solid everywhere** -- cannot walk through walls, floors, or ceilings
7. **The door opens and closes** -- approach the spawn room door, it opens, waits, and closes
8. **Stairs are walkable** -- smooth stepping up to the elevated area
9. **Lava damages the player** -- standing in the Danger Room drains health, flashes red, applies knockback
10. **Death and respawn work** -- die in lava, respawn at the start with full health and brief invulnerability
11. **Pickups collect** -- health and ammo items increase the corresponding values
12. **The teleporter works** -- stepping into the Arena teleporter sends you back to the spawn room
13. **Weapons work** -- fire at walls and enemies, see tracers and projectiles
14. **The HUD displays correctly** -- crosshair, health bar, ammo counter all visible
15. **Hot reload works** -- save a change in TrenchBroom, see it reflected in QEngine within a second

---

## Series Conclusion

You started with an empty window in Chapter 0. Twenty chapters later, you have a complete game engine.

### The journey

| Phase | Chapters | What You Built |
|-------|----------|---------------|
| Foundation | 0-3 | Window, OpenGL context, shader system, ECS architecture |
| 3D Rendering | 4-7 | Camera, textures, mesh loading, Phong lighting |
| Game World | 8-11 | Level geometry, collision detection, physics, triggers/doors/lifts |
| Player & Gameplay | 12-15 | Weapons, projectiles, player body, Jolt Physics integration |
| TrenchBroom | 16-20 | Gameplay polish, .map parser, entity mapping, brush collision, editor workflow |

### What QEngine can do now

- **Render** textured, lit 3D environments with directional and point lights
- **Physics** via Jolt: rigid bodies, character controller with Quake-style movement, kinematic movers
- **Gameplay**: hitscan and projectile weapons, health/ammo pickups, damage feedback, death/respawn
- **Interactive objects**: doors, lifts, trigger volumes (damage, teleport, mover activation)
- **Level editing**: TrenchBroom integration with custom game config, FGD, and texture browser
- **Development tools**: debug HUD, hot reload, command-line map selection
- **Architecture**: clean ECS with stateless systems, data-only components, and a fixed timestep

### Ideas for future enhancements

QEngine is a solid foundation. Here are directions you could take it next:

| Enhancement | What It Adds |
|-------------|-------------|
| **Lightmap baking** | Pre-computed lighting from brush face data. Raycast from each lightmap texel to each light source, store in an atlas texture. Dramatically improves visual quality without runtime cost. |
| **Detail brushes** | Non-collision decorative geometry. Mark some brushes as "detail" so they render but do not generate Jolt bodies. Useful for pillars, trim, rubble. |
| **BSP compilation** | Spatial partitioning for large levels. Convert brushes to a BSP tree with potentially visible sets (PVS) for efficient occlusion culling. Essential for levels with more than a few hundred brushes. |
| **Audio system** | Add miniaudio for sound effects and music. 3D positional audio for footsteps, weapon sounds, and ambient environment. |
| **Networked multiplayer** | Use ENet for reliable UDP networking. Implement client-server architecture with snapshot interpolation, client-side prediction, and lag compensation. |
| **Particle system** | Muzzle flashes, explosions, blood effects, environmental particles. GPU-instanced billboards with configurable lifetime, velocity, and colour curves. |
| **Skeletal animation** | Bone hierarchies, skinning shaders, animation blending. Import animated models from glTF format. Add weapon view models with recoil and reload animations. |
| **AI navigation** | NavMesh generation from brush geometry. A* pathfinding for enemies. State machines for patrol, chase, attack, and retreat behaviours. |
| **Post-processing** | Framebuffer objects for bloom, vignette, colour grading, and motion blur. Screen-space ambient occlusion (SSAO) for depth. |
| **Save/load system** | Serialize the ECS registry to JSON or binary. Save player progress, health, ammo, current map, and entity states. |

Each of these is a self-contained project that builds on the foundation you have already laid. The ECS architecture makes adding new systems straightforward: define components for the new data, write a system function that queries and updates them, and wire it into the tick order.

### Final words

You have built a game engine from scratch. Not a wrapper around someone else's framework, not a scripting layer on top of Unity or Unreal -- a real engine that you understand from the ground up, from the OpenGL draw calls to the Jolt physics step to the TrenchBroom map parser.

That understanding is the real value of this tutorial series. Commercial engines are better tools for shipping games, but they are black boxes. When something breaks, you search forums and hope someone else had the same problem. With QEngine, you can trace any bug from symptom to cause because you wrote every line.

The code is yours. Extend it, break it, rebuild it. Make a game with it, or use the knowledge to write a better engine. The twenty chapters behind you are a foundation -- what you build next is up to you.

---

## What's Next

This is the final chapter of the QEngine tutorial series. The engine is complete, the tools are integrated, and the level is playable. If you want to keep building, the "Ideas for future enhancements" table above is a good starting point. Each enhancement is independent -- pick the one that excites you most and start building.

Thank you for following along. Now go make something.
