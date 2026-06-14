# QEngine

A Quake-inspired game engine built from scratch in C++ as a tutorial series. Uses OpenGL for rendering, EnTT for ECS architecture, and Jolt Physics for collision and character movement.

## Features

- **ECS Architecture** — Entity Component System using EnTT. Components are pure data, systems are stateless functions.
- **OpenGL Rendering** — Phong lighting (directional + point lights), textured meshes, debug HUD overlay.
- **Jolt Physics** — Static, dynamic, and kinematic bodies. CharacterVirtual for player movement. Sensor bodies for triggers.
- **Quake-style Movement** — Ground/air acceleration, bunny hopping, stair stepping via CharacterVirtual's ExtendedUpdate.
- **Gameplay Systems** — Doors, lifts (with start delay), trigger volumes (activate, teleport, damage, heal), hitscan and projectile weapons.
- **Fixed Timestep** — Physics and game logic run at 60 ticks/second independent of frame rate.

## Building

Requires MSYS2 UCRT64 toolchain (GCC, CMake, Ninja).

```bash
cmake -B build -G Ninja
cmake --build build
```

## Project Structure

```
QEngine/
├── src/
│   ├── main.cpp                    # Windowed entry point and game loop
│   ├── engine/
│   │   ├── core/                   # Window, input, resources, timestep
│   │   ├── ecs/
│   │   │   ├── components.h        # All ECS component definitions
│   │   │   ├── scene_setup.*       # Entity spawning
│   │   │   └── systems/            # All game systems
│   │   ├── physics/                # Jolt integration, config, layers
│   │   ├── renderer/               # Camera, shaders, textures, meshes
│   │   ├── level/                  # Level geometry and loading
│   │   └── app/                    # Shared sim orchestration (buildWorld, stepSimulation)
│   └── harness/                    # Headless entry point (QEngineHeadless)
├── assets/                         # Shaders, textures, models
├── docs/                           # Technical documentation
└── extern/                         # Third-party libraries (entt, glad, glfw, glm, stb; Jolt via FetchContent)
```

## Documentation

Detailed technical documentation lives in [`docs/architecture/`](docs/architecture/README.md):

- [Architecture](docs/architecture/ARCHITECTURE.md) — Project structure, design philosophy, what's implemented
- [Systems](docs/architecture/SYSTEMS.md) — Every ECS system, what it does, components it uses
- [Components](docs/architecture/COMPONENTS.md) — Every component, fields, defaults, which systems read/write them
- [Tick Order](docs/architecture/TICK_ORDER.md) — Game loop breakdown, system execution order, data flow
- [Jolt Physics](docs/architecture/JOLT_PHYSICS.md) — Physics integration, body types, layers, CharacterVirtual
- [Scene Setup](docs/architecture/SCENE_SETUP.md) — Showcase scene entities, positions, configuration

Process flows and current status live in [`docs/processes/`](docs/processes/_overview.md) and [`docs/status/`](docs/status/_overview.md).

## Tutorial Series

This engine is built incrementally across a tutorial series (Chapters 0-20). Tutorials live separately from the codebase. Current progress: Chapters 0-15d complete.

| Phase | Chapters | Focus |
|-------|----------|-------|
| 1. Foundation | 0-3 | Window, shaders, ECS |
| 2. 3D Rendering | 4-7 | Camera, textures, mesh loading, lighting |
| 3. Game World | 8-11 | Level geometry, collision, physics, triggers |
| 4. Player & Gameplay | 12-15 | Weapons, player body, Jolt Physics integration |
| 5. TrenchBroom | 16-20 | Gameplay polish, .map parser, entity mapping, brush collision, editor workflow |
