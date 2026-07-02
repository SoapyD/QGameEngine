# Chapter 26: Boss Fights & Arenas

## What You'll Learn
- Building complex gameplay from existing ECS systems
- Multi-phase boss AI with health-gated transitions
- Attack patterns as projectile maths
- Arena lockdown using triggers and movers
- Spawn waves for mid-fight reinforcements
- Death sequences using particles, audio, and screen shake

---

## The ECS Lesson

This chapter adds **no new engine systems**. Everything here is built from components and systems that already exist:

| Feature | Built From |
|---------|-----------|
| Boss behaviour | AIBrain (Ch 14) extended with phases |
| Attack patterns | Projectile spawning (Ch 12) |
| Arena lockdown | TriggerVolume + Mover (Ch 11) |
| Spawn waves | Entity creation (Ch 3) |
| Death sequence | Particles (Ch 20) + Audio (Ch 16) + Screen shake (Ch 20) |
| Health gates | Health component (Ch 3) |

This is ECS paying dividends. Complex gameplay emerges from composing simple, generic parts. The boss is just an entity with data. There is no `BossClass`.

---

## Multi-Phase Boss AI

A boss fight is an FSM (finite state machine) with health-gated phase transitions — the same pattern as Chapter 14's enemy AI, but with more phases and more interesting attacks.

### Boss Components

```cpp
struct BossPhase {
    float healthThreshold;       // Transition when HP% drops below this
    float moveSpeed;
    float attackCooldown;        // Seconds between attacks
    std::string attackPattern;   // "single", "spread", "spiral", "slam"
    std::string enterSound;      // Sound when entering this phase
    int spawnWaveIndex = -1;     // Index into spawn wave list, -1 = none
};

struct BossBrain {
    std::vector<BossPhase> phases;
    int currentPhase = 0;
    float attackTimer = 0.0f;
    float spiralAngle = 0.0f;   // For rotating spiral attacks

    // Phase transition
    bool inTransition = false;
    float transitionTimer = 0.0f;
    float transitionDuration = 1.5f;  // Brief invulnerability + animation

    bool defeated = false;
};
```

### The Boss System

```cpp
// src/engine/ecs/systems/boss_system.h
#pragma once
#include <entt/entt.hpp>

void bossSystem(entt::registry& registry, float dt);
```

```cpp
// src/engine/ecs/systems/boss_system.cpp
#include "engine/ecs/systems/boss_system.h"
#include "engine/ecs/components.h"

void bossSystem(entt::registry& registry, float dt) {
    auto view = registry.view<BossBrain, Health, Position>();

    for (auto [entity, boss, health, pos] : view.each()) {
        if (boss.defeated) continue;

        // ─── Check for death ────────────────────────────────────
        if (health.current <= 0.0f) {
            boss.defeated = true;
            triggerBossDeathSequence(registry, entity, pos.value);
            continue;
        }

        // ─── Check for phase transition ─────────────────────────
        float healthPercent = health.current / health.max;
        int targetPhase = boss.currentPhase;

        for (int i = boss.currentPhase + 1;
             i < static_cast<int>(boss.phases.size()); i++) {
            if (healthPercent <= boss.phases[i].healthThreshold) {
                targetPhase = i;
            }
        }

        if (targetPhase != boss.currentPhase && !boss.inTransition) {
            boss.inTransition = true;
            boss.transitionTimer = 0.0f;

            // Play phase transition sound
            registry.emplace_or_replace<PlaySoundOnce>(
                entity, boss.phases[targetPhase].enterSound, 1.0f);

            // Spawn wave if this phase has one
            int waveIdx = boss.phases[targetPhase].spawnWaveIndex;
            if (waveIdx >= 0) {
                triggerSpawnWave(registry, waveIdx);
            }
        }

        // ─── Handle transition (brief invulnerability) ──────────
        if (boss.inTransition) {
            boss.transitionTimer += dt;

            // Screen shake during transition
            // applyScreenShake(registry, 0.3f);

            if (boss.transitionTimer >= boss.transitionDuration) {
                boss.inTransition = false;
                boss.currentPhase = targetPhase;
                boss.attackTimer = 0.0f;
            }
            continue;  // Skip attacks during transition
        }

        // ─── Attack logic ───────────────────────────────────────
        const auto& phase = boss.phases[boss.currentPhase];
        boss.attackTimer += dt;

        if (boss.attackTimer >= phase.attackCooldown) {
            boss.attackTimer = 0.0f;

            // Find the player
            glm::vec3 playerPos(0.0f);
            auto players = registry.view<TagPlayer, Position>();
            for (auto [p, tag, ppos] : players.each()) {
                playerPos = ppos.value;
                break;
            }

            // Execute attack pattern
            if (phase.attackPattern == "single") {
                fireAtPlayer(registry, pos.value, playerPos, 15.0f, 20.0f);
            } else if (phase.attackPattern == "spread") {
                fireSpread(registry, pos.value, playerPos, 5, 30.0f, 15.0f, 15.0f);
            } else if (phase.attackPattern == "spiral") {
                fireSpiral(registry, pos.value, boss.spiralAngle, 8, 12.0f, 15.0f);
                boss.spiralAngle += 15.0f;  // Rotate each volley
            } else if (phase.attackPattern == "slam") {
                groundSlam(registry, pos.value, 5.0f, 50.0f);
            }

            // Play attack sound
            registry.emplace_or_replace<PlaySoundOnce>(entity, "boss_attack", 1.0f);
        }

        // ─── Movement (chase player) ────────────────────────────
        if (registry.all_of<Velocity>(entity)) {
            glm::vec3 playerPos(0.0f);
            auto players = registry.view<TagPlayer, Position>();
            for (auto [p, tag, ppos] : players.each()) {
                playerPos = ppos.value;
                break;
            }

            glm::vec3 toPlayer = playerPos - pos.value;
            float dist = glm::length(toPlayer);

            if (dist > 3.0f) {  // Don't walk on top of the player
                glm::vec3 dir = glm::normalize(toPlayer);
                auto& vel = registry.get<Velocity>(entity);
                vel.value.x = dir.x * phase.moveSpeed;
                vel.value.z = dir.z * phase.moveSpeed;
            }
        }
    }
}
```

---

## Attack Patterns

Each pattern is a function that spawns projectile entities. The maths is straightforward trigonometry. Add these attack pattern functions to `src/engine/ecs/systems/boss_system.cpp`:

### Single Shot

One projectile aimed at the player:

```cpp
void fireAtPlayer(entt::registry& registry,
                   const glm::vec3& origin, const glm::vec3& target,
                   float speed, float damage) {
    glm::vec3 direction = glm::normalize(target - origin);

    auto proj = registry.create();
    registry.emplace<Position>(proj, origin);
    registry.emplace<Velocity>(proj, direction * speed);
    registry.emplace<Projectile>(proj, damage, 5.0f);  // damage, lifetime
    registry.emplace<AABBCollider>(proj, glm::vec3(0.2f));
    // registry.emplace<MeshRenderer>(proj, "projectile", "projectile_tex");
}
```

### Spread (Fan of Projectiles)

Multiple projectiles in an arc — the same maths as the shotgun from Chapter 12:

```cpp
void fireSpread(entt::registry& registry,
                 const glm::vec3& origin, const glm::vec3& target,
                 int count, float spreadAngle, float speed, float damage) {

    glm::vec3 baseDir = glm::normalize(target - origin);
    float baseAngle = atan2(baseDir.z, baseDir.x);
    float halfSpread = glm::radians(spreadAngle) * 0.5f;

    for (int i = 0; i < count; i++) {
        // Distribute evenly across the spread arc
        float t = (count > 1) ? static_cast<float>(i) / (count - 1) : 0.5f;
        float angle = baseAngle - halfSpread + t * halfSpread * 2.0f;

        glm::vec3 dir(cos(angle), 0.0f, sin(angle));

        auto proj = registry.create();
        registry.emplace<Position>(proj, origin);
        registry.emplace<Velocity>(proj, dir * speed);
        registry.emplace<Projectile>(proj, damage, 5.0f);
        registry.emplace<AABBCollider>(proj, glm::vec3(0.15f));
    }
}
```

### Spiral (Rotating Pattern)

Projectiles spawned in a ring that rotates each volley — the "bullet hell" pattern:

```cpp
void fireSpiral(entt::registry& registry,
                 const glm::vec3& origin, float startAngle,
                 int count, float speed, float damage) {

    float angleStep = glm::two_pi<float>() / count;

    for (int i = 0; i < count; i++) {
        float angle = glm::radians(startAngle) + i * angleStep;
        glm::vec3 dir(cos(angle), 0.0f, sin(angle));

        auto proj = registry.create();
        registry.emplace<Position>(proj, origin);
        registry.emplace<Velocity>(proj, dir * speed);
        registry.emplace<Projectile>(proj, damage, 4.0f);
        registry.emplace<AABBCollider>(proj, glm::vec3(0.1f));
    }
}
```

```
Spiral pattern (top view, 3 volleys):

     Volley 1          Volley 2          Volley 3
       ↑                 ↗                →
    ←  B  →           ↙  B  ↗          ↑  B  ↓
       ↓                 ↙                ←

Each volley rotates 15° from the last
```

### Ground Slam (Area Damage)

No projectiles — instant splash damage at the boss's position. Reuses the splash damage function from Chapter 12:

```cpp
void groundSlam(entt::registry& registry,
                 const glm::vec3& center, float radius, float damage) {

    // Damage all entities within radius
    auto view = registry.view<Position, Health>();
    for (auto [entity, pos, health] : view.each()) {
        if (registry.all_of<BossBrain>(entity)) continue;  // Don't damage self

        float dist = glm::length(pos.value - center);
        if (dist < radius) {
            float falloff = 1.0f - (dist / radius);  // More damage at centre
            health.current -= damage * falloff;
        }
    }

    // Spawn ground crack particles
    // emitParticleBurst(registry, center, 30, ...);

    // Screen shake
    // applyScreenShake(registry, 0.5f);

    // Sound
    // Play "boss_slam" sound
}
```

---

## Arena System

The arena manages the encounter — lock the player in, spawn the boss, unlock when it's dead.

### Arena Component

```cpp
struct ArenaController {
    bool activated = false;
    bool bossDefeated = false;
    glm::vec3 bossSpawnPoint;
    std::string bossType;

    // Entity references (set during level load)
    std::vector<entt::entity> doors;         // Doors to lock/unlock
    std::vector<glm::vec3> rewardPositions;  // Where to spawn rewards

    float defeatDelay = 2.0f;    // Seconds after boss death before rewards/unlock
    float defeatTimer = 0.0f;
};

struct ArenaTrigger {
    entt::entity arenaEntity;    // Which ArenaController to activate
};
```

### Arena System

```cpp
void arenaSystem(entt::registry& registry, float dt) {
    auto view = registry.view<ArenaController>();

    for (auto [entity, arena] : view.each()) {

        // ─── Not yet activated ──────────────────────────────────
        if (!arena.activated) continue;

        // ─── Check if boss is defeated ──────────────────────────
        if (!arena.bossDefeated) {
            // Check if any BossBrain entity is defeated
            auto bosses = registry.view<BossBrain>();
            for (auto [bossEntity, boss] : bosses.each()) {
                if (boss.defeated) {
                    arena.bossDefeated = true;
                    arena.defeatTimer = 0.0f;
                    break;
                }
            }
        }

        // ─── Post-defeat: wait then unlock ──────────────────────
        if (arena.bossDefeated) {
            arena.defeatTimer += dt;

            if (arena.defeatTimer >= arena.defeatDelay) {
                // Unlock doors
                for (auto door : arena.doors) {
                    if (registry.valid(door) && registry.all_of<Mover>(door)) {
                        registry.get<Mover>(door).state = MoverState::Opening;
                    }
                }

                // Spawn rewards
                for (const auto& pos : arena.rewardPositions) {
                    // spawnHealthPickup(registry, pos);
                    // spawnAmmoPickup(registry, pos);
                }

                // Remove the controller (arena is done)
                registry.remove<ArenaController>(entity);
            }
        }
    }
}
```

### Activation (via Trigger)

When the player walks into the arena trigger:

```cpp
// In triggerSystem (Chapter 11), add:
if (registry.all_of<ArenaTrigger>(triggerEntity)) {
    auto& arenaTrig = registry.get<ArenaTrigger>(triggerEntity);
    auto& arena = registry.get<ArenaController>(arenaTrig.arenaEntity);

    if (!arena.activated) {
        arena.activated = true;

        // Lock doors
        for (auto door : arena.doors) {
            if (registry.all_of<Mover>(door)) {
                registry.get<Mover>(door).state = MoverState::Closing;
            }
        }

        // Spawn the boss
        spawnBoss(registry, arena.bossType, arena.bossSpawnPoint);

        // Remove trigger (one-shot)
        registry.remove<ArenaTrigger>(triggerEntity);
    }
}
```

---

## Spawn Waves

Mid-fight reinforcements to maintain pressure. Add this to `src/engine/ecs/systems/boss_system.cpp`:

```cpp
struct SpawnWave {
    struct SpawnEntry {
        std::string enemyType;
        glm::vec3 position;
    };

    std::vector<SpawnEntry> spawns;
    bool triggered = false;
};

// Global wave definitions (loaded from level data)
std::vector<SpawnWave> spawnWaves;

void triggerSpawnWave(entt::registry& registry, int waveIndex) {
    if (waveIndex < 0 || waveIndex >= static_cast<int>(spawnWaves.size())) return;

    auto& wave = spawnWaves[waveIndex];
    if (wave.triggered) return;
    wave.triggered = true;

    for (const auto& entry : wave.spawns) {
        // Use existing enemy factory from Chapter 14
        // spawnEnemy(registry, entry.enemyType, entry.position);
    }
}
```

---

## Boss Death Sequence

When the boss's health reaches zero. Add this to `src/engine/ecs/systems/boss_system.cpp`:

```cpp
void triggerBossDeathSequence(entt::registry& registry,
                                entt::entity bossEntity,
                                const glm::vec3& position) {

    // 1. Explosion particles
    // Reuse the explosion emitter from Chapter 20
    // emitExplosion(registry, position, 50);  // 50 particles

    // 2. Multiple smaller explosions over time (stagger effect)
    // Create a timed sequence entity:
    auto seq = registry.create();
    registry.emplace<Position>(seq, position);
    // registry.emplace<DeathSequence>(seq, 2.0f);  // 2 second sequence

    // 3. Sound
    registry.emplace_or_replace<PlaySoundOnce>(bossEntity, "boss_death", 1.5f);

    // 4. Screen shake
    // applyScreenShake(registry, 1.0f);  // Strong, 1 second

    // 5. Remove boss visual (optional: ragdoll or fade)
    // registry.remove<MeshRenderer>(bossEntity);

    // 6. The ArenaController detects boss.defeated = true
    //    and handles door unlocking + rewards (see arena system above)
}
```

---

## Example Boss: The Guardian

A complete boss definition — nothing but data. Add this factory function to `src/engine/ecs/scene_setup.cpp` (or a dedicated `src/engine/ecs/boss_factory.cpp`):

```cpp
entt::entity spawnBoss(entt::registry& registry,
                        const std::string& type,
                        const glm::vec3& position) {

    auto boss = registry.create();

    // Core components
    registry.emplace<Position>(boss, position);
    registry.emplace<Velocity>(boss, glm::vec3(0.0f));
    registry.emplace<Rotation>(boss, glm::vec3(0.0f));
    registry.emplace<Health>(boss, 500.0f, 500.0f);
    registry.emplace<AABBCollider>(boss, glm::vec3(1.5f, 2.0f, 1.5f));
    registry.emplace<Gravity>(boss);
    // registry.emplace<MeshRenderer>(boss, "guardian", "guardian_tex");

    // Boss brain with 3 phases
    BossBrain brain;
    brain.phases = {
        // Phase 1: 100-60% HP — slow, single rockets
        {
            .healthThreshold = 1.0f,   // Active from start
            .moveSpeed = 3.0f,
            .attackCooldown = 2.0f,
            .attackPattern = "single",
            .enterSound = "boss_roar",
            .spawnWaveIndex = -1
        },
        // Phase 2: 60-30% HP — faster, spread rockets, spawns grunts
        {
            .healthThreshold = 0.6f,
            .moveSpeed = 5.0f,
            .attackCooldown = 1.5f,
            .attackPattern = "spread",
            .enterSound = "boss_enrage",
            .spawnWaveIndex = 0       // Triggers spawn wave 0
        },
        // Phase 3: 30-0% HP — fast, spiral + slam, spawns knights
        {
            .healthThreshold = 0.3f,
            .moveSpeed = 7.0f,
            .attackCooldown = 1.0f,
            .attackPattern = "spiral",
            .enterSound = "boss_fury",
            .spawnWaveIndex = 1       // Triggers spawn wave 1
        }
    };

    registry.emplace<BossBrain>(boss, brain);

    return boss;
}
```

The boss is **entirely data**. Change the numbers and you have a different boss. Change the mesh name and you have a different visual. The bossSystem doesn't know or care which boss it's running — it just processes BossBrain components.

---

## C++ Concept: Designated Initialisers

```cpp
BossPhase phase = {
    .healthThreshold = 0.6f,
    .moveSpeed = 5.0f,
    .attackCooldown = 1.5f,
    .attackPattern = "spread",
    .enterSound = "boss_enrage",
    .spawnWaveIndex = 0
};
```

C++20 added **designated initialisers** — you can name each field when initialising a struct. This makes the code self-documenting: you can see exactly what each value means without counting positions.

Rules:
- Fields must be in declaration order
- You can skip fields (they get default-initialised)
- Only works with aggregate types (structs with no constructors)

This is the same feature C has had since C99 — C++ finally adopted it.

---

## What's Next

In **Chapter 27**, we'll build a developer console — an in-game command line for toggling god mode, spawning entities, teleporting, and rendering debug visuals like collision boxes and nav graphs. The single most useful tool for developing and debugging everything we've built.
