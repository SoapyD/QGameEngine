# Chapter 45a: Particle System Cleanup

> **Prerequisites:** Chapter 45 (Advanced Particle Physics & Rendering) completed. You should have a working particle system with collision response, force fields (drag, wind, turbulence, point attractors/repulsors), particle rotation, flipbook texture animation, trails/ribbons, and soft particles. Your `Particle` struct has the extended fields from Chapter 45 and your `ParticleEmitterDef` struct exists from Chapter 20a.

---

## Time for Another Cleanup

You know the pattern by now. Chapters 5a, 10a, 15a, 20a, 25a, 30a, 35a, and 40a each followed the same rhythm: the features work, the code does not scale. Chapter 45 added six major features to the particle system -- collision, force fields, rotation, flipbook animation, trails, and soft particles. Every one of them works. And every one of them introduced structural problems that will make Chapter 46's data-driven particle effects painful to build on top of.

Let us take inventory. Open your `particle_system.cpp`, `particle_system.h`, and the files you touched in Chapters 41 through 45. You will find something like this:

**Problem 1: Force fields are loose parameters.**

```cpp
// particle_system.cpp — force fields passed as a vector parameter
void updateParticlePhysics(Particle& p, float dt, float currentTime,
                           const WindSettings& wind,
                           const std::vector<ForceField>& forceFields)
{
    glm::vec3 acceleration = glm::vec3(0.0f, -9.81f, 0.0f);
    acceleration += -p.drag * p.velocity;
    acceleration += wind.direction * wind.strength;
    if (p.drag > 0.0f) {
        acceleration += computeTurbulence(p.position, currentTime, 1.5f, 3.0f);
    }
    for (const auto& field : forceFields) {
        acceleration += computeForceFieldEffect(p, field);
    }
    p.velocity += acceleration * dt;
}
```

Every caller must gather force fields into a vector and pass it down. Adding a force field means finding every call site that constructs this vector. In Chapter 45 we only had one call site, so this was fine. But Chapter 46 will have emitters spawning particles from ECS entities, and those emitters should not need to know which force fields exist in the scene. Force fields are spatial entities -- they should live in the ECS registry, and the particle system should query for them.

**Problem 2: Turbulence constants are hardcoded magic numbers.**

```cpp
acceleration += computeTurbulence(p.position, currentTime, 1.5f, 3.0f);
// 1.5f = frequency (spatial scale), 3.0f = amplitude (force strength)
```

These values work well for smoke. They are wrong for fire (which needs tighter, stronger turbulence) and wrong for magical effects (which need broad, gentle swirling). The frequency and amplitude should be per-effect configuration, not buried in the update loop.

**Problem 3: Main particles and trail particles live in separate pools with duplicated logic.**

```cpp
// Separate pools with separate update paths
std::vector<Particle>      particlePool(4096);
std::vector<Particle>      collisionPool(256);
std::vector<TrailParticle> trailPool(64);
```

The update loop for `collisionPool` duplicates the age/kill/physics/position logic from `particlePool`, just adding a collision check. The `TrailParticle` wraps a `Particle` and duplicates everything again. Three pools, three update loops, three places to fix bugs. We need a single pool class that handles feature flags cleanly.

**Problem 4: The `ParticleEmitterDef` from Chapter 20a does not include any Chapter 45 properties.**

```cpp
// From Chapter 20a — the current state of ParticleEmitterDef
struct ParticleEmitterDef {
    glm::vec3 velocityMin, velocityMax;
    glm::vec4 colorStart, colorEnd;
    float sizeStart, sizeEnd;
    float lifetimeMin, lifetimeMax;
    int burstCount;
};
```

None of the Chapter 45 properties are here: no collision settings, no rotation range, no drag, no trail configuration, no turbulence parameters. Every effect that uses the new features sets them manually after spawning. Chapter 46 will define effects in JSON files -- it needs every parameter in the def struct.

Here is our plan:

| Problem | Solution |
|---|---|
| Force fields as loose `std::vector` parameter | `ForceField` ECS component with `ForceFieldType` enum; particle system queries registry |
| Turbulence constants hardcoded `1.5f, 3.0f` | Turbulence frequency/amplitude moved to `ParticleEffectDef` |
| Separate pools with duplicated update logic | `ParticlePool` class with feature flags (`hasCollision`, `hasTrails`, `hasRotation`) |
| `ParticleEmitterDef` missing Ch45 properties | Extended `ParticleEffectDef` struct with all properties, predefined defs updated |

---

## Step 1: Force Fields as ECS Entities

### The Problem in Detail

In Chapter 45, a `ForceField` is a plain struct that exists only as long as someone holds a reference to it. Want to place a wind zone near a vent? You create a `ForceField` in some ad-hoc vector, pass that vector to the particle update, and hope nothing else needs it. Want to visualise force fields in the editor? You cannot -- they are not entities, they have no transform component, and the debug renderer does not know about them.

Force fields are spatial things in the world. They have a position, a radius, and a type. They belong in the ECS registry alongside every other spatial thing.

### ForceFieldType Enum

The old `ForceField` struct conflated type with a sign convention -- positive strength meant attract, negative meant repel. That is clever but opaque. We make the type explicit:

```cpp
// engine/ecs/components.h — new ForceFieldType enum

enum class ForceFieldType {
    PointAttractor,   // pulls particles toward position
    PointRepulsor,    // pushes particles away from position
    Wind,             // constant directional force within volume
    Turbulence,       // noise-based chaotic force within volume
    Vortex            // swirls particles around an axis
};
```

### ForceFieldShape Enum

Force fields need a shape to define their area of influence:

```cpp
// engine/ecs/components.h — new ForceFieldShape enum

enum class ForceFieldShape {
    Sphere,           // radial falloff from centre
    Box               // axis-aligned box with edge falloff
};
```

### The ForceField Component

```cpp
// engine/ecs/components.h — new ForceField component
// Replaces the old ForceField struct from Chapter 45

struct ForceField {
    ForceFieldType type     = ForceFieldType::PointAttractor;
    ForceFieldShape shape   = ForceFieldShape::Sphere;
    float strength          = 10.0f;    // force magnitude (always positive now)
    float radius            = 5.0f;     // sphere radius or box half-extent
    glm::vec3 direction     = glm::vec3(0.0f, 0.0f, 1.0f);  // for Wind and Vortex axis
    float turbulenceFreq    = 1.5f;     // spatial frequency (Turbulence type only)
    float turbulenceAmp     = 3.0f;     // force amplitude (Turbulence type only)
};
```

The entity already has a `Transform` component (from Chapter 5) that provides position. The `ForceField` component adds the force-specific data. The `direction` field is used by `Wind` (wind direction) and `Vortex` (spin axis). `PointAttractor` and `PointRepulsor` do not use it -- they pull/push radially from the transform position.

Notice that `turbulenceFreq` and `turbulenceAmp` have moved here from the hardcoded `1.5f, 3.0f` in the update loop. Each turbulence field entity can now have its own frequency and amplitude. A vent near a furnace can have tight, strong turbulence. A gentle breeze zone can have broad, weak turbulence.

### Computing Force from a ForceField Entity

The old `computeForceFieldEffect()` handled only point attractor/repulsor. We expand it to handle all five types:

```cpp
// engine/particles/force_field_utils.h
#pragma once

#include "engine/ecs/components.h"
#include "engine/particles/particle_noise.h"  // ParticleNoise::noise3D from Ch 45

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>

namespace ForceFieldUtils {

// ─── Falloff within the field's volume ──────────────────────────
// Returns 1.0 at the centre, 0.0 at the edge, 0.0 outside.

inline float computeFalloff(const glm::vec3& particlePos,
                            const glm::vec3& fieldPos,
                            float radius,
                            ForceFieldShape shape)
{
    if (shape == ForceFieldShape::Sphere) {
        float dist = glm::length(particlePos - fieldPos);
        if (dist >= radius) return 0.0f;
        return 1.0f - (dist / radius);
    }

    // Box: axis-aligned, half-extent = radius on all axes
    glm::vec3 delta = glm::abs(particlePos - fieldPos);
    if (delta.x >= radius || delta.y >= radius || delta.z >= radius) {
        return 0.0f;
    }
    float maxAxis = std::max({delta.x, delta.y, delta.z});
    return 1.0f - (maxAxis / radius);
}

// ─── Compute the force vector from one ForceField entity ────────

inline glm::vec3 computeForce(const glm::vec3& particlePos,
                               const glm::vec3& fieldPos,
                               const ForceField& field,
                               float currentTime)
{
    float falloff = computeFalloff(particlePos, fieldPos, field.radius, field.shape);
    if (falloff <= 0.0f) return glm::vec3(0.0f);

    switch (field.type) {

        case ForceFieldType::PointAttractor: {
            glm::vec3 toField = fieldPos - particlePos;
            float dist = glm::length(toField);
            if (dist < 0.001f) return glm::vec3(0.0f);
            return (toField / dist) * field.strength * falloff;
        }

        case ForceFieldType::PointRepulsor: {
            glm::vec3 away = particlePos - fieldPos;
            float dist = glm::length(away);
            if (dist < 0.001f) return glm::vec3(0.0f);
            return (away / dist) * field.strength * falloff;
        }

        case ForceFieldType::Wind: {
            return field.direction * field.strength * falloff;
        }

        case ForceFieldType::Turbulence: {
            float offsetTime = currentTime * 0.7f;
            float freq = field.turbulenceFreq;
            float amp  = field.turbulenceAmp;

            float fx = ParticleNoise::noise3D(
                particlePos.x * freq + offsetTime,
                particlePos.y * freq,
                particlePos.z * freq);
            float fy = ParticleNoise::noise3D(
                particlePos.x * freq,
                particlePos.y * freq + offsetTime + 31.416f,
                particlePos.z * freq);
            float fz = ParticleNoise::noise3D(
                particlePos.x * freq,
                particlePos.y * freq,
                particlePos.z * freq + offsetTime + 67.123f);

            return glm::vec3(fx, fy, fz) * amp * falloff;
        }

        case ForceFieldType::Vortex: {
            // Cross product of the vortex axis with the direction to the particle
            // produces a tangential force that swirls particles around the axis
            glm::vec3 toParticle = particlePos - fieldPos;
            glm::vec3 tangent = glm::cross(field.direction, toParticle);
            float len = glm::length(tangent);
            if (len < 0.001f) return glm::vec3(0.0f);
            return (tangent / len) * field.strength * falloff;
        }
    }

    return glm::vec3(0.0f);
}

} // namespace ForceFieldUtils
```

### Spawning Force Field Entities

The old way: create a `ForceField` struct, push it into a vector, pass that vector to `updateParticlePhysics()`.

The new way: create an entity with `Transform` and `ForceField` components. The particle system finds them automatically.

```cpp
// Before (Chapter 45) — ad-hoc force field vector
std::vector<ForceField> forceFields;
ForceField attractor;
attractor.position = glm::vec3(5.0f, 2.0f, 0.0f);
attractor.strength = 10.0f;
attractor.radius = 5.0f;
forceFields.push_back(attractor);
// ... pass forceFields to every update call ...

// After (Chapter 45a) — ECS entity
auto ventField = registry.create();
registry.emplace<Transform>(ventField, glm::vec3(5.0f, 2.0f, 0.0f));
registry.emplace<ForceField>(ventField,
    ForceFieldType::PointAttractor,  // type
    ForceFieldShape::Sphere,         // shape
    10.0f,                           // strength
    5.0f                             // radius
);
// The particle system queries the registry — no manual passing needed.
```

Force fields are now first-class entities. You can attach them to moving platforms (the transform updates automatically through the physics system), toggle them on and off by adding/removing the component, and visualise them in a debug renderer by iterating the same view the particle system uses.

### Removing the Old WindSettings Struct

The global `WindSettings` struct from Chapter 45 is now redundant. A wind zone is just a `ForceField` entity with `ForceFieldType::Wind`:

```cpp
// Before (Chapter 45):
WindSettings wind;
wind.direction = glm::vec3(1.0f, 0.0f, 0.0f);
wind.strength = 2.0f;

// After (Chapter 45a):
auto windZone = registry.create();
registry.emplace<Transform>(windZone, glm::vec3(0.0f));  // centred at origin
registry.emplace<ForceField>(windZone,
    ForceFieldType::Wind,
    ForceFieldShape::Box,
    2.0f,                                    // strength
    100.0f,                                  // radius (large box = global wind)
    glm::vec3(1.0f, 0.0f, 0.0f)             // direction
);
```

For truly global wind (no falloff, affects everything), set a very large radius. The falloff at 100 units from centre is effectively zero only at the very edge -- for a level that fits inside 200x200 units, this is indistinguishable from global wind.

Delete the `WindSettings` struct from `components.h`. It is no longer needed.

---

## Step 2: Unified Particle Pool

### The Problem in Detail

Chapter 45 recommended separating particles into three pools:

```cpp
std::vector<Particle>      particlePool(4096);    // basic particles
std::vector<Particle>      collisionPool(256);     // collision-enabled
std::vector<TrailParticle> trailPool(64);          // trail particles
```

This was a reasonable performance strategy. But the update logic is nearly identical across all three:

```cpp
// particlePool update
for (auto& p : particlePool) {
    if (!p.active) continue;
    p.age += dt;
    if (p.age >= p.lifetime) { p.active = false; continue; }
    // physics, position, rotation...
}

// collisionPool update — same thing, plus collision check
for (auto& p : collisionPool) {
    if (!p.active) continue;
    p.age += dt;
    if (p.age >= p.lifetime) { p.active = false; continue; }
    // physics, position, rotation...
    updateParticleCollision(p, dt, spatialHash, registry);  // only difference
}

// trailPool update — same thing again, plus trail recording
for (auto& tp : trailPool) {
    if (!tp.base.active) continue;
    tp.base.age += dt;
    // ... duplicated physics on tp.base ...
    recordTrailPosition(tp);  // only difference
}
```

Three copies of the same age/kill/physics/position code. Fix a bug in one, forget the others. We unify this into a single `ParticlePool` class.

### Feature Flags

The key insight: most particles do not need collision or trails. Instead of paying the cost of checking `if (p.collisionEnabled)` on 4096 particles every frame, we set feature flags on the pool itself. A pool either supports collision or it does not. The update loop compiles the check into a branch that covers the entire pool, not each individual particle.

```cpp
// engine/particles/particle_pool.h
#pragma once

#include "engine/ecs/components.h"

#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

// ─── Feature flags for a particle pool ──────────────────────────
// These are set once at pool creation and determine which optional
// update steps run. This avoids per-particle branching: if a pool
// does not support collision, the collision loop never runs.

struct PoolFeatures {
    bool hasCollision = false;
    bool hasTrails    = false;
    bool hasRotation  = false;
};
```

### Trail Data — Alongside, Not Separate

In Chapter 45, trail particles used a separate `TrailParticle` struct that wrapped a `Particle`. This forced the trail pool to duplicate the update logic. Instead, we store trail data in a parallel array inside the pool. Particle at index `i` has its trail data at trail index `i`.

```cpp
// engine/particles/particle_pool.h (continued)

constexpr int TRAIL_HISTORY_SIZE = 16;

struct TrailData {
    glm::vec3 positionHistory[TRAIL_HISTORY_SIZE];
    int       historyWriteIndex = 0;
    int       historyCount      = 0;
    float     trailWidth        = 0.2f;
};
```

### The ParticlePool Class

```cpp
// engine/particles/particle_pool.h (continued)

class ParticlePool {
public:
    // ─── Construction ────────────────────────────────────────
    // capacity: maximum number of simultaneous particles
    // features: which optional systems this pool supports

    ParticlePool(int capacity, const PoolFeatures& features)
        : m_features(features)
    {
        m_particles.resize(capacity);

        if (features.hasTrails) {
            m_trailData.resize(capacity);
        }
    }

    // ─── Acquire a particle slot ─────────────────────────────
    // Scans for an inactive particle and returns a pointer to it,
    // or nullptr if the pool is full. The caller initialises the
    // particle's properties after acquisition.

    Particle* acquire()
    {
        for (int i = 0; i < static_cast<int>(m_particles.size()); ++i) {
            if (!m_particles[i].active) {
                m_particles[i] = Particle{};  // reset to defaults
                m_particles[i].active = true;

                if (m_features.hasTrails) {
                    m_trailData[i] = TrailData{};  // reset trail
                }

                return &m_particles[i];
            }
        }
        return nullptr;  // pool exhausted
    }

    // ─── Access ──────────────────────────────────────────────

    Particle& getParticle(int index)             { return m_particles[index]; }
    const Particle& getParticle(int index) const { return m_particles[index]; }

    TrailData& getTrailData(int index)             { return m_trailData[index]; }
    const TrailData& getTrailData(int index) const { return m_trailData[index]; }

    int capacity() const { return static_cast<int>(m_particles.size()); }
    const PoolFeatures& features() const { return m_features; }

    // ─── Particle and trail data (for rendering) ─────────────

    const std::vector<Particle>& particles() const { return m_particles; }
    const std::vector<TrailData>& trailData() const { return m_trailData; }
    std::vector<Particle>& particles() { return m_particles; }
    std::vector<TrailData>& trailData() { return m_trailData; }

    // ─── Count active particles (diagnostic) ─────────────────

    int activeCount() const
    {
        int count = 0;
        for (const auto& p : m_particles) {
            if (p.active) ++count;
        }
        return count;
    }

private:
    std::vector<Particle>  m_particles;
    std::vector<TrailData> m_trailData;   // empty if !hasTrails
    PoolFeatures           m_features;
};
```

### Creating Pools

Replace the three separate vectors with typed pools:

```cpp
// Before (Chapter 45):
std::vector<Particle>      particlePool(4096);
std::vector<Particle>      collisionPool(256);
std::vector<TrailParticle> trailPool(64);

// After (Chapter 45a):
ParticlePool mainPool(4096, { .hasCollision = false, .hasTrails = false, .hasRotation = true  });
ParticlePool debrisPool(256, { .hasCollision = true,  .hasTrails = false, .hasRotation = true  });
ParticlePool trailPool(64,  { .hasCollision = false, .hasTrails = true,  .hasRotation = false });
```

The pools are named for their purpose, the feature flags document what each pool supports, and the update code is unified. Let us write that update code.

### Recording Trail Positions

The trail recording function from Chapter 45 is unchanged, but it now operates on the parallel `TrailData` array:

```cpp
// engine/particles/particle_pool.h — inline in ParticlePool or free function

inline void recordTrailPosition(const Particle& p, TrailData& trail)
{
    trail.positionHistory[trail.historyWriteIndex] = p.position;
    trail.historyWriteIndex = (trail.historyWriteIndex + 1) % TRAIL_HISTORY_SIZE;
    if (trail.historyCount < TRAIL_HISTORY_SIZE) {
        trail.historyCount++;
    }
}
```

---

## Step 3: Extended ParticleEffectDef

### The Problem in Detail

Chapter 20a introduced `ParticleEmitterDef` with basic spawn properties: velocity range, colour, size, lifetime, burst count. Chapter 45 then added collision, rotation, drag, trails, turbulence, flipbook animation, and force fields -- but none of those properties made it into the def struct. Every effect that uses the new features sets them manually:

```cpp
// Current state — manual property setting after spawn
Particle* p = mainPool.acquire();
if (p) {
    p->position = emitterPos;
    p->velocity = randomInRange(EXPLOSION_DEF.velocityMin, EXPLOSION_DEF.velocityMax);
    p->colorStart = EXPLOSION_DEF.colorStart;
    // ... other 20a properties from the def ...

    // Ch 45 properties — set manually, not in the def
    p->drag = 2.0f;
    p->rotation = randomFloat(0.0f, glm::two_pi<float>());
    p->angularVelocity = randomFloat(-3.0f, 3.0f);
    p->collisionEnabled = false;
    p->flipbookColumns = 4;
    p->flipbookRows = 4;
}
```

This is the same "hardcoded constants scattered in spawn functions" problem we cleaned up in every previous `a` chapter. We need all particle properties in one struct.

### Renaming: ParticleEmitterDef to ParticleEffectDef

The name `ParticleEmitterDef` from Chapter 20a described the emitter. But what we really want is a definition of the entire effect: spawn properties, physics properties, visual properties, and optional feature configuration. We rename it to `ParticleEffectDef` to reflect this broader scope.

```cpp
// engine/particles/particle_effect_def.h
#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <string>

// ─── ParticleEffectDef ──────────────────────────────────────────
// Complete definition of a particle effect. Contains every tunable
// parameter that was previously scattered across spawn functions,
// hardcoded constants, and manual post-spawn property setting.
//
// Chapter 46 will load these from JSON files. For now, we define
// them as C++ constants (updating the predefined defs from 20a).

struct ParticleEffectDef {

    // ─── Identity ────────────────────────────────────────────
    std::string name;

    // ─── Spawn properties (from Chapter 20a) ────────────────
    glm::vec3 velocityMin      = glm::vec3(-1.0f, 1.0f, -1.0f);
    glm::vec3 velocityMax      = glm::vec3( 1.0f, 5.0f,  1.0f);
    glm::vec4 colorStart       = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    glm::vec4 colorEnd         = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
    float     sizeStart        = 0.1f;
    float     sizeEnd          = 0.0f;
    float     lifetimeMin      = 0.5f;
    float     lifetimeMax      = 1.5f;
    int       burstCount       = 10;

    // ─── Physics (Chapter 45 — previously hardcoded) ────────
    float     drag             = 0.0f;
    float     gravityScale     = 1.0f;   // multiplier on gravity (0 = no gravity)

    // ─── Turbulence (Chapter 45 — previously hardcoded 1.5f, 3.0f) ──
    // Per-effect turbulence applied to all particles from this def.
    // Set both to 0.0f to disable per-effect turbulence (force field
    // entities still apply their own turbulence independently).
    float     turbulenceFreq   = 0.0f;
    float     turbulenceAmp    = 0.0f;

    // ─── Collision (Chapter 45 — previously manual per-particle) ──
    bool      collisionEnabled = false;
    float     restitution      = 0.5f;
    float     friction         = 0.3f;
    bool      killOnCollision  = false;

    // ─── Rotation (Chapter 45 — previously manual per-particle) ──
    float     rotationMin      = 0.0f;
    float     rotationMax      = 0.0f;
    float     angularVelMin    = 0.0f;
    float     angularVelMax    = 0.0f;

    // ─── Flipbook (Chapter 45 — previously manual per-particle) ──
    int       flipbookColumns  = 1;
    int       flipbookRows     = 1;
    std::string textureName;           // looked up in ResourceManager

    // ─── Trails (Chapter 45 — previously in separate TrailParticle) ──
    bool      trailEnabled     = false;
    float     trailWidth       = 0.2f;

    // ─── Pool hint ───────────────────────────────────────────
    // Which pool type this effect should spawn into. The particle
    // system uses this to route acquire() calls to the correct pool.
    enum class PoolHint {
        Main,       // basic particles (smoke, sparks, muzzle flash)
        Debris,     // collision-enabled particles (chunks, blood, shells)
        Trail       // trail particles (tracers, rocket exhaust)
    };
    PoolHint poolHint = PoolHint::Main;
};
```

### Updated Predefined Effect Definitions

Chapter 20a defined `MUZZLE_FLASH_DEF`, `EXPLOSION_DEF`, and `SPARK_DEF`. We update these to include all the Chapter 45 properties. Every magic number that was previously buried in a spawn function is now in the def.

```cpp
// engine/particles/particle_effect_defs.h
#pragma once

#include "engine/particles/particle_effect_def.h"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

// ─── Predefined effect definitions ──────────────────────────────
// Updated from Chapter 20a to include Chapter 45 properties.
// These are the "before" for Chapter 46's data-driven JSON loading.

namespace ParticleEffects {

inline const ParticleEffectDef MUZZLE_FLASH_DEF = [] {
    ParticleEffectDef def;
    def.name            = "muzzle_flash";
    def.velocityMin     = glm::vec3(-0.5f, -0.5f, 5.0f);
    def.velocityMax     = glm::vec3( 0.5f,  0.5f, 8.0f);
    def.colorStart      = glm::vec4(1.0f, 0.9f, 0.5f, 1.0f);
    def.colorEnd        = glm::vec4(1.0f, 0.4f, 0.0f, 0.0f);
    def.sizeStart       = 0.05f;
    def.sizeEnd         = 0.15f;
    def.lifetimeMin     = 0.03f;
    def.lifetimeMax     = 0.08f;
    def.burstCount      = 5;
    def.drag            = 1.0f;
    def.gravityScale    = 0.0f;    // no gravity on muzzle flash
    def.rotationMin     = 0.0f;
    def.rotationMax     = glm::two_pi<float>();
    def.angularVelMin   = -5.0f;
    def.angularVelMax   = 5.0f;
    def.poolHint        = ParticleEffectDef::PoolHint::Main;
    return def;
}();

inline const ParticleEffectDef EXPLOSION_DEF = [] {
    ParticleEffectDef def;
    def.name            = "explosion";
    def.velocityMin     = glm::vec3(-3.0f, 1.0f, -3.0f);
    def.velocityMax     = glm::vec3( 3.0f, 6.0f,  3.0f);
    def.colorStart      = glm::vec4(1.0f, 0.8f, 0.2f, 1.0f);
    def.colorEnd        = glm::vec4(0.3f, 0.1f, 0.0f, 0.0f);
    def.sizeStart       = 0.2f;
    def.sizeEnd         = 0.8f;
    def.lifetimeMin     = 0.4f;
    def.lifetimeMax     = 1.0f;
    def.burstCount      = 30;
    def.drag            = 2.0f;
    def.gravityScale    = 0.3f;
    def.turbulenceFreq  = 1.5f;   // was hardcoded in update loop
    def.turbulenceAmp   = 3.0f;   // was hardcoded in update loop
    def.rotationMin     = 0.0f;
    def.rotationMax     = glm::two_pi<float>();
    def.angularVelMin   = -3.0f;
    def.angularVelMax   = 3.0f;
    def.flipbookColumns = 4;
    def.flipbookRows    = 4;
    def.textureName     = "explosion_atlas";
    def.poolHint        = ParticleEffectDef::PoolHint::Main;
    return def;
}();

inline const ParticleEffectDef SPARK_DEF = [] {
    ParticleEffectDef def;
    def.name              = "spark";
    def.velocityMin       = glm::vec3(-2.0f, 1.0f, -2.0f);
    def.velocityMax       = glm::vec3( 2.0f, 4.0f,  2.0f);
    def.colorStart        = glm::vec4(1.0f, 0.9f, 0.5f, 1.0f);
    def.colorEnd          = glm::vec4(1.0f, 0.3f, 0.0f, 0.0f);
    def.sizeStart         = 0.02f;
    def.sizeEnd           = 0.01f;
    def.lifetimeMin       = 0.2f;
    def.lifetimeMax       = 0.6f;
    def.burstCount        = 15;
    def.drag              = 0.5f;
    def.gravityScale      = 1.0f;
    def.collisionEnabled  = true;    // sparks bounce off walls
    def.restitution       = 0.6f;
    def.friction          = 0.2f;
    def.killOnCollision   = false;
    def.rotationMin       = 0.0f;
    def.rotationMax       = glm::two_pi<float>();
    def.angularVelMin     = -8.0f;
    def.angularVelMax     = 8.0f;
    def.poolHint          = ParticleEffectDef::PoolHint::Debris;
    return def;
}();

inline const ParticleEffectDef BLOOD_SPLAT_DEF = [] {
    ParticleEffectDef def;
    def.name              = "blood_splat";
    def.velocityMin       = glm::vec3(-1.5f, 0.5f, -1.5f);
    def.velocityMax       = glm::vec3( 1.5f, 3.0f,  1.5f);
    def.colorStart        = glm::vec4(0.6f, 0.0f, 0.0f, 1.0f);
    def.colorEnd          = glm::vec4(0.3f, 0.0f, 0.0f, 0.0f);
    def.sizeStart         = 0.03f;
    def.sizeEnd           = 0.01f;
    def.lifetimeMin       = 0.3f;
    def.lifetimeMax       = 0.8f;
    def.burstCount        = 12;
    def.drag              = 1.0f;
    def.gravityScale      = 1.0f;
    def.collisionEnabled  = true;
    def.restitution       = 0.1f;
    def.friction          = 0.8f;
    def.killOnCollision   = true;    // splats leave decals on hit
    def.poolHint          = ParticleEffectDef::PoolHint::Debris;
    return def;
}();

inline const ParticleEffectDef SMOKE_DEF = [] {
    ParticleEffectDef def;
    def.name            = "smoke";
    def.velocityMin     = glm::vec3(-0.3f, 0.5f, -0.3f);
    def.velocityMax     = glm::vec3( 0.3f, 2.0f,  0.3f);
    def.colorStart      = glm::vec4(0.5f, 0.5f, 0.5f, 0.4f);
    def.colorEnd        = glm::vec4(0.3f, 0.3f, 0.3f, 0.0f);
    def.sizeStart       = 0.1f;
    def.sizeEnd         = 0.6f;
    def.lifetimeMin     = 1.0f;
    def.lifetimeMax     = 2.5f;
    def.burstCount      = 8;
    def.drag            = 3.0f;
    def.gravityScale    = -0.1f;     // slight upward drift
    def.turbulenceFreq  = 1.0f;     // broad, gentle swirls
    def.turbulenceAmp   = 2.0f;
    def.rotationMin     = 0.0f;
    def.rotationMax     = glm::two_pi<float>();
    def.angularVelMin   = -1.0f;
    def.angularVelMax   = 1.0f;
    def.poolHint        = ParticleEffectDef::PoolHint::Main;
    return def;
}();

inline const ParticleEffectDef BULLET_TRACER_DEF = [] {
    ParticleEffectDef def;
    def.name            = "bullet_tracer";
    def.velocityMin     = glm::vec3(0.0f, 0.0f, 50.0f);
    def.velocityMax     = glm::vec3(0.0f, 0.0f, 50.0f);
    def.colorStart      = glm::vec4(1.0f, 0.9f, 0.4f, 1.0f);
    def.colorEnd        = glm::vec4(1.0f, 0.5f, 0.1f, 0.0f);
    def.sizeStart       = 0.01f;
    def.sizeEnd         = 0.005f;
    def.lifetimeMin     = 0.1f;
    def.lifetimeMax     = 0.2f;
    def.burstCount      = 1;
    def.drag            = 0.0f;
    def.gravityScale    = 0.0f;
    def.trailEnabled    = true;
    def.trailWidth      = 0.05f;
    def.poolHint        = ParticleEffectDef::PoolHint::Trail;
    return def;
}();

} // namespace ParticleEffects
```

### Spawning from a ParticleEffectDef

With all properties in the def, spawning becomes a single function that reads the def and writes the particle. No more manual property setting at each call site:

```cpp
// engine/particles/particle_spawner.h
#pragma once

#include "engine/particles/particle_pool.h"
#include "engine/particles/particle_effect_def.h"
#include "engine/core/math_utils.h"  // MathUtils::randomFloat, randomVec3 from Ch 20a

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace ParticleSpawner {

// ─── Spawn a burst of particles from a def ──────────────────────
// Acquires burstCount particles from the given pool, initialising
// each one from the def's property ranges. Returns the number of
// particles actually spawned (may be less if pool is full).

inline int spawnBurst(ParticlePool& pool,
                      const ParticleEffectDef& def,
                      const glm::vec3& position)
{
    int spawned = 0;

    for (int i = 0; i < def.burstCount; ++i) {
        Particle* p = pool.acquire();
        if (!p) break;  // pool full

        // Position
        p->position = position;

        // Velocity (random within range)
        p->velocity = MathUtils::randomVec3(def.velocityMin, def.velocityMax);

        // Colour
        p->colorStart = def.colorStart;
        p->colorEnd   = def.colorEnd;

        // Size
        p->sizeStart = def.sizeStart;
        p->sizeEnd   = def.sizeEnd;

        // Lifetime
        p->lifetime = MathUtils::randomFloat(def.lifetimeMin, def.lifetimeMax);
        p->age      = 0.0f;

        // Physics (Ch 45 — now from def)
        p->drag = def.drag;

        // Collision (Ch 45 — now from def)
        p->collisionEnabled = def.collisionEnabled;
        p->restitution      = def.restitution;
        p->friction         = def.friction;
        p->killOnCollision  = def.killOnCollision;

        // Rotation (Ch 45 — now from def)
        p->rotation        = MathUtils::randomFloat(def.rotationMin, def.rotationMax);
        p->angularVelocity = MathUtils::randomFloat(def.angularVelMin, def.angularVelMax);

        // Flipbook (Ch 45 — now from def)
        p->flipbookColumns = def.flipbookColumns;
        p->flipbookRows    = def.flipbookRows;

        // Trail (Ch 45 — now from def)
        p->trailEnabled = def.trailEnabled;

        ++spawned;
    }

    return spawned;
}

} // namespace ParticleSpawner
```

Compare this to the Chapter 45 state where every spawn function had its own copy of these assignments with hardcoded values. Now there is one spawn function, and all variation comes from the def.

---

## Step 4: Updated Particle System

### The Unified Update Function

The particle system update now queries the registry for force fields instead of receiving them as a parameter. It operates on a `ParticlePool` and branches on pool feature flags rather than per-particle flags.

```cpp
// engine/particles/particle_system.h
#pragma once

#include "engine/particles/particle_pool.h"
#include "engine/particles/particle_effect_def.h"
#include "engine/particles/force_field_utils.h"
#include "engine/particles/particle_noise.h"
#include "engine/ecs/components.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>

// ─── Forward declarations ────────────────────────────────────────
struct SpatialHash;
void spawnDecal(entt::registry& registry, const glm::vec3& position,
                const glm::vec3& normal, DecalType type);

struct RaycastHit {
    glm::vec3 point;
    glm::vec3 normal;
    float     distance;
    bool      hit;
};

RaycastHit raycastWorld(const glm::vec3& origin, const glm::vec3& direction,
                        float maxDistance, const SpatialHash& spatialHash);

// ─── Collision response (unchanged from Ch 45) ──────────────────

inline void resolveCollision(Particle& p, float dt,
                             const SpatialHash& spatialHash,
                             entt::registry& registry)
{
    if (!p.collisionEnabled || !p.active) return;

    glm::vec3 movement = p.velocity * dt;
    float moveLength = glm::length(movement);
    if (moveLength < 0.0001f) return;

    glm::vec3 moveDir = movement / moveLength;
    RaycastHit hit = raycastWorld(p.position, moveDir, moveLength, spatialHash);
    if (!hit.hit) return;

    if (p.killOnCollision) {
        spawnDecal(registry, hit.point, hit.normal, DecalType::BloodSplat);
        p.active = false;
        return;
    }

    p.position = hit.point + hit.normal * 0.01f;

    float vDotN = glm::dot(p.velocity, hit.normal);
    if (vDotN >= 0.0f) return;

    glm::vec3 vNormal     = vDotN * hit.normal;
    glm::vec3 vTangential = p.velocity - vNormal;
    p.velocity = vTangential * (1.0f - p.friction) - vNormal * p.restitution;

    if (glm::length(p.velocity) < 0.1f) {
        p.velocity = glm::vec3(0.0f);
    }
}

// ─── Per-effect turbulence ──────────────────────────────────────
// Applies turbulence using the effect def's frequency and amplitude.
// This is the per-effect turbulence. Force field entities apply
// their own turbulence independently through the registry query.

inline glm::vec3 computeEffectTurbulence(const glm::vec3& position,
                                          float currentTime,
                                          float freq, float amp)
{
    if (freq <= 0.0f || amp <= 0.0f) return glm::vec3(0.0f);

    float offsetTime = currentTime * 0.7f;

    float fx = ParticleNoise::noise3D(
        position.x * freq + offsetTime,
        position.y * freq,
        position.z * freq);
    float fy = ParticleNoise::noise3D(
        position.x * freq,
        position.y * freq + offsetTime + 31.416f,
        position.z * freq);
    float fz = ParticleNoise::noise3D(
        position.x * freq,
        position.y * freq,
        position.z * freq + offsetTime + 67.123f);

    return glm::vec3(fx, fy, fz) * amp;
}
```

### The Core Update Loop

```cpp
// engine/particles/particle_system.h (continued)

// ─── Update a single particle pool ──────────────────────────────
// Queries the registry for ForceField entities. Uses pool feature
// flags to skip expensive steps on pools that don't need them.

inline void updatePool(ParticlePool& pool,
                       const ParticleEffectDef& effectDef,
                       float dt,
                       float currentTime,
                       entt::registry& registry,
                       const SpatialHash& spatialHash)
{
    const PoolFeatures& features = pool.features();

    // ─── Gather force fields from the registry (once per pool update) ──
    auto forceFieldView = registry.view<Transform, ForceField>();

    // ─── Update each active particle ────────────────────────
    for (int i = 0; i < pool.capacity(); ++i) {
        Particle& p = pool.particles()[i];
        if (!p.active) continue;

        // Age and kill
        p.age += dt;
        if (p.age >= p.lifetime) {
            p.active = false;
            continue;
        }

        // ── Force accumulation ──────────────────────────────
        glm::vec3 acceleration = glm::vec3(0.0f, -9.81f * effectDef.gravityScale, 0.0f);

        // Drag
        acceleration += -p.drag * p.velocity;

        // Per-effect turbulence (from the def, replaces hardcoded 1.5f/3.0f)
        acceleration += computeEffectTurbulence(
            p.position, currentTime,
            effectDef.turbulenceFreq, effectDef.turbulenceAmp);

        // Force field entities (queried from registry)
        for (auto [entity, transform, field] : forceFieldView.each()) {
            acceleration += ForceFieldUtils::computeForce(
                p.position, transform.position, field, currentTime);
        }

        // Integrate velocity
        p.velocity += acceleration * dt;

        // ── Collision (only if pool supports it) ────────────
        if (features.hasCollision) {
            resolveCollision(p, dt, spatialHash, registry);
        }

        // ── Position update (particle may have been killed by collision) ──
        if (p.active) {
            p.position += p.velocity * dt;
        }

        // ── Rotation (only if pool supports it) ─────────────
        if (features.hasRotation) {
            p.rotation += p.angularVelocity * dt;
        }

        // ── Trail recording (only if pool supports it) ──────
        if (features.hasTrails && p.active) {
            recordTrailPosition(p, pool.trailData()[i]);
        }
    }
}
```

Compare this to the Chapter 45 state. The old code had three separate loops for three pools, a `std::vector<ForceField>` parameter threaded through every call, hardcoded turbulence constants, and a separate `WindSettings` struct. The new code has one loop, one pool class, registry-queried force fields, and per-effect turbulence configuration.

### The System Entry Point

The particle system function that `main.cpp` calls each frame:

```cpp
// engine/particles/particle_system.h (continued)

// ─── ParticleSystemContext ──────────────────────────────────────
// Holds the pools and provides the top-level update. Stored as a
// registry context object (same pattern as PhysicsConfig from Ch 10a).

struct ParticleSystemContext {
    ParticlePool mainPool   { 4096, { false, false, true  } };
    ParticlePool debrisPool {  256, { true,  false, true  } };
    ParticlePool trailPool  {   64, { false, true,  false } };
};

inline void particleSystem(entt::registry& registry, float dt, float currentTime,
                           const SpatialHash& spatialHash)
{
    auto& ctx = registry.ctx().get<ParticleSystemContext>();

    // Each pool is updated with a default effect def that provides
    // baseline turbulence and gravity. Individual effects override
    // these when spawning — the per-particle drag and collision
    // properties are already set on the particles themselves.
    //
    // In Chapter 46, each pool will track which effect def its
    // particles belong to, enabling per-effect update parameters.
    // For now, we use a neutral baseline.

    ParticleEffectDef mainBaseline;
    mainBaseline.gravityScale   = 1.0f;
    mainBaseline.turbulenceFreq = 0.0f;  // no default turbulence on main pool
    mainBaseline.turbulenceAmp  = 0.0f;

    ParticleEffectDef debrisBaseline;
    debrisBaseline.gravityScale   = 1.0f;
    debrisBaseline.turbulenceFreq = 0.0f;
    debrisBaseline.turbulenceAmp  = 0.0f;

    ParticleEffectDef trailBaseline;
    trailBaseline.gravityScale   = 0.0f;
    trailBaseline.turbulenceFreq = 0.0f;
    trailBaseline.turbulenceAmp  = 0.0f;

    updatePool(ctx.mainPool,   mainBaseline,   dt, currentTime, registry, spatialHash);
    updatePool(ctx.debrisPool, debrisBaseline,  dt, currentTime, registry, spatialHash);
    updatePool(ctx.trailPool,  trailBaseline,   dt, currentTime, registry, spatialHash);
}
```

### Registering the Context

In `setupScene()` (or wherever you initialise the registry), register the context:

```cpp
// In setupScene() or main.cpp initialisation
registry.ctx().emplace<ParticleSystemContext>();
```

This follows the same pattern as `PhysicsConfig` from Chapter 10a -- engine subsystem state stored as a registry context object rather than a global.

---

## Step 5: Putting It All Together

### Before vs After — Full Comparison

Here is what a typical frame looked like in Chapter 45:

```cpp
// Chapter 45 — scattered state, manual parameter passing

// Globals / local state
WindSettings wind;
wind.direction = glm::vec3(1.0f, 0.0f, 0.0f);
wind.strength  = 2.0f;

std::vector<ForceField> forceFields;
// ... populate forceFields from somewhere ...

// Three separate update calls
for (auto& p : particlePool) {
    if (!p.active) continue;
    p.age += dt;
    if (p.age >= p.lifetime) { p.active = false; continue; }
    updateParticlePhysics(p, dt, currentTime, wind, forceFields);
    p.position += p.velocity * dt;
    p.rotation += p.angularVelocity * dt;
}

for (auto& p : collisionPool) {
    if (!p.active) continue;
    p.age += dt;
    if (p.age >= p.lifetime) { p.active = false; continue; }
    updateParticlePhysics(p, dt, currentTime, wind, forceFields);
    updateParticleCollision(p, dt, spatialHash, registry);
    if (p.active) p.position += p.velocity * dt;
    p.rotation += p.angularVelocity * dt;
}

for (auto& tp : trailPool) {
    if (!tp.base.active) continue;
    tp.base.age += dt;
    if (tp.base.age >= tp.base.lifetime) { tp.base.active = false; continue; }
    updateParticlePhysics(tp.base, dt, currentTime, wind, forceFields);
    tp.base.position += tp.base.velocity * dt;
    recordTrailPosition(tp);
}
```

And here is Chapter 45a:

```cpp
// Chapter 45a — unified, registry-driven

// Force fields are entities — no manual vector construction
// Wind is a ForceField entity — no separate WindSettings
// Pools are in ParticleSystemContext — no loose vectors

particleSystem(registry, dt, currentTime, spatialHash);
```

One line. The system queries force fields from the registry, iterates each pool with the unified update loop, and feature flags control which optional steps run. Spawning is equally clean:

```cpp
// Chapter 45 — manual property assignment
Particle* p = /* find inactive in particlePool */;
p->position = pos;
p->velocity = randomVec3(glm::vec3(-3, 1, -3), glm::vec3(3, 6, 3));
p->colorStart = glm::vec4(1.0f, 0.8f, 0.2f, 1.0f);
p->colorEnd   = glm::vec4(0.3f, 0.1f, 0.0f, 0.0f);
// ... 15 more lines of manual assignment ...
p->drag = 2.0f;
p->rotation = randomFloat(0.0f, 6.28f);
p->angularVelocity = randomFloat(-3.0f, 3.0f);

// Chapter 45a — one call
auto& ctx = registry.ctx().get<ParticleSystemContext>();
ParticleSpawner::spawnBurst(ctx.mainPool, ParticleEffects::EXPLOSION_DEF, position);
```

### File Summary

Here is every file touched or created in this chapter:

| File | Change |
|---|---|
| `engine/ecs/components.h` | Added `ForceFieldType`, `ForceFieldShape` enums and `ForceField` component. Removed `WindSettings` struct. Removed old `ForceField` struct. |
| `engine/particles/force_field_utils.h` | **New.** `ForceFieldUtils` namespace with `computeFalloff()` and `computeForce()`. Replaces `computeForceFieldEffect()` and `computeTurbulence()` from particle_system.cpp. |
| `engine/particles/particle_pool.h` | **New.** `PoolFeatures`, `TrailData`, `ParticlePool` class. Replaces the three separate `std::vector` pools. |
| `engine/particles/particle_effect_def.h` | **New.** `ParticleEffectDef` struct. Replaces and extends `ParticleEmitterDef` from Chapter 20a. |
| `engine/particles/particle_effect_defs.h` | **New.** Predefined defs in `ParticleEffects` namespace. Updates `MUZZLE_FLASH_DEF`, `EXPLOSION_DEF`, `SPARK_DEF` from 20a, adds `BLOOD_SPLAT_DEF`, `SMOKE_DEF`, `BULLET_TRACER_DEF`. |
| `engine/particles/particle_spawner.h` | **New.** `ParticleSpawner::spawnBurst()`. Replaces per-effect spawn functions. |
| `engine/particles/particle_system.h` | **Rewritten.** Unified `updatePool()`, `ParticleSystemContext`, `particleSystem()` entry point. Registry-queried force fields. |
| `engine/particles/particle_noise.h` | **Unchanged.** The `ParticleNoise` namespace from Chapter 45 is used as-is. |

---

## C++ Concept: Data-Oriented Design and Feature Flags

The `ParticlePool` class makes a design choice that is worth examining: feature flags are set per-pool, not per-particle. Why?

### The Per-Particle Approach

```cpp
// Per-particle branching
for (auto& p : particles) {
    if (!p.active) continue;
    // ... common update ...
    if (p.collisionEnabled) {
        resolveCollision(p, dt, spatialHash, registry);
    }
    if (p.trailEnabled) {
        recordTrailPosition(p, trailData[i]);
    }
    if (p.rotationEnabled) {
        p.rotation += p.angularVelocity * dt;
    }
}
```

This looks clean. One loop, one pool, branches per particle. But consider what happens on modern hardware.

CPUs fetch memory in **cache lines** -- 64-byte chunks. When you iterate a `std::vector<Particle>`, the CPU prefetches the next few particles into the L1 cache. This is fast because the access pattern is sequential and predictable. The CPU's prefetcher recognises the pattern and loads data before you ask for it.

But branches break the prefetcher's ability to speculate. If `collisionEnabled` is true for particle 47 but false for particle 48, the CPU cannot predict which path the branch will take. Worse, if the collision code touches `SpatialHash` data (which lives in a completely different memory region), the cache line that was holding the next particle gets evicted to make room for the spatial hash data. When the loop returns to particle 49, it has to reload it from a slower cache level.

### The Per-Pool Approach

```cpp
// Per-pool branching
if (features.hasCollision) {
    for (int i = 0; i < capacity; ++i) {
        Particle& p = particles[i];
        if (!p.active) continue;
        resolveCollision(p, dt, spatialHash, registry);
    }
}
```

Now the branch is evaluated once for the entire pool. Inside the loop, every particle follows the same code path. The branch predictor is happy (it predicts "taken" every time and is always right). The prefetcher is happy (sequential access, no surprises). The cache is less polluted because the spatial hash data stays resident across the entire collision loop instead of being evicted and reloaded 4096 times.

### The Trade-Off

The per-pool approach means you cannot have a single particle with collision in a pool that does not support it. You need to know at spawn time which pool to use. The `PoolHint` field in `ParticleEffectDef` handles this: the effect definition knows which features it needs, and `spawnBurst()` routes to the correct pool.

This is **data-oriented design**: organising data by how it is accessed (all collision particles together, all trail particles together) rather than by what it logically represents (all particles of one effect together). The logical grouping is convenient for the programmer. The access-pattern grouping is efficient for the hardware. In a particle system that processes thousands of particles every frame, hardware efficiency wins.

For a deeper treatment, Mike Acton's "Data-Oriented Design and C++" talk (CppCon 2014) remains the definitive introduction. The core principle: where there is one, there are many. If you process one collision particle, you process hundreds. Organise for the batch, not the individual.

---

## What's Next

The particle system is now structured for extensibility. Force fields are ECS entities that the particle system discovers through registry queries. Pools are feature-flagged classes with a single unified update path. Every tunable parameter lives in a `ParticleEffectDef` struct. The hardcoded turbulence constants, the scattered spawn logic, the duplicated update loops -- all gone.

More importantly, `ParticleEffectDef` is now a complete, self-contained description of a particle effect. Every property that Chapter 45 introduced is represented. This is exactly what Chapter 46 needs.

In **Chapter 46: Data-Driven Particles & Sub-Emitters**, we will:

- Load `ParticleEffectDef` structs from JSON files instead of defining them in C++
- Build a `ParticleEffectLibrary` (following the `AnimationLibrary` pattern from Chapter 25a) that caches loaded defs by name
- Implement sub-emitters: particles that spawn other particles on death or on collision (firework rockets that explode into sparks, torch flames that spawn smoke)
- Add continuous emission mode alongside burst mode (particles per second, not just burst count)
- Build a hot-reload system so artists can edit JSON effect files and see changes immediately without restarting the engine

The combination of this chapter's clean structure with Chapter 46's data-driven loading will give QEngine a particle system where new effects can be created entirely in data files -- no C++ changes, no recompilation, no programmer involvement. That is the goal.
