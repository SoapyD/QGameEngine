# QEngine — Technical Documentation

Reference documentation for the QEngine codebase. Updated through Chapter 15d.

## Documents

| Document | Description |
|----------|-------------|
| [ARCHITECTURE.md](ARCHITECTURE.md) | Project structure, folder layout, design philosophy |
| [SYSTEMS.md](SYSTEMS.md) | Every ECS system — what it does, what components it reads/writes, tick order |
| [COMPONENTS.md](COMPONENTS.md) | Every ECS component — fields, defaults, which systems use them |
| [TICK_ORDER.md](TICK_ORDER.md) | The game loop explained — input, fixed timestep, physics, rendering |
| [JOLT_PHYSICS.md](JOLT_PHYSICS.md) | Jolt integration — body types, layers, CharacterVirtual, body helpers |
| [SCENE_SETUP.md](SCENE_SETUP.md) | How the showcase scene is built — entities, lighting, physics demos, triggers |

## Diagrams

| File | Description |
|------|-------------|
| [ECS Architecture (PNG)](images/ecs_architecture.png) | Visual diagram of all systems, components, helpers, and data flow |
| [ECS Architecture (SVG)](images/ecs_architecture.svg) | Same diagram, scalable vector format |
| [ECS Architecture (DOT)](images/ecs_architecture.dot) | Graphviz source file |

## Roadmaps

| Document | Description |
|----------|-------------|
| [ROADMAP.md](../roadmap/ROADMAP.md) | Master chapter plan (Phases 1-5, Chapters 0-20) |
| [ROADMAP_TRENCHBROOM.md](../roadmap/ROADMAP_TRENCHBROOM.md) | TrenchBroom integration details (Chapters 17-20) |
| [ROADMAP_CLEANUP.md](../roadmap/ROADMAP_CLEANUP.md) | Cleanup and polish tasks |
| [ROADMAP_NICE_TO_HAVES.md](../roadmap/ROADMAP_NICE_TO_HAVES.md) | Optional future features |

## Other Reference

| Document | Description |
|----------|-------------|
| [CPP_CONCEPTS_BY_CHAPTER.md](CPP_CONCEPTS_BY_CHAPTER.md) | C++ concepts introduced in each chapter |
| [FUTURE_TUTORIALS.md](../roadmap/FUTURE_TUTORIALS.md) | Ideas for future tutorial content |
| [Processes overview](../processes/_overview.md) | How each functionality flows across systems |
| [Status overview](../status/_overview.md) | Quick-parse state of every functionality |
