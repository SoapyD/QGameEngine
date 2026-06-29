# Game Engine Architecture: A Comparative Guide

## Overview

This document compares three major approaches to game engine architecture, tracing the evolution from the early 1990s id Software engines through to modern Entity Component Systems (ECS).

---

## 1. id Style — Direct & Procedural (1990s)

Used by: **Doom (id Tech 1), Quake (id Tech 2)**

The id Software engines are tightly coupled, procedural C codebases. Systems communicate through direct function calls and shared global state. There is no event bus, no publish-subscribe pattern, and no decoupling layer.

**Core characteristics:**
- A tight main loop: process input -> run tickers -> render
- Game objects are flat structs with function pointers (Doom's `mobj_t`, Quake's `edict_t`)
- A "thinker" system — a linked list of active objects that get their think functions called each tick
- Everything just knows about everything else

**Example — Player opens a door:**

```cpp
void UseDoor(Player* player, Door* door) {
    if (player->hasKey[door->keyType]) {
        door->state = DOOR_OPENING;
        PlaySound(door->openSound);
        player->hasKey[door->keyType] = false;
    }
}
```

One function. It knows about the player, the door, and the sound system. It reaches in and does everything directly.

**Example — Enemy dies:**

```cpp
void KillEnemy(Enemy* enemy, Player* player) {
    player->score += enemy->points;
    player->killCount++;
    SpawnGibs(enemy->x, enemy->y);
    PlaySound(SFX_ENEMY_DEATH);
    RemoveFromWorld(enemy);
}
```

**Strengths:**
- Extremely simple to read and debug — one function, whole picture
- Maximum performance — no indirection overhead
- Ideal for small teams (2-4 programmers)

**Weaknesses:**
- Becomes spaghetti as the codebase grows
- Adding new reactions (e.g. achievements, particles) means editing existing functions
- Hard for large teams to work on in parallel

**Key resources:**
- Doom source code (GPL, 1997)
- Quake source code (GPL, 1999)
- Fabien Sanglard's *Game Engine Black Book: Doom* and *Game Engine Black Book: Quake*

---

## 2. McShaffry Style — Event-Driven Architecture (2000s)

Used by: **Unreal Engine 3-era, Unity (original), most mid-2000s engines**

Reference: *Game Coding Complete (4th Edition)* by Mike McShaffry and David "Rez" Graham

This approach decouples systems through an event bus. Instead of calling functions directly, code fires events and other systems register as listeners. Entities use an actor/component model with inheritance hierarchies.

**Core characteristics:**
- An `EventManager` that routes events between systems
- Publish-subscribe pattern — systems register for events they care about
- Actor/component model for entities
- Lua scripting integration for game logic
- Layered architecture: application layer, game logic, game view

**Example — Player opens a door:**

```cpp
// The initial action just fires an event
void UseDoor(ActorId player, ActorId door) {
    EventManager::Get()->TriggerEvent(
        std::make_shared<DoorUseEvent>(player, door)
    );
}

// The door system registered for this event (in a separate file)
void DoorSystem::OnDoorUse(shared_ptr<DoorUseEvent> event) {
    if (inventorySystem->HasKey(event->player, event->door)) {
        EventManager::Get()->TriggerEvent(
            std::make_shared<DoorOpenEvent>(event->door)
        );
    }
}

// The audio system also registered for DoorOpenEvent (another file)
void AudioSystem::OnDoorOpen(shared_ptr<DoorOpenEvent> event) {
    PlaySound(GetDoorSound(event->door));
}

// The inventory system listens too (yet another file)
void InventorySystem::OnDoorOpen(shared_ptr<DoorOpenEvent> event) {
    RemoveKey(event->player, event->door);
}
```

**Example — Enemy dies:**

```cpp
void KillEnemy(ActorId enemy) {
    EventManager::Get()->TriggerEvent(
        std::make_shared<ActorDeathEvent>(enemy)
    );
}

// Five different files, five different systems, all listening:
// ScoreSystem::OnActorDeath    -> adds points
// StatsSystem::OnActorDeath    -> increments kill count
// GibSystem::OnActorDeath      -> spawns gibs
// AudioSystem::OnActorDeath    -> plays sound
// WorldSystem::OnActorDeath    -> removes from world
```

**Strengths:**
- Loosely coupled — systems don't need to know about each other
- Easy to add new reactions without touching existing code
- Scales well for large teams where people own different systems

**Weaknesses:**
- Logic is scattered across many files ("ravioli code")
- Harder to debug — you have to chase events across the codebase
- Event indirection adds overhead
- Can become over-engineered

**Key resources:**
- *Game Coding Complete (4th Edition)* — McShaffry & Graham
- *Game Programming Patterns* — Bob Nystrom (free at gameprogrammingpatterns.com)

---

## 3. ECS — Entity Component System (2010s–Present)

Used by: **Unity DOTS, Bevy (Rust), EnTT/flecs (C++), Overwatch engine**

The modern dominant paradigm. The core rule: **components have no behaviour, systems have no state**. Entities are just numeric IDs that tie components together. This is a data-oriented approach designed around how CPUs actually work (cache lines, memory layout).

**Core characteristics:**
- **Entities** are just IDs (e.g. `uint32_t`)
- **Components** are plain data structs with no methods
- **Systems** are functions that query for entities with specific component combinations and operate on them
- Data is stored contiguously in memory for cache efficiency
- Composition over inheritance — entity types are defined by which components they have

### How Entities Are Built

You don't write classes. You compose entities from data:

**A Player:**
```
Entity 1:
  - Position { x: 10, y: 5, z: 0 }
  - Velocity { dx: 0, dy: 0, dz: 0 }
  - Health { current: 100, max: 100 }
  - PlayerInput {}              // tag — "this entity reads input"
  - Sprite { texture: "hero.png" }
  - Collider { width: 32, height: 64 }
```

**A Goblin:**
```
Entity 2:
  - Position { x: 50, y: 20, z: 0 }
  - Velocity { dx: 0, dy: 0, dz: 0 }
  - Health { current: 30, max: 30 }
  - AIFollow { target: Entity 1, speed: 3.0 }
  - Sprite { texture: "goblin.png" }
  - Collider { width: 24, height: 32 }
```

**A Turret (can't move — no Velocity component):**
```
Entity 3:
  - Position { x: 100, y: 100, z: 0 }
  - Health { current: 200, max: 200 }
  - Sprite { texture: "turret.png" }
  - Collider { width: 32, height: 32 }
  - Shooter { fireCooldown: 1.0, projectile: "bullet" }
```

**A Healing Zone (can't move, can't be damaged — no Velocity or Health):**
```
Entity 4:
  - Position { x: 75, y: 30, z: 0 }
  - Collider { width: 64, height: 64 }
  - HealZone { rate: 5.0 }
```

### How Systems Work

Systems query for component combinations and operate on any entity that matches:

```cpp
// Moves ANYTHING with Position and Velocity — player, enemy, bullet, whatever
void MovementSystem(World& world) {
    for (auto [pos, vel] : world.query<Position, Velocity>()) {
        pos.x += vel.dx * dt;
        pos.y += vel.dy * dt;
    }
}

// Only grabs entities with PlayerInput AND Velocity
void PlayerInputSystem(World& world) {
    for (auto [input, vel] : world.query<PlayerInput, Velocity>()) {
        if (KeyDown(W)) vel.dy = -5.0f;
        if (KeyDown(S)) vel.dy =  5.0f;
        if (KeyDown(A)) vel.dx = -5.0f;
        if (KeyDown(D)) vel.dx =  5.0f;
    }
}

// AI chases its target
void AIFollowSystem(World& world) {
    for (auto [pos, ai] : world.query<Position, AIFollow>()) {
        auto& targetPos = world.get<Position>(ai.target);
        float dx = targetPos.x - pos.x;
        float dy = targetPos.y - pos.y;
        float len = sqrt(dx*dx + dy*dy);
        auto& vel = world.get<Velocity>(ai.target);
        vel.dx = (dx / len) * ai.speed;
        vel.dy = (dy / len) * ai.speed;
    }
}

// Anything with Health that hits zero gets destroyed
void DeathSystem(World& world) {
    for (auto [entity, health] : world.query<Health>()) {
        if (health.current <= 0) {
            world.destroy(entity);
        }
    }
}

// Heals ANYTHING with Health that overlaps a HealZone
void HealZoneSystem(World& world) {
    for (auto [pos, col, zone] : world.query<Position, Collider, HealZone>()) {
        for (auto [ePos, eCol, health] : world.query<Position, Collider, Health>()) {
            if (Overlaps(pos, col, ePos, eCol)) {
                health.current = min(health.current + zone.rate * dt, health.max);
            }
        }
    }
}
```

Notice: `HealZoneSystem` doesn't care what walks into the zone. Players heal. Goblins heal. Turrets heal. Anything with `Health` that overlaps gets healed. Zero special cases.

### Tick Order & Queries

Systems are called in a defined order each frame. The order matters because systems read and write the same data:

```cpp
void GameLoop() {
    while (running) {
        PlayerInputSystem(world);   // 1. read input, set velocities
        AIFollowSystem(world);      // 2. AI decides velocities
        MovementSystem(world);      // 3. apply velocities to positions
        CollisionSystem(world);     // 4. resolve overlaps
        HealZoneSystem(world);      // 5. apply zone effects
        DeathSystem(world);         // 6. clean up dead entities
        RenderSystem(world);        // 7. draw everything
    }
}
```

Order matters because `PlayerInputSystem` sets velocity, and `MovementSystem` reads it. If you flip those, input would be one frame delayed. Getting the tick order right is one of the real design challenges with ECS.

Each system simply says "give me every entity that has components X, Y, Z" and iterates over the matches. A system doesn't know or care what "type" of entity it's processing. If an entity has the right components, it matches. If it doesn't, it's invisible to that system.

That's why the turret with no `Velocity` component simply can't move — `MovementSystem` queries for `Position + Velocity`, the turret only has `Position`, so the query never returns it. The behaviour isn't "disabled", it literally doesn't exist for that entity.

It's a very flat, mechanical process. No polymorphism, no virtual dispatch, no event routing. Just: iterate data, do work, next system.

**Strengths:**
- Extremely cache-friendly — data stored contiguously in memory
- Maximum flexibility — new entity types are just new combinations of existing components
- Systems are simple, testable functions
- Scales well for both small and large teams
- Behaviour emerges from what data is present (no Velocity = can't move)

**Weaknesses:**
- Steeper initial learning curve
- Relationships between entities (parent-child, inventory) can be awkward
- Debugging can be harder — no object to inspect, just scattered data
- Still a relatively young pattern — less established literature

**Key resources:**
- *Data-Oriented Design* by Richard Fabian (free at dataorienteddesign.com)
- *Game Engine Architecture (3rd Ed.)* by Jason Gregory
- GDC 2017: "Overwatch Gameplay Architecture and Netcode" by Tim Ford
- EnTT documentation and wiki (by Michele "skypjack" Caini)
- flecs documentation and blog (by Sander Mertens)
- Allan Deutsch's "ECS Back and Forth" series

---

## Side-by-Side Comparison

| | id Style | McShaffry Style | ECS |
|---|---|---|---|
| **Era** | 1990s | 2000s | 2010s–present |
| **Communication** | Direct function calls | Event bus, pub-sub | Systems query data |
| **Entities** | Flat structs + function pointers | Actor/component objects | ID + data composition |
| **Coupling** | Tight | Loose | None (data and logic separated) |
| **Behaviour lives in** | Functions that know everything | Event listeners | Stateless systems |
| **Adding new behaviour** | Edit existing functions | Register a new listener | Write a new system |
| **Debugging** | Read one function | Chase events across files | Inspect data tables |
| **Performance focus** | Manual optimization | Architecture-first | Cache-friendly by design |
| **Analogy** | Cooking alone in your kitchen | A restaurant with stations | An assembly line |

## The Mental Shift

| OOP Thinking | ECS Thinking |
|---|---|
| "A player **is a** character that **can** move and shoot" | "Entity 42 **has** Position, Velocity, Shooter data" |
| Behaviour lives inside objects | Behaviour lives in systems, objects are just data |
| New entity type = new class | New entity type = new combination of existing components |
| "What **is** this thing?" | "What data **does** this thing have?" |

---

## Recommended Learning Path

1. **Understand the philosophy**: Read *Data-Oriented Design* by Richard Fabian
2. **Learn traditional patterns first**: Read *Game Programming Patterns* by Bob Nystrom
3. **Study the history**: Read Fabien Sanglard's Black Books on Doom and Quake
4. **Get hands-on with ECS**: Pick up EnTT (C++ header-only) or flecs and build a small game
5. **Study a real-world example**: Watch the Overwatch GDC talk for how ECS works in production
6. **Go deeper**: Read *Game Engine Architecture* by Jason Gregory for the full picture
