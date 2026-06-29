# Chapter 14a: Jolt Physics — CMake, Boilerplate & World Wrapper

## What You'll Learn
- Why we're replacing custom physics with Jolt
- Installing and linking Jolt Physics via CMake FetchContent
- The Jolt boilerplate: layers, filters, allocators, job systems
- Creating a physics world wrapper and stepping it each tick

---

## Why Jolt?

Our custom physics — `physicsSystem`, `collisionSystem`, `movementSystem`, `groundDetectionSystem` — works for simple cases but breaks down when things interact. The lift can't carry the player. Objects jitter when resting on surfaces. Stair-stepping is a fragile hack. Every fix reveals the next edge case.

Real physics engines solve all of this with constraint solvers, persistent contact manifolds, and proper resting contact. Jolt Physics is a modern C++17 library used in Horizon Forbidden West and Death Stranding 2. It's multi-threaded, clean to integrate, and its API style fits naturally with our EnTT codebase.

The swap is contained: we replace four systems and their support code. Everything else — rendering, ECS, input, triggers, movers, combat, the debug HUD — stays untouched.

---

## Step 1: Add Jolt to the Project

We'll use CMake FetchContent to download and build Jolt automatically.

### Update `CMakeLists.txt`

Add these lines after the existing `stb` library block and before the `add_executable` line:

```cmake
# ──────────────────────────────────────────────
# Jolt Physics — fetched from GitHub
# ──────────────────────────────────────────────
include(FetchContent)

# Jolt configuration — set BEFORE FetchContent_MakeAvailable
set(PHYSICS_REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    JoltPhysics
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG        v5.2.0
    SOURCE_SUBDIR  Build
)
FetchContent_MakeAvailable(JoltPhysics)
```

Then add `Jolt` to the `target_link_libraries`:

```cmake
target_link_libraries(QEngine PRIVATE
    glfw
    glad
    glm
    entt
    stb
    Jolt                # NEW
)
```

### Static Linking on MSYS2 / MinGW (Windows only)

Jolt's thread pool uses `std::thread`, which links against `libwinpthread-1.dll` at runtime. On MSYS2, if your PATH contains DLLs from a different toolchain (e.g. `mingw64` instead of `ucrt64`), the program will crash on startup with exit code `0xc0000139`. The fix is to statically link the C++ runtime so no DLLs are needed:

Add this after `add_executable(QEngine ...)` and before `target_include_directories`:

```cmake
# Static-link the C++ runtime so we don't depend on DLLs at all
# (avoids UCRT64 / MINGW64 mismatch on MSYS2)
if (MINGW)
    target_link_options(QEngine PRIVATE -static-libgcc -static-libstdc++ -static)
endif()
```

This bakes the runtime libraries directly into the executable. The binary is slightly larger but fully self-contained.

The first build will take a minute or two as CMake downloads and compiles Jolt. Subsequent builds are fast — CMake caches the result.

### Fix VS Code IntelliSense

FetchContent downloads Jolt into `build/_deps/`, which VS Code's C/C++ extension doesn't know about. To fix red squiggles on `#include <Jolt/Jolt.h>`, add the Jolt source path to `.vscode/c_cpp_properties.json`:

```json
"includePath": [
    "${workspaceFolder}/src",
    "${workspaceFolder}/extern/entt/src",
    "${workspaceFolder}/extern/glfw/include",
    "${workspaceFolder}/extern/glad/include",
    "${workspaceFolder}/extern/glm",
    "${workspaceFolder}/extern/stb",
    "${workspaceFolder}/build/_deps/joltphysics-src"
]
```

Also add `CMAKE_EXPORT_COMPILE_COMMANDS` near the top of `CMakeLists.txt` (after `project(...)`) so IntelliSense can resolve all include paths:

```cmake
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
```

Reload the VS Code window after configuring CMake.

> **`SOURCE_SUBDIR Build`** — Jolt's CMakeLists.txt lives in a `Build/` subdirectory of the repo, not the root. This tells FetchContent where to find it.

---

## Step 2: The Jolt Boilerplate

Jolt requires several pieces of setup before you can create a physics world. This is the most code-heavy step, but it's all copy-paste boilerplate that you write once and rarely touch again.

### New file: `src/engine/physics/jolt_setup.h`

```cpp
#pragma once

// Jolt requires this macro before including any headers
#include <Jolt/Jolt.h>

// Jolt headers
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>

#include <thread>
#include <cstdarg>
#include <iostream>

// All Jolt symbols are inside the JPH namespace
using namespace JPH;
using namespace JPH::literals;

// ─── Object Layers ──────────────────────────────────────────
// These map roughly to our existing CollisionLayers bitmask,
// but Jolt uses a different system: ObjectLayers for bodies,
// BroadPhaseLayers for the broad-phase acceleration structure.

namespace Layers
{
    static constexpr JPH::ObjectLayer NON_MOVING = 0;  // floors, walls
    static constexpr JPH::ObjectLayer MOVING     = 1;  // player, physics objects
    static constexpr JPH::ObjectLayer SENSOR     = 2;  // triggers (no collision response)
    static constexpr JPH::ObjectLayer NUM_LAYERS = 3;
};

namespace BroadPhaseLayers
{
    static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
    static constexpr JPH::BroadPhaseLayer MOVING(1);
    static constexpr uint NUM_LAYERS(2);
};

// ─── Layer Mapping ──────────────────────────────────────────
// Maps each ObjectLayer to a BroadPhaseLayer.
// Non-moving and sensors share the static broad-phase layer.

class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface
{
public:
    BPLayerInterfaceImpl()
    {
        mObjectToBroadPhase[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
        mObjectToBroadPhase[Layers::MOVING]     = BroadPhaseLayers::MOVING;
        mObjectToBroadPhase[Layers::SENSOR]     = BroadPhaseLayers::NON_MOVING;
    }

    virtual uint GetNumBroadPhaseLayers() const override
    {
        return BroadPhaseLayers::NUM_LAYERS;
    }

    virtual JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override
    {
        return mObjectToBroadPhase[inLayer];
    }

    virtual const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override
    {
        switch ((JPH::BroadPhaseLayer::Type)inLayer)
        {
        case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::NON_MOVING: return "NON_MOVING";
        case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::MOVING:     return "MOVING";
        default:                                                        return "UNKNOWN";
        }
    }

private:
    JPH::BroadPhaseLayer mObjectToBroadPhase[Layers::NUM_LAYERS];
};

// ─── Collision Filters ──────────────────────────────────────
// Determines which object layers can collide with each other.
// Static objects don't collide with each other (no point).

class ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter
{
public:
    virtual bool ShouldCollide(JPH::ObjectLayer inLayer1,
                               JPH::ObjectLayer inLayer2) const override
    {
        switch (inLayer1)
        {
        case Layers::NON_MOVING:
            return inLayer2 == Layers::MOVING;
        case Layers::MOVING:
            return inLayer2 != Layers::SENSOR;
        case Layers::SENSOR:
            return inLayer2 == Layers::MOVING;
        default:
            return false;
        }
    }
};

class ObjectVsBroadPhaseLayerFilterImpl : public JPH::ObjectVsBroadPhaseLayerFilter
{
public:
    virtual bool ShouldCollide(JPH::ObjectLayer inLayer1,
                               JPH::BroadPhaseLayer inLayer2) const override
    {
        switch (inLayer1)
        {
        case Layers::NON_MOVING:
            return inLayer2 == BroadPhaseLayers::MOVING;
        case Layers::MOVING:
            return true;
        case Layers::SENSOR:
            return inLayer2 == BroadPhaseLayers::MOVING;
        default:
            return false;
        }
    }
};

// ─── Jolt Trace Callback (debug logging) ────────────────────

static void JoltTraceImpl(const char* inFMT, ...)
{
    va_list list;
    va_start(list, inFMT);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), inFMT, list);
    va_end(list);
    std::cout << "[Jolt] " << buffer << std::endl;
}

#ifdef JPH_ENABLE_ASSERTS
static bool JoltAssertFailedImpl(const char* inExpression, const char* inMessage,
                                  const char* inFile, uint inLine)
{
    std::cout << inFile << ":" << inLine << ": (" << inExpression << ") "
              << (inMessage != nullptr ? inMessage : "") << std::endl;
    return true;  // break into debugger
}
#endif
```

### Understanding the Layers

Jolt uses a two-tier filtering system:

1. **Object Layers** -- every body has one. Our three layers are `NON_MOVING` (level geometry), `MOVING` (player, physics cubes), and `SENSOR` (trigger volumes).

2. **Broad-Phase Layers** -- a coarser grouping used by Jolt's internal acceleration structure. We map `NON_MOVING` and `SENSOR` to the same broad-phase layer since they're both static. `MOVING` gets its own.

3. **Filters** -- `ObjectLayerPairFilter` decides which object layer pairs can collide. `ObjectVsBroadPhaseLayerFilter` decides which object layers test against which broad-phase layers. The key rule: static doesn't collide with static.

This is more setup than our old bitmask system, but Jolt uses it for performance -- the broad-phase layer structure enables very fast spatial queries.

---

## Step 3: The Physics World Wrapper

Rather than scattering Jolt setup through `main.cpp`, we'll wrap it in a class that the ECS can use.

### New file: `src/engine/physics/jolt_world.h`

```cpp
#pragma once

#include "engine/physics/jolt_setup.h"
#include <memory>

// Wraps Jolt's PhysicsSystem and its required infrastructure.
// Stored in registry context so systems can access it.
struct JoltWorld
{
    // Jolt infrastructure — must outlive the PhysicsSystem
    std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator;
    std::unique_ptr<JPH::JobSystemThreadPool> jobSystem;

    // Layer interfaces
    BPLayerInterfaceImpl broadPhaseLayerInterface;
    ObjectVsBroadPhaseLayerFilterImpl objectVsBroadPhaseFilter;
    ObjectLayerPairFilterImpl objectLayerPairFilter;

    // The physics world itself
    std::unique_ptr<JPH::PhysicsSystem> physicsSystem;

    void init()
    {
        // Register Jolt allocator and install callbacks
        JPH::RegisterDefaultAllocator();
        JPH::Trace = JoltTraceImpl;
        JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = JoltAssertFailedImpl;)

        // Create the factory (needed for serialization/deserialization)
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();

        // Pre-allocate 10 MB for physics temp data
        tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(10 * 1024 * 1024);

        // Create a thread pool — use all available cores minus one
        jobSystem = std::make_unique<JPH::JobSystemThreadPool>(
            JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
            (int)std::thread::hardware_concurrency() - 1
        );

        // Create the physics system
        const uint maxBodies = 1024;
        const uint numBodyMutexes = 0;    // auto
        const uint maxBodyPairs = 1024;
        const uint maxContactConstraints = 1024;

        physicsSystem = std::make_unique<JPH::PhysicsSystem>();
        physicsSystem->Init(
            maxBodies, numBodyMutexes, maxBodyPairs, maxContactConstraints,
            broadPhaseLayerInterface, objectVsBroadPhaseFilter,
            objectLayerPairFilter
        );

        // Set gravity (Quake-style: 20 units/s^2 downward)
        physicsSystem->SetGravity(JPH::Vec3(0.0f, -20.0f, 0.0f));
    }

    void step(float deltaTime)
    {
        // Step the physics simulation
        // 1 collision step per update is fine for our fixed timestep
        physicsSystem->Update(deltaTime, 1, tempAllocator.get(), jobSystem.get());
    }

    JPH::BodyInterface& getBodyInterface()
    {
        return physicsSystem->GetBodyInterface();
    }

    void shutdown()
    {
        physicsSystem.reset();
        jobSystem.reset();
        tempAllocator.reset();

        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }
};
```

### New file: `src/engine/physics/jolt_world.cpp`

```cpp
#include "engine/physics/jolt_world.h"
```

This is intentionally minimal -- the implementation is all in the header for now since everything is inline. The `.cpp` file exists so the linker has a translation unit for Jolt symbols.

---

## What Changed — Summary

| File | Change |
|------|--------|
| `CMakeLists.txt` | Added Jolt FetchContent, linked `Jolt` library |
| `jolt_setup.h` | **New** -- Jolt includes, layer definitions, filter classes, callbacks |
| `jolt_world.h` | **New** -- Physics world wrapper with init/step/shutdown |
| `jolt_world.cpp` | **New** -- Translation unit for Jolt symbols |

---

## New C++ Concept: FetchContent

CMake's `FetchContent` module downloads and builds external dependencies automatically:

```cmake
FetchContent_Declare(
    JoltPhysics                                          # Name (your choice)
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git  # Where
    GIT_TAG        v5.2.0                                # Which version
    SOURCE_SUBDIR  Build                                 # Where CMakeLists.txt lives
)
FetchContent_MakeAvailable(JoltPhysics)                  # Download & add_subdirectory
```

This is the modern alternative to git submodules. The download happens once at configure time and is cached in `_deps/` inside your build directory. You can pin a specific version with `GIT_TAG` -- always do this in real projects to avoid surprises.

---

## What's Next

The Jolt physics library is linked and the world wrapper is ready. In **Chapter 14b**, we'll create physics bodies — static bodies from level geometry, dynamic bodies for physics entities — and wire the physics step into the game loop.
