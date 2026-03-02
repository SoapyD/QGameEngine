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
│   ├── main.cpp                    # Entry point and game loop
│   └── engine/
│       ├── core/                   # Window, input, resources, timestep
│       ├── ecs/
│       │   ├── components.h        # All ECS component definitions
│       │   ├── scene_setup.*       # Entity spawning
│       │   └── systems/            # All game systems
│       ├── physics/                # Jolt integration, config, layers
│       ├── renderer/               # Camera, shaders, textures, meshes
│       └── level/                  # Level geometry and loading
├── assets/                         # Shaders, textures, models
├── docs/                           # Technical documentation
└── extern/                         # Third-party libraries
```

## Documentation

Detailed technical documentation lives in [`docs/`](docs/README.md):

- [Architecture](docs/ARCHITECTURE.md) — Project structure, design philosophy, what's implemented
- [Systems](docs/SYSTEMS.md) — Every ECS system, what it does, components it uses
- [Components](docs/COMPONENTS.md) — Every component, fields, defaults, which systems read/write them
- [Tick Order](docs/TICK_ORDER.md) — Game loop breakdown, system execution order, data flow
- [Jolt Physics](docs/JOLT_PHYSICS.md) — Physics integration, body types, layers, CharacterVirtual
- [Scene Setup](docs/SCENE_SETUP.md) — Showcase scene entities, positions, configuration

## Tutorial Series

This engine is built incrementally across a tutorial series (Chapters 0-25). Tutorials live separately from the codebase. Current progress: Chapters 0-15d complete.

| Phase | Chapters | Focus |
|-------|----------|-------|
| 1. Foundation | 0-5 | Window, shaders, ECS, camera, textures |
| 2. World Building | 6-8 | Mesh loading, lighting, level geometry |
| 3. Physics | 9-10 | Collision detection, movement, game loop |
| 4. Gameplay | 11-13 | Doors/lifts/triggers, weapons, player body |
| 5. Jolt Physics | 14-15 | Physics engine integration, character controller |
| 6. Content | 16-25 | Gameplay content, TrenchBroom integration |
