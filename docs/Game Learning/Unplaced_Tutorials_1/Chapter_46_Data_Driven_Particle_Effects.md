# Chapter 46: Data-Driven Particle Effects

## What You'll Learn
- Why hardcoded particle effects limit your team and your iteration speed
- Designing a ParticleEffectDef structure that captures every knob an artist needs to turn
- Emitter shapes: spawning particles from points, spheres, cones, boxes, and rings
- Colour and size curves: replacing start/end interpolation with multi-key gradients
- Sub-emitters: particles that spawn child effects on birth, death, or collision
- A complete JSON schema for defining effects, with seven realistic examples
- A ParticleEffectManager that loads, caches, and serves effect definitions
- A ParticleEmitter ECS component and system that drives emission at runtime
- Integrating data-driven effects with animation events, weapons, and damage
- Hot-reloading effect files for instant feedback during development
- C++ concepts: data-driven design, factory pattern, piecewise curve evaluation

---

## Where We Left Off

Over Chapters 20 and 45 we built a particle system with real physics. Particles collide with world geometry and bounce. Drag, wind, and turbulence make motion look natural. Flipbook animation gives fire and smoke proper texture detail. Trails follow fast-moving particles. Soft particles fade cleanly against surfaces. The rendering and simulation are solid.

But there is a problem. Every particle effect in the engine looks like this:

```cpp
void spawnExplosion(ParticlePool& pool, const glm::vec3& pos)
{
    for (int i = 0; i < 30; ++i) {
        Particle& p = pool.acquire();
        p.position     = pos;
        p.velocity     = randomDirection() * randomRange(2.0f, 8.0f);
        p.colorStart   = {1.0f, 0.8f, 0.2f, 1.0f};
        p.colorEnd     = {0.3f, 0.1f, 0.05f, 0.0f};
        p.sizeStart    = 0.3f;
        p.sizeEnd      = 1.2f;
        p.lifetime     = randomRange(0.4f, 1.0f);
        p.drag         = 1.5f;
        p.collisionEnabled = true;
        p.restitution  = 0.3f;
        // ... 20 more lines of setup
    }
    // Then a second loop for smoke, a third for flash...
}
```

Every distinct effect — muzzle flash, blood splatter, rocket trail, sparks on metal, sparks on stone, torch fire, rain splash — is a separate C++ function. Want to make the explosion smoke a little darker? Open the source, find the function, change a float, recompile, relink, restart, navigate to a spot in the level, trigger the explosion, evaluate. That cycle takes minutes. An artist who cannot write C++ cannot do it at all.

```
CURRENT WORKFLOW (hardcoded)
────────────────────────────────────────────────────────────────────
  Artist: "Can we make the blood darker and spray wider?"
  Programmer: *opens spawnBlood()... changes two floats... recompiles...*
  Programmer: "Okay, try it now." (3 minutes later)
  Artist: "Hmm, a little too wide. And can the droplets be smaller?"
  Programmer: *sighs... changes two more floats... recompiles...*
  (repeat 15 times per effect)

TARGET WORKFLOW (data-driven)
────────────────────────────────────────────────────────────────────
  Artist: *opens blood_splatter.json... changes "speedMax": 6 → 4...*
  Artist: *saves file... effect updates in-game immediately*
  Artist: *tweaks sizeStart, saves again, sees result in 0.5 seconds*
  Programmer: (working on something else entirely)
```

This chapter moves every particle effect definition out of C++ and into JSON data files. The engine loads them at startup, artists edit them with any text editor, and an optional hot-reload system picks up changes without restarting the game.

---

## ParticleEffectDef — The Effect Definition

We need a single data structure that fully describes a particle effect. Everything that was previously a hardcoded number in a spawn function becomes a field in this struct. Where a single value is too rigid, we use a range (min/max) so the system can randomise each particle.

### Emitter Shapes

Particles need to spawn somewhere. A point emitter concentrates everything at a single position. An explosion needs a sphere. A torch flame needs a cone pointing upward. Gunfire debris might need a box along a wall. A magic spell might need a ring.

```
EMITTER SHAPES (top-down and side views)

  POINT            SPHERE           CONE              BOX              RING
    *            . * . * .         \  |  /          ┌──────┐         *   *   *
  (all at        * . * . *         \ | /           │ *  * │        *         *
   origin)       . * . * .         \|/            │*   * │       *           *
                 * . * . *          *              │ * *  │        *         *
                  . * . * .        /|\             └──────┘         *   *   *
                               origin
                                                  particles        particles
                particles fill    particles       fill volume      on ring
                volume of sphere  in cone angle                    circumference
```

```cpp
// In src/engine/particles/particle_effect_def.h

#pragma once

#include <string>
#include <vector>
#include <glm/glm.hpp>

enum class EmitterShape {
    Point,
    Sphere,
    Cone,
    Box,
    Ring
};

struct EmitterShapeDef {
    EmitterShape shape = EmitterShape::Point;

    // Sphere
    float sphereRadius    = 1.0f;

    // Cone
    float coneAngle       = 30.0f;  // half-angle in degrees
    float coneRadius      = 0.0f;   // base radius (0 = pure point tip)

    // Box
    glm::vec3 boxHalfExtents = {0.5f, 0.5f, 0.5f};

    // Ring
    float ringRadius      = 1.0f;
};
```

### Property Ranges

A single particle is boring. Variation makes effects look natural. Instead of setting `lifetime = 0.8f` for every particle, we define `lifetimeMin = 0.5f` and `lifetimeMax = 1.2f` and randomise within that range for each spawn.

```cpp
struct FloatRange {
    float min = 0.0f;
    float max = 0.0f;

    float sample() const {
        // Assumes a randomFloat(min, max) utility exists from earlier chapters
        return randomFloat(min, max);
    }
};
```

### Colour and Size Curves

In Chapter 20, we interpolated between `colorStart` and `colorEnd`. That gives a straight line from one colour to another. For fire, we need orange at birth, transitioning to red, then to dark grey, then fading to transparent black. That requires a gradient with multiple stops.

```
COLOUR GRADIENT FOR FIRE

  Time:    0.0        0.25        0.5         0.75        1.0
           |           |           |            |           |
  Colour:  bright     orange      deep         dark        transparent
           yellow                  red          grey        black
           ████████████████████████████████████████████████████
           ██ FFCC00 ██ FF6600 ██ CC2200 ██ 444444 ██ 000000 ██
           ████████████████████████████████████████████████████
           alpha=1.0              alpha=0.9                alpha=0.0
```

A colour curve is an array of keys sorted by time. Each key has a normalised time (0.0 to 1.0 over the particle's lifetime) and an RGBA colour. To evaluate the curve at any point, find the two surrounding keys and lerp between them.

```cpp
struct ColourKey {
    float     time  = 0.0f;          // normalised [0, 1]
    glm::vec4 color = {1, 1, 1, 1};  // RGBA
};

struct SizeKey {
    float time  = 0.0f;
    float value = 1.0f;
};

glm::vec4 evaluateColourCurve(const std::vector<ColourKey>& keys, float t)
{
    if (keys.empty()) return {1, 1, 1, 1};
    if (keys.size() == 1 || t <= keys.front().time) return keys.front().color;
    if (t >= keys.back().time) return keys.back().color;

    // Find the two keys surrounding t
    for (size_t i = 0; i < keys.size() - 1; ++i) {
        if (t >= keys[i].time && t <= keys[i + 1].time) {
            float segmentLength = keys[i + 1].time - keys[i].time;
            float localT = (segmentLength > 0.0001f)
                ? (t - keys[i].time) / segmentLength
                : 0.0f;
            return glm::mix(keys[i].color, keys[i + 1].color, localT);
        }
    }

    return keys.back().color;
}

float evaluateSizeCurve(const std::vector<SizeKey>& keys, float t)
{
    if (keys.empty()) return 1.0f;
    if (keys.size() == 1 || t <= keys.front().time) return keys.front().value;
    if (t >= keys.back().time) return keys.back().value;

    for (size_t i = 0; i < keys.size() - 1; ++i) {
        if (t >= keys[i].time && t <= keys[i + 1].time) {
            float segmentLength = keys[i + 1].time - keys[i].time;
            float localT = (segmentLength > 0.0001f)
                ? (t - keys[i].time) / segmentLength
                : 0.0f;
            return glm::mix(keys[i].value, keys[i + 1].value, localT);
        }
    }

    return keys.back().value;
}
```

The curve evaluation functions have the same shape: clamp to first/last key at the extremes, search for the enclosing pair in the middle, and lerp. The search is linear because particle curves typically have 2-6 keys — not worth a binary search.

With curves, the old `colorStart`/`colorEnd` fields on the Particle struct become redundant for data-driven effects. The system evaluates the curve each frame using `age / lifetime` as the normalised time `t`, and writes the result directly to the particle's current colour. We keep the old fields for backward compatibility with any remaining hardcoded effects.

### Sub-Emitter Definitions

A spark bounces off a wall and puffs out a tiny cloud of dust. A fireball rises and leaves a trail of smoke. A blood droplet hits the floor and spawns a small splatter. These are sub-emitters: child particle effects triggered by events in a parent particle's life.

```
SUB-EMITTER CHAIN

  Parent: Firework Rocket (trail of sparks)
    │
    ├── OnBirth → Spark Trail (each spark gets a small trail)
    │
    └── OnDeath → Firework Burst (explosion of coloured stars)
                    │
                    └── OnDeath → Sparkle Fade (tiny twinkles)

  Max recursion depth: 3 (rocket → burst → sparkle → STOP)
```

```cpp
enum class SubEmitterTrigger {
    OnBirth,       // when the parent particle is first spawned
    OnDeath,       // when the parent particle's age exceeds its lifetime
    OnCollision    // when the parent particle hits world geometry
};

struct SubEmitterDef {
    SubEmitterTrigger trigger        = SubEmitterTrigger::OnDeath;
    std::string       childEffect;   // name of the child ParticleEffectDef
    bool              inheritVelocity = false;
    float             velocityScale   = 1.0f;
};
```

We need a recursion limit. Without one, an effect whose sub-emitter references itself (or creates a cycle) would spawn particles forever and crash the engine. A maximum depth of 2-3 is sensible. The spawn function receives the current depth and refuses to spawn sub-emitters beyond the limit.

### Collision Response

Chapter 45 gave particles the ability to bounce off geometry. The effect definition exposes those settings so each effect can configure its own collision behaviour:

```cpp
struct CollisionDef {
    bool  enabled       = false;
    float restitution   = 0.5f;   // bounciness
    float friction      = 0.3f;   // tangential damping on bounce
    bool  killOnHit     = false;  // particle dies on first collision
    bool  spawnDecalOnHit = false; // spawn a decal where the particle hits
    std::string decalTexture;      // path to decal texture (Ch 31 integration)
};
```

### The Complete Definition

Putting it all together:

```cpp
enum class BlendMode {
    Alpha,
    Additive
};

enum class EmissionMode {
    Continuous,   // spawn at a steady rate (particles per second)
    Burst         // spawn a fixed count all at once
};

struct ParticleEffectDef {
    std::string name;

    // --- Emission ---
    EmissionMode emissionMode  = EmissionMode::Burst;
    float emissionRate         = 0.0f;   // particles per second (continuous mode)
    int   burstCount           = 10;     // particles per burst (burst mode)
    float duration             = 0.0f;   // seconds (0 = one-shot burst or infinite continuous)
    bool  looping              = false;  // restart after duration expires?

    // --- Emitter Shape ---
    EmitterShapeDef shape;

    // --- Per-Particle Properties (randomised between min and max) ---
    FloatRange lifetime     = {0.5f, 1.5f};
    FloatRange speed        = {1.0f, 5.0f};
    FloatRange sizeStart    = {0.1f, 0.3f};
    FloatRange rotationSpeed = {0.0f, 0.0f};  // radians/sec

    // --- Curves ---
    std::vector<ColourKey> colourOverLifetime = {
        {0.0f, {1, 1, 1, 1}},
        {1.0f, {1, 1, 1, 0}}
    };
    std::vector<SizeKey> sizeOverLifetime = {
        {0.0f, 1.0f},
        {1.0f, 1.0f}
    };
    // sizeOverLifetime is a multiplier on the particle's sizeStart.
    // At t=0 the particle is sizeStart * sizeOverLifetime[0].
    // At t=1 it is sizeStart * sizeOverLifetime[last].

    // --- Physics ---
    float gravityMultiplier = 1.0f;  // 0 = no gravity, 1 = full gravity, -1 = rises
    float drag              = 0.0f;
    glm::vec3 localWind     = {0, 0, 0};
    float turbulenceStrength = 0.0f;
    CollisionDef collision;

    // --- Rendering ---
    BlendMode blendMode        = BlendMode::Alpha;
    std::string texturePath;       // path to particle texture or atlas
    int flipbookColumns            = 1;
    int flipbookRows               = 1;
    bool trailEnabled              = false;
    float trailWidth               = 0.1f;
    int   trailHistoryLength       = 16;

    // --- Sub-Emitters ---
    std::vector<SubEmitterDef> subEmitters;
    int maxSubEmitterDepth         = 2;
};
```

This single struct captures everything. An artist needs to understand the JSON fields, not C++ internals. The programmer writes the system once; the artist creates hundreds of effects.

---

## The JSON Format

Every `ParticleEffectDef` maps directly to a JSON file. We use nlohmann/json (set up in Chapter 23) for parsing.

### Schema Overview

```json
{
    "name": "effect_name",

    "emission": {
        "mode": "burst",
        "burstCount": 20,
        "rate": 0,
        "duration": 0,
        "looping": false
    },

    "shape": {
        "type": "point",
        "sphereRadius": 1.0,
        "coneAngle": 30.0,
        "coneRadius": 0.0,
        "boxHalfExtents": [0.5, 0.5, 0.5],
        "ringRadius": 1.0
    },

    "properties": {
        "lifetime": { "min": 0.5, "max": 1.5 },
        "speed": { "min": 1.0, "max": 5.0 },
        "sizeStart": { "min": 0.1, "max": 0.3 },
        "rotationSpeed": { "min": 0.0, "max": 0.0 }
    },

    "colourOverLifetime": [
        { "time": 0.0, "color": [1.0, 1.0, 1.0, 1.0] },
        { "time": 1.0, "color": [1.0, 1.0, 1.0, 0.0] }
    ],

    "sizeOverLifetime": [
        { "time": 0.0, "value": 1.0 },
        { "time": 1.0, "value": 1.0 }
    ],

    "physics": {
        "gravityMultiplier": 1.0,
        "drag": 0.0,
        "localWind": [0.0, 0.0, 0.0],
        "turbulenceStrength": 0.0
    },

    "collision": {
        "enabled": false,
        "restitution": 0.5,
        "friction": 0.3,
        "killOnHit": false,
        "spawnDecalOnHit": false,
        "decalTexture": ""
    },

    "rendering": {
        "blendMode": "alpha",
        "texture": "textures/particles/default.png",
        "flipbookColumns": 1,
        "flipbookRows": 1,
        "trailEnabled": false,
        "trailWidth": 0.1,
        "trailHistoryLength": 16
    },

    "subEmitters": []
}
```

### Example: Blood Splatter

Dark red droplets burst outward, fall under gravity, die on impact with surfaces, and leave a decal where they land.

```json
{
    "name": "blood_splatter",

    "emission": {
        "mode": "burst",
        "burstCount": 20,
        "duration": 0,
        "looping": false
    },

    "shape": {
        "type": "sphere",
        "sphereRadius": 0.1
    },

    "properties": {
        "lifetime": { "min": 0.6, "max": 1.4 },
        "speed": { "min": 2.0, "max": 6.0 },
        "sizeStart": { "min": 0.02, "max": 0.06 },
        "rotationSpeed": { "min": -3.0, "max": 3.0 }
    },

    "colourOverLifetime": [
        { "time": 0.0, "color": [0.6, 0.02, 0.02, 1.0] },
        { "time": 0.7, "color": [0.4, 0.01, 0.01, 0.9] },
        { "time": 1.0, "color": [0.25, 0.0, 0.0, 0.0] }
    ],

    "sizeOverLifetime": [
        { "time": 0.0, "value": 1.0 },
        { "time": 1.0, "value": 0.5 }
    ],

    "physics": {
        "gravityMultiplier": 1.2,
        "drag": 0.5
    },

    "collision": {
        "enabled": true,
        "restitution": 0.1,
        "friction": 0.8,
        "killOnHit": true,
        "spawnDecalOnHit": true,
        "decalTexture": "textures/decals/blood_splat_01.png"
    },

    "rendering": {
        "blendMode": "alpha",
        "texture": "textures/particles/blood_drop.png"
    },

    "subEmitters": []
}
```

### Example: Fire

A looping emitter that shoots particles upward in a cone. Colour fades through a yellow-orange-red-grey-black gradient. Uses a flipbook fire texture. Additive blending for a bright, glowing look. Spawns smoke sub-emitter when particles die at the top.

```json
{
    "name": "fire_torch",

    "emission": {
        "mode": "continuous",
        "rate": 40,
        "duration": 0,
        "looping": true
    },

    "shape": {
        "type": "cone",
        "coneAngle": 12.0,
        "coneRadius": 0.05
    },

    "properties": {
        "lifetime": { "min": 0.3, "max": 0.7 },
        "speed": { "min": 1.5, "max": 3.0 },
        "sizeStart": { "min": 0.15, "max": 0.3 },
        "rotationSpeed": { "min": -1.0, "max": 1.0 }
    },

    "colourOverLifetime": [
        { "time": 0.0, "color": [1.0, 0.9, 0.3, 1.0] },
        { "time": 0.2, "color": [1.0, 0.6, 0.1, 1.0] },
        { "time": 0.5, "color": [0.9, 0.2, 0.05, 0.8] },
        { "time": 0.8, "color": [0.3, 0.1, 0.05, 0.4] },
        { "time": 1.0, "color": [0.1, 0.1, 0.1, 0.0] }
    ],

    "sizeOverLifetime": [
        { "time": 0.0, "value": 0.6 },
        { "time": 0.3, "value": 1.0 },
        { "time": 1.0, "value": 1.8 }
    ],

    "physics": {
        "gravityMultiplier": -0.3,
        "drag": 2.0,
        "turbulenceStrength": 1.5
    },

    "collision": { "enabled": false },

    "rendering": {
        "blendMode": "additive",
        "texture": "textures/particles/fire_flipbook.png",
        "flipbookColumns": 4,
        "flipbookRows": 4
    },

    "subEmitters": [
        {
            "trigger": "onDeath",
            "childEffect": "smoke_wisp",
            "inheritVelocity": true,
            "velocityScale": 0.3
        }
    ]
}
```

### Example: Smoke

Slow, expanding, turbulence-driven grey particles. Heavy drag makes them decelerate quickly and drift.

```json
{
    "name": "smoke_wisp",

    "emission": {
        "mode": "burst",
        "burstCount": 3,
        "duration": 0,
        "looping": false
    },

    "shape": {
        "type": "sphere",
        "sphereRadius": 0.1
    },

    "properties": {
        "lifetime": { "min": 1.0, "max": 2.5 },
        "speed": { "min": 0.2, "max": 0.8 },
        "sizeStart": { "min": 0.1, "max": 0.2 },
        "rotationSpeed": { "min": -0.5, "max": 0.5 }
    },

    "colourOverLifetime": [
        { "time": 0.0, "color": [0.5, 0.5, 0.5, 0.3] },
        { "time": 0.5, "color": [0.4, 0.4, 0.4, 0.2] },
        { "time": 1.0, "color": [0.3, 0.3, 0.3, 0.0] }
    ],

    "sizeOverLifetime": [
        { "time": 0.0, "value": 1.0 },
        { "time": 1.0, "value": 4.0 }
    ],

    "physics": {
        "gravityMultiplier": -0.1,
        "drag": 3.0,
        "turbulenceStrength": 0.8
    },

    "collision": { "enabled": false },

    "rendering": {
        "blendMode": "alpha",
        "texture": "textures/particles/smoke_soft.png"
    },

    "subEmitters": []
}
```

### Example: Bullet Impact (Metal)

Bright yellow-white sparks burst outward at high speed, bounce off surfaces with high restitution, and die quickly. A small dust puff sub-emitter fires at the point of impact.

```json
{
    "name": "impact_metal",

    "emission": {
        "mode": "burst",
        "burstCount": 12,
        "duration": 0,
        "looping": false
    },

    "shape": {
        "type": "cone",
        "coneAngle": 45.0,
        "coneRadius": 0.0
    },

    "properties": {
        "lifetime": { "min": 0.2, "max": 0.6 },
        "speed": { "min": 5.0, "max": 12.0 },
        "sizeStart": { "min": 0.01, "max": 0.03 },
        "rotationSpeed": { "min": 0.0, "max": 0.0 }
    },

    "colourOverLifetime": [
        { "time": 0.0, "color": [1.0, 1.0, 0.8, 1.0] },
        { "time": 0.3, "color": [1.0, 0.8, 0.2, 1.0] },
        { "time": 1.0, "color": [1.0, 0.4, 0.05, 0.0] }
    ],

    "sizeOverLifetime": [
        { "time": 0.0, "value": 1.0 },
        { "time": 1.0, "value": 0.3 }
    ],

    "physics": {
        "gravityMultiplier": 1.0,
        "drag": 0.5
    },

    "collision": {
        "enabled": true,
        "restitution": 0.7,
        "friction": 0.2,
        "killOnHit": false,
        "spawnDecalOnHit": false
    },

    "rendering": {
        "blendMode": "additive",
        "texture": "textures/particles/spark.png"
    },

    "subEmitters": [
        {
            "trigger": "onBirth",
            "childEffect": "impact_dust_small",
            "inheritVelocity": false,
            "velocityScale": 0.0
        }
    ]
}
```

### Example: Bullet Impact (Stone)

Grey-brown dust instead of sparks. Lower speed, larger particles, no bounce — they die on hit.

```json
{
    "name": "impact_stone",

    "emission": {
        "mode": "burst",
        "burstCount": 15,
        "duration": 0,
        "looping": false
    },

    "shape": {
        "type": "cone",
        "coneAngle": 50.0,
        "coneRadius": 0.0
    },

    "properties": {
        "lifetime": { "min": 0.3, "max": 0.9 },
        "speed": { "min": 1.0, "max": 4.0 },
        "sizeStart": { "min": 0.03, "max": 0.08 },
        "rotationSpeed": { "min": -2.0, "max": 2.0 }
    },

    "colourOverLifetime": [
        { "time": 0.0, "color": [0.65, 0.55, 0.4, 0.8] },
        { "time": 0.4, "color": [0.5, 0.45, 0.35, 0.5] },
        { "time": 1.0, "color": [0.4, 0.35, 0.3, 0.0] }
    ],

    "sizeOverLifetime": [
        { "time": 0.0, "value": 1.0 },
        { "time": 1.0, "value": 2.5 }
    ],

    "physics": {
        "gravityMultiplier": 0.8,
        "drag": 3.0,
        "turbulenceStrength": 0.5
    },

    "collision": {
        "enabled": true,
        "restitution": 0.0,
        "friction": 1.0,
        "killOnHit": true,
        "spawnDecalOnHit": true,
        "decalTexture": "textures/decals/bullet_hole_stone.png"
    },

    "rendering": {
        "blendMode": "alpha",
        "texture": "textures/particles/dust_puff.png"
    },

    "subEmitters": []
}
```

### Example: Rocket Trail

A continuous emitter that stays attached to its parent entity (the rocket projectile). Trail-enabled particles create streaks behind the rocket. Smoke sub-emitter fires on each particle's death to leave lingering clouds.

```json
{
    "name": "rocket_trail",

    "emission": {
        "mode": "continuous",
        "rate": 60,
        "duration": 0,
        "looping": true
    },

    "shape": {
        "type": "point"
    },

    "properties": {
        "lifetime": { "min": 0.3, "max": 0.6 },
        "speed": { "min": 0.1, "max": 0.5 },
        "sizeStart": { "min": 0.05, "max": 0.1 },
        "rotationSpeed": { "min": 0.0, "max": 0.0 }
    },

    "colourOverLifetime": [
        { "time": 0.0, "color": [1.0, 0.9, 0.6, 0.9] },
        { "time": 0.4, "color": [1.0, 0.5, 0.1, 0.6] },
        { "time": 1.0, "color": [0.3, 0.3, 0.3, 0.0] }
    ],

    "sizeOverLifetime": [
        { "time": 0.0, "value": 1.0 },
        { "time": 1.0, "value": 3.0 }
    ],

    "physics": {
        "gravityMultiplier": 0.0,
        "drag": 1.0
    },

    "collision": { "enabled": false },

    "rendering": {
        "blendMode": "additive",
        "texture": "textures/particles/smoke_trail.png",
        "trailEnabled": true,
        "trailWidth": 0.08,
        "trailHistoryLength": 12
    },

    "subEmitters": [
        {
            "trigger": "onDeath",
            "childEffect": "smoke_wisp",
            "inheritVelocity": true,
            "velocityScale": 0.2
        }
    ]
}
```

### Example: Explosion

A multi-layer effect. In practice this is implemented as a single JSON file that uses a large burst count for the core, with sub-emitters for the secondary layers. Alternatively, the gameplay code spawns multiple named effects at the same position. We show the multi-spawn approach because it gives the most control.

```json
{
    "name": "explosion_large",

    "emission": {
        "mode": "burst",
        "burstCount": 30,
        "duration": 0,
        "looping": false
    },

    "shape": {
        "type": "sphere",
        "sphereRadius": 0.3
    },

    "properties": {
        "lifetime": { "min": 0.5, "max": 1.2 },
        "speed": { "min": 3.0, "max": 10.0 },
        "sizeStart": { "min": 0.2, "max": 0.5 },
        "rotationSpeed": { "min": -4.0, "max": 4.0 }
    },

    "colourOverLifetime": [
        { "time": 0.0, "color": [1.0, 1.0, 0.9, 1.0] },
        { "time": 0.1, "color": [1.0, 0.8, 0.2, 1.0] },
        { "time": 0.4, "color": [1.0, 0.3, 0.05, 0.9] },
        { "time": 0.7, "color": [0.3, 0.15, 0.05, 0.5] },
        { "time": 1.0, "color": [0.1, 0.1, 0.1, 0.0] }
    ],

    "sizeOverLifetime": [
        { "time": 0.0, "value": 0.5 },
        { "time": 0.15, "value": 1.0 },
        { "time": 1.0, "value": 2.0 }
    ],

    "physics": {
        "gravityMultiplier": 0.3,
        "drag": 2.0,
        "turbulenceStrength": 2.0
    },

    "collision": {
        "enabled": true,
        "restitution": 0.3,
        "friction": 0.5,
        "killOnHit": false
    },

    "rendering": {
        "blendMode": "additive",
        "texture": "textures/particles/fire_flipbook.png",
        "flipbookColumns": 4,
        "flipbookRows": 4
    },

    "subEmitters": [
        {
            "trigger": "onDeath",
            "childEffect": "smoke_wisp",
            "inheritVelocity": true,
            "velocityScale": 0.15
        },
        {
            "trigger": "onBirth",
            "childEffect": "explosion_debris",
            "inheritVelocity": true,
            "velocityScale": 0.5
        }
    ]
}
```

The gameplay code that triggers an explosion spawns three effects at the same position for the full layered look:

```cpp
// In the explosion handler
effectManager.spawnEffect("explosion_large", hitPos, glm::vec3(0, 1, 0));
effectManager.spawnEffect("explosion_flash", hitPos, glm::vec3(0, 1, 0));
effectManager.spawnEffect("explosion_shockwave", hitPos, glm::vec3(0, 1, 0));
```

The flash is a single large billboard that appears for 2 frames and vanishes. The shockwave is an expanding ring. Combining multiple named effects gives designers full control over each layer independently.

---

## Loading Effects from JSON

The loader converts a JSON file into a `ParticleEffectDef`. Helper functions handle enums and nested objects.

```cpp
// In src/engine/particles/particle_effect_loader.h

#pragma once

#include "particle_effect_def.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>

using json = nlohmann::json;

namespace ParticleEffectLoader {

// --- Enum Helpers ---

inline EmitterShape parseShape(const std::string& s) {
    if (s == "point")  return EmitterShape::Point;
    if (s == "sphere") return EmitterShape::Sphere;
    if (s == "cone")   return EmitterShape::Cone;
    if (s == "box")    return EmitterShape::Box;
    if (s == "ring")   return EmitterShape::Ring;
    throw std::runtime_error("Unknown emitter shape: " + s);
}

inline EmissionMode parseEmissionMode(const std::string& s) {
    if (s == "burst")      return EmissionMode::Burst;
    if (s == "continuous") return EmissionMode::Continuous;
    throw std::runtime_error("Unknown emission mode: " + s);
}

inline BlendMode parseBlendMode(const std::string& s) {
    if (s == "alpha")    return BlendMode::Alpha;
    if (s == "additive") return BlendMode::Additive;
    throw std::runtime_error("Unknown blend mode: " + s);
}

inline SubEmitterTrigger parseTrigger(const std::string& s) {
    if (s == "onBirth")     return SubEmitterTrigger::OnBirth;
    if (s == "onDeath")     return SubEmitterTrigger::OnDeath;
    if (s == "onCollision") return SubEmitterTrigger::OnCollision;
    throw std::runtime_error("Unknown sub-emitter trigger: " + s);
}

// --- Range Helper ---

inline FloatRange parseRange(const json& j) {
    FloatRange r;
    r.min = j.value("min", 0.0f);
    r.max = j.value("max", r.min);
    return r;
}

// --- Main Loader ---

inline ParticleEffectDef loadFromFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open particle effect: " + filepath);
    }

    json j = json::parse(file);
    ParticleEffectDef def;

    def.name = j.value("name", "unnamed");

    // Emission
    if (j.contains("emission")) {
        auto& em = j["emission"];
        def.emissionMode = parseEmissionMode(em.value("mode", "burst"));
        def.emissionRate = em.value("rate", 0.0f);
        def.burstCount   = em.value("burstCount", 10);
        def.duration     = em.value("duration", 0.0f);
        def.looping      = em.value("looping", false);
    }

    // Shape
    if (j.contains("shape")) {
        auto& sh = j["shape"];
        def.shape.shape = parseShape(sh.value("type", "point"));
        def.shape.sphereRadius    = sh.value("sphereRadius", 1.0f);
        def.shape.coneAngle       = sh.value("coneAngle", 30.0f);
        def.shape.coneRadius      = sh.value("coneRadius", 0.0f);
        def.shape.ringRadius      = sh.value("ringRadius", 1.0f);

        if (sh.contains("boxHalfExtents")) {
            auto& b = sh["boxHalfExtents"];
            def.shape.boxHalfExtents = {b[0], b[1], b[2]};
        }
    }

    // Properties
    if (j.contains("properties")) {
        auto& pr = j["properties"];
        if (pr.contains("lifetime"))      def.lifetime      = parseRange(pr["lifetime"]);
        if (pr.contains("speed"))         def.speed         = parseRange(pr["speed"]);
        if (pr.contains("sizeStart"))     def.sizeStart     = parseRange(pr["sizeStart"]);
        if (pr.contains("rotationSpeed")) def.rotationSpeed = parseRange(pr["rotationSpeed"]);
    }

    // Colour curve
    if (j.contains("colourOverLifetime")) {
        def.colourOverLifetime.clear();
        for (auto& key : j["colourOverLifetime"]) {
            ColourKey ck;
            ck.time = key.value("time", 0.0f);
            auto& c = key["color"];
            ck.color = {c[0], c[1], c[2], c[3]};
            def.colourOverLifetime.push_back(ck);
        }
    }

    // Size curve
    if (j.contains("sizeOverLifetime")) {
        def.sizeOverLifetime.clear();
        for (auto& key : j["sizeOverLifetime"]) {
            SizeKey sk;
            sk.time  = key.value("time", 0.0f);
            sk.value = key.value("value", 1.0f);
            def.sizeOverLifetime.push_back(sk);
        }
    }

    // Physics
    if (j.contains("physics")) {
        auto& ph = j["physics"];
        def.gravityMultiplier  = ph.value("gravityMultiplier", 1.0f);
        def.drag               = ph.value("drag", 0.0f);
        def.turbulenceStrength = ph.value("turbulenceStrength", 0.0f);

        if (ph.contains("localWind")) {
            auto& w = ph["localWind"];
            def.localWind = {w[0], w[1], w[2]};
        }
    }

    // Collision
    if (j.contains("collision")) {
        auto& co = j["collision"];
        def.collision.enabled         = co.value("enabled", false);
        def.collision.restitution     = co.value("restitution", 0.5f);
        def.collision.friction        = co.value("friction", 0.3f);
        def.collision.killOnHit       = co.value("killOnHit", false);
        def.collision.spawnDecalOnHit = co.value("spawnDecalOnHit", false);
        def.collision.decalTexture    = co.value("decalTexture", std::string(""));
    }

    // Rendering
    if (j.contains("rendering")) {
        auto& rn = j["rendering"];
        def.blendMode         = parseBlendMode(rn.value("blendMode", "alpha"));
        def.texturePath       = rn.value("texture", std::string(""));
        def.flipbookColumns   = rn.value("flipbookColumns", 1);
        def.flipbookRows      = rn.value("flipbookRows", 1);
        def.trailEnabled      = rn.value("trailEnabled", false);
        def.trailWidth        = rn.value("trailWidth", 0.1f);
        def.trailHistoryLength = rn.value("trailHistoryLength", 16);
    }

    // Sub-emitters
    if (j.contains("subEmitters")) {
        for (auto& sub : j["subEmitters"]) {
            SubEmitterDef sd;
            sd.trigger         = parseTrigger(sub.value("trigger", "onDeath"));
            sd.childEffect     = sub.value("childEffect", std::string(""));
            sd.inheritVelocity = sub.value("inheritVelocity", false);
            sd.velocityScale   = sub.value("velocityScale", 1.0f);
            def.subEmitters.push_back(sd);
        }
    }

    def.maxSubEmitterDepth = j.value("maxSubEmitterDepth", 2);

    return def;
}

} // namespace ParticleEffectLoader
```

Every field uses `j.value("key", default)` so that missing fields gracefully fall back to defaults. An artist can start with a minimal JSON file containing just a name and emission settings, and progressively add sections as they refine the effect.

---

## ParticleEffectManager

The manager loads all effect definitions from a directory, caches them by name, and provides the spawn interface that gameplay systems call.

```cpp
// In src/engine/particles/particle_effect_manager.h

#pragma once

#include "particle_effect_def.h"
#include "particle_effect_loader.h"
#include <unordered_map>
#include <filesystem>
#include <string>
#include <entt/entt.hpp>
#include <glm/glm.hpp>

class ParticleEffectManager {
public:
    // Load all .json files from the given directory
    void loadDirectory(const std::string& dirPath)
    {
        namespace fs = std::filesystem;

        for (auto& entry : fs::directory_iterator(dirPath)) {
            if (entry.path().extension() == ".json") {
                try {
                    ParticleEffectDef def =
                        ParticleEffectLoader::loadFromFile(entry.path().string());

                    std::string name = def.name;
                    m_effects[name] = std::move(def);

                    // Record modification time for hot-reload
                    m_filePaths[name] = entry.path().string();
                    m_modTimes[name]  = fs::last_write_time(entry.path());

                } catch (const std::exception& e) {
                    // Log the error but continue loading other files
                    // logError("Failed to load particle effect: %s — %s",
                    //          entry.path().c_str(), e.what());
                }
            }
        }
    }

    // Look up an effect definition by name
    const ParticleEffectDef* getEffect(const std::string& name) const
    {
        auto it = m_effects.find(name);
        if (it != m_effects.end()) {
            return &it->second;
        }
        return nullptr;
    }

    // Spawn an emitter entity that will produce particles according to the named effect
    entt::entity spawnEffect(entt::registry& registry,
                             const std::string& effectName,
                             const glm::vec3& position,
                             const glm::vec3& direction,
                             entt::entity parent = entt::null)
    {
        const ParticleEffectDef* def = getEffect(effectName);
        if (!def) return entt::null;

        return createEmitterEntity(registry, *def, position, direction, parent, 0);
    }

    // Hot-reload: call once per frame (or every few seconds)
    void checkForChanges()
    {
        namespace fs = std::filesystem;

        for (auto& [name, path] : m_filePaths) {
            auto currentTime = fs::last_write_time(path);
            if (currentTime != m_modTimes[name]) {
                m_modTimes[name] = currentTime;

                try {
                    ParticleEffectDef def =
                        ParticleEffectLoader::loadFromFile(path);
                    m_effects[name] = std::move(def);
                    // logInfo("Hot-reloaded particle effect: %s", name.c_str());
                } catch (const std::exception& e) {
                    // logError("Hot-reload failed for %s: %s",
                    //          name.c_str(), e.what());
                }
            }
        }
    }

private:
    entt::entity createEmitterEntity(entt::registry& registry,
                                     const ParticleEffectDef& def,
                                     const glm::vec3& position,
                                     const glm::vec3& direction,
                                     entt::entity parent,
                                     int subEmitterDepth);
    // (defined below, after the ParticleEmitter component)

    std::unordered_map<std::string, ParticleEffectDef> m_effects;
    std::unordered_map<std::string, std::string>       m_filePaths;
    std::unordered_map<std::string, std::filesystem::file_time_type> m_modTimes;
};
```

The manager is the only place in the codebase that knows about effect files. Everything else works with effect names.

---

## ParticleEmitter — The ECS Component

Following QEngine's rule — components have no behaviour, systems have no state — the emitter component is pure data. It tracks what effect this emitter is producing, where it is spawning, and how much time has accumulated for the next emission.

```cpp
// In src/engine/particles/particle_emitter.h

#pragma once

#include "particle_effect_def.h"
#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include <string>

struct ParticleEmitter {
    std::string effectName;                  // lookup key into the manager
    const ParticleEffectDef* effectDef = nullptr;  // cached pointer (refreshed on hot-reload)

    glm::vec3 position  = {0, 0, 0};
    glm::vec3 direction = {0, 1, 0};        // used for cone/hemisphere orientation

    float emissionAccumulator = 0.0f;        // accumulated time for continuous emission
    float age                 = 0.0f;        // how long this emitter has been active
    bool  active              = true;
    bool  hasBurst            = false;        // for one-shot bursts: has the burst fired?

    entt::entity parentEntity = entt::null;  // if set, emitter follows this entity's position
    int subEmitterDepth       = 0;           // recursion depth for sub-emitter chains
};
```

Now the manager's `createEmitterEntity` can be implemented:

```cpp
entt::entity ParticleEffectManager::createEmitterEntity(
    entt::registry& registry,
    const ParticleEffectDef& def,
    const glm::vec3& position,
    const glm::vec3& direction,
    entt::entity parent,
    int subEmitterDepth)
{
    entt::entity e = registry.create();

    ParticleEmitter& emitter = registry.emplace<ParticleEmitter>(e);
    emitter.effectName       = def.name;
    emitter.effectDef        = &def;
    emitter.position         = position;
    emitter.direction         = direction;
    emitter.parentEntity     = parent;
    emitter.subEmitterDepth  = subEmitterDepth;
    emitter.active           = true;
    emitter.hasBurst         = false;
    emitter.age              = 0.0f;
    emitter.emissionAccumulator = 0.0f;

    return e;
}
```

---

## The Particle Emitter System

The system iterates over all active emitter entities, advances their timers, and spawns particles into the pool according to the effect definition.

### Spawning a Single Particle from a Definition

First, a helper that takes a `ParticleEffectDef` and produces one configured particle:

```cpp
// In src/engine/particles/particle_emitter_system.cpp

#include "particle_emitter.h"
#include "particle_effect_def.h"
#include "particle_pool.h" // from Ch 20
#include <glm/gtc/constants.hpp>

// Generate a random direction within the emitter shape, returning
// both spawn position offset and initial velocity direction.
void sampleEmitterShape(const EmitterShapeDef& shape,
                        const glm::vec3& emitterDir,
                        glm::vec3& outOffset,
                        glm::vec3& outDirection)
{
    switch (shape.shape) {
    case EmitterShape::Point:
        outOffset    = {0, 0, 0};
        outDirection = randomOnUnitSphere();
        break;

    case EmitterShape::Sphere:
        outOffset    = randomOnUnitSphere() * randomFloat(0.0f, shape.sphereRadius);
        outDirection = glm::normalize(outOffset);
        break;

    case EmitterShape::Cone: {
        // Random direction within a cone around emitterDir
        float angle = randomFloat(0.0f, glm::radians(shape.coneAngle));
        float phi   = randomFloat(0.0f, glm::two_pi<float>());

        // Build a direction in cone-local space (cone axis = +Y)
        glm::vec3 localDir = {
            std::sin(angle) * std::cos(phi),
            std::cos(angle),
            std::sin(angle) * std::sin(phi)
        };

        // Rotate from +Y to emitterDir
        outDirection = rotateToAxis(localDir, emitterDir);
        outOffset    = outDirection * shape.coneRadius;
        break;
    }

    case EmitterShape::Box:
        outOffset = {
            randomFloat(-shape.boxHalfExtents.x, shape.boxHalfExtents.x),
            randomFloat(-shape.boxHalfExtents.y, shape.boxHalfExtents.y),
            randomFloat(-shape.boxHalfExtents.z, shape.boxHalfExtents.z)
        };
        outDirection = randomOnUnitSphere();
        break;

    case EmitterShape::Ring: {
        float theta = randomFloat(0.0f, glm::two_pi<float>());
        outOffset = {
            std::cos(theta) * shape.ringRadius,
            0.0f,
            std::sin(theta) * shape.ringRadius
        };
        outDirection = glm::normalize(outOffset); // outward from ring center
        break;
    }
    }
}

void spawnParticleFromDef(ParticlePool& pool,
                          const ParticleEffectDef& def,
                          const glm::vec3& emitterPos,
                          const glm::vec3& emitterDir)
{
    Particle& p = pool.acquire();
    if (!p.active) return; // pool full

    glm::vec3 offset, dir;
    sampleEmitterShape(def.shape, emitterDir, offset, dir);

    p.position = emitterPos + offset;
    p.velocity = dir * def.speed.sample();
    p.lifetime = def.lifetime.sample();
    p.age      = 0.0f;
    p.active   = true;

    // Size: the sizeStart range sets the base, the curve multiplies it over time
    p.sizeStart = def.sizeStart.sample();
    p.sizeEnd   = p.sizeStart; // we use the curve now, not start/end interpolation

    // Colour: initial value from curve at t=0 (the system updates it each frame)
    p.colorStart = evaluateColourCurve(def.colourOverLifetime, 0.0f);
    p.colorEnd   = evaluateColourCurve(def.colourOverLifetime, 1.0f);

    // Rotation
    p.rotation        = randomFloat(0.0f, glm::two_pi<float>());
    p.angularVelocity = def.rotationSpeed.sample();

    // Physics
    p.drag             = def.drag;
    p.collisionEnabled = def.collision.enabled;
    p.restitution      = def.collision.restitution;
    p.friction         = def.collision.friction;
    p.killOnCollision  = def.collision.killOnHit;

    // Flipbook
    p.flipbookColumns = def.flipbookColumns;
    p.flipbookRows    = def.flipbookRows;

    // Trails
    p.trailEnabled = def.trailEnabled;
}
```

### The System Loop

```cpp
void particleEmitterSystem(entt::registry& registry,
                           ParticlePool& pool,
                           ParticleEffectManager& effectManager,
                           float dt)
{
    auto view = registry.view<ParticleEmitter>();

    for (auto entity : view) {
        ParticleEmitter& emitter = view.get<ParticleEmitter>(entity);
        if (!emitter.active) continue;

        const ParticleEffectDef* def = emitter.effectDef;
        if (!def) continue;

        // If attached to a parent, follow its position
        if (emitter.parentEntity != entt::null &&
            registry.valid(emitter.parentEntity))
        {
            // Assumes a Transform component exists on the parent
            if (auto* transform = registry.try_get<Transform>(emitter.parentEntity)) {
                emitter.position = transform->position;
            }
        }

        emitter.age += dt;

        // Check duration: deactivate timed emitters
        if (def->duration > 0.0f && emitter.age >= def->duration) {
            if (def->looping) {
                emitter.age = 0.0f;  // restart
            } else {
                emitter.active = false;
                registry.destroy(entity);
                continue;
            }
        }

        // --- Spawn particles ---

        if (def->emissionMode == EmissionMode::Burst) {
            if (!emitter.hasBurst) {
                emitter.hasBurst = true;
                for (int i = 0; i < def->burstCount; ++i) {
                    spawnParticleFromDef(pool, *def, emitter.position,
                                         emitter.direction);
                }
                // One-shot burst: deactivate immediately
                if (!def->looping) {
                    emitter.active = false;
                    registry.destroy(entity);
                    continue;
                }
            }
        }
        else if (def->emissionMode == EmissionMode::Continuous) {
            emitter.emissionAccumulator += dt * def->emissionRate;
            int toSpawn = static_cast<int>(emitter.emissionAccumulator);
            emitter.emissionAccumulator -= static_cast<float>(toSpawn);

            for (int i = 0; i < toSpawn; ++i) {
                spawnParticleFromDef(pool, *def, emitter.position,
                                     emitter.direction);
            }
        }
    }
}
```

### Updating Particles with Curves

The existing `particleUpdateSystem` from Chapter 20/45 is extended. After advancing age and integrating physics, we evaluate the colour and size curves:

```cpp
void particleUpdateSystem(ParticlePool& pool,
                          const ParticleEffectManager& effectManager,
                          float dt)
{
    for (size_t i = 0; i < pool.size(); ++i) {
        Particle& p = pool[i];
        if (!p.active) continue;

        p.age += dt;
        if (p.age >= p.lifetime) {
            p.active = false;
            // Sub-emitter OnDeath triggers handled below
            continue;
        }

        // Existing physics: gravity, drag, wind, turbulence, collision
        // (unchanged from Ch 45)

        // --- NEW: Curve evaluation ---
        // If this particle belongs to a data-driven effect, evaluate curves.
        // We store a pointer/index to the effect def on the particle, or
        // we store the curve data directly. For simplicity we store an
        // effect name or index:

        float t = p.age / p.lifetime; // normalised age [0, 1]

        // Colour and size curves are evaluated per-frame.
        // The effect def is looked up via the particle's effectIndex.
        if (p.effectIndex >= 0) {
            const ParticleEffectDef* def = effectManager.getEffectByIndex(p.effectIndex);
            if (def) {
                // Colour from curve
                p.currentColor = evaluateColourCurve(def->colourOverLifetime, t);

                // Size from curve (multiplier on base size)
                float sizeMul = evaluateSizeCurve(def->sizeOverLifetime, t);
                p.currentSize = p.sizeStart * sizeMul;
            }
        } else {
            // Legacy path: simple start/end interpolation (Ch 20)
            p.currentColor = glm::mix(p.colorStart, p.colorEnd, t);
            p.currentSize  = glm::mix(p.sizeStart, p.sizeEnd, t);
        }
    }
}
```

This adds two new fields to Particle: `currentColor`, `currentSize`, and `effectIndex`. The rendering system reads `currentColor` and `currentSize` instead of computing them from start/end pairs.

```cpp
// Added to the Particle struct
glm::vec4 currentColor = {1, 1, 1, 1};
float     currentSize  = 0.1f;
int       effectIndex  = -1;  // -1 = legacy (no curve), >= 0 = index into manager
```

---

## Sub-Emitter Execution

Sub-emitters fire when a particle experiences a trigger event. The check happens inside the particle update system at the moment the trigger condition is met.

```cpp
void handleSubEmitters(ParticlePool& pool,
                       ParticleEffectManager& effectManager,
                       entt::registry& registry,
                       const Particle& particle,
                       SubEmitterTrigger trigger,
                       int currentDepth)
{
    if (particle.effectIndex < 0) return;

    const ParticleEffectDef* def = effectManager.getEffectByIndex(particle.effectIndex);
    if (!def) return;

    for (const auto& sub : def->subEmitters) {
        if (sub.trigger != trigger) continue;

        // Enforce recursion limit
        if (currentDepth >= def->maxSubEmitterDepth) continue;

        const ParticleEffectDef* childDef = effectManager.getEffect(sub.childEffect);
        if (!childDef) continue;

        glm::vec3 inheritedVel = sub.inheritVelocity
            ? particle.velocity * sub.velocityScale
            : glm::vec3(0.0f);

        // Spawn child emitter at the particle's position
        entt::entity childEmitter = effectManager.spawnEffect(
            registry,
            sub.childEffect,
            particle.position,
            glm::normalize(particle.velocity + glm::vec3(0.001f)) // avoid zero
        );

        if (childEmitter != entt::null) {
            auto& em = registry.get<ParticleEmitter>(childEmitter);
            em.subEmitterDepth = currentDepth + 1;
        }
    }
}
```

The calls are inserted at the right points in the particle update loop:

```cpp
// On particle death:
if (p.age >= p.lifetime) {
    handleSubEmitters(pool, effectManager, registry, p,
                      SubEmitterTrigger::OnDeath, p.subEmitterDepth);

    // Also handle decal spawning on death (for blood that kills on ground hit)
    if (/* collision killed it */ && def->collision.spawnDecalOnHit) {
        spawnDecal(registry, p.position, hitNormal, def->collision.decalTexture);
    }

    p.active = false;
    continue;
}

// On particle collision (inside the collision response from Ch 45):
if (collisionDetected) {
    handleSubEmitters(pool, effectManager, registry, p,
                      SubEmitterTrigger::OnCollision, p.subEmitterDepth);

    if (def->collision.spawnDecalOnHit) {
        spawnDecal(registry, hitPoint, hitNormal, def->collision.decalTexture);
    }
}
```

The `OnBirth` trigger fires at spawn time, inside `spawnParticleFromDef`:

```cpp
void spawnParticleFromDef(ParticlePool& pool,
                          const ParticleEffectDef& def,
                          const glm::vec3& emitterPos,
                          const glm::vec3& emitterDir,
                          entt::registry& registry,
                          ParticleEffectManager& effectManager,
                          int subEmitterDepth)
{
    Particle& p = pool.acquire();
    // ... (all the setup from before) ...

    p.subEmitterDepth = subEmitterDepth;

    // Fire OnBirth sub-emitters
    handleSubEmitters(pool, effectManager, registry, p,
                      SubEmitterTrigger::OnBirth, subEmitterDepth);
}
```

---

## Integration with Existing Systems

The whole point of data-driven effects is that gameplay code becomes simple. Instead of calling specific functions with hardcoded parameters, everything goes through the effect manager with a string name.

### Animation Events (Ch 40)

In Chapter 40, we defined animation event types. The particle event type previously called a hardcoded function. Now it references an effect name:

```cpp
// Before (Ch 40):
struct ParticleEventData {
    void (*spawnFunc)(ParticlePool&, const glm::vec3&);
    std::string boneName;
};

// After (Ch 46):
struct ParticleEventData {
    std::string effectName;   // e.g., "muzzle_flash"
    std::string boneName;     // e.g., "weapon_barrel"
};
```

The event handler in `eventDispatchSystem` changes from:

```cpp
// Before:
data.spawnFunc(particlePool, boneWorldPos);

// After:
effectManager.spawnEffect(registry, data.effectName, boneWorldPos,
                          boneWorldForward);
```

Animation JSON files reference effect names. An animator can change which effect plays at which frame without touching C++.

### Weapon System

```cpp
// Before (hardcoded):
void onWeaponFire(ParticlePool& pool, const glm::vec3& muzzlePos) {
    spawnMuzzleFlash(pool, muzzlePos);   // 30 lines of hardcoded setup
    spawnShellCasing(pool, muzzlePos);   // another 20 lines
}

// After (data-driven):
void onWeaponFire(entt::registry& registry,
                  ParticleEffectManager& effectManager,
                  const glm::vec3& muzzlePos,
                  const glm::vec3& muzzleDir)
{
    effectManager.spawnEffect(registry, "muzzle_flash", muzzlePos, muzzleDir);
    effectManager.spawnEffect(registry, "shell_casing", muzzlePos, muzzleDir);
}
```

### Damage System

The surface material determines which effect to spawn. This is a simple lookup:

```cpp
void onBulletHit(entt::registry& registry,
                 ParticleEffectManager& effectManager,
                 const glm::vec3& hitPos,
                 const glm::vec3& hitNormal,
                 SurfaceMaterial material)
{
    std::string effectName;

    switch (material) {
    case SurfaceMaterial::Metal: effectName = "impact_metal"; break;
    case SurfaceMaterial::Stone: effectName = "impact_stone"; break;
    case SurfaceMaterial::Wood:  effectName = "impact_wood";  break;
    case SurfaceMaterial::Flesh: effectName = "blood_splatter"; break;
    default:                     effectName = "impact_stone"; break;
    }

    effectManager.spawnEffect(registry, effectName, hitPos, hitNormal);
}
```

### Explosion System

```cpp
void onExplosion(entt::registry& registry,
                 ParticleEffectManager& effectManager,
                 const glm::vec3& detonationPos)
{
    effectManager.spawnEffect(registry, "explosion_large", detonationPos, {0, 1, 0});
    effectManager.spawnEffect(registry, "explosion_flash", detonationPos, {0, 1, 0});
    effectManager.spawnEffect(registry, "explosion_shockwave", detonationPos, {0, 1, 0});
}
```

Three lines replace what used to be 150 lines of hardcoded particle setup across multiple functions. The effects themselves are defined in JSON files that an artist can edit independently.

---

## Hot-Reload

Rapid iteration is the killer feature. An artist opens the game, plays to a spot where the effect is visible, opens the JSON file in a text editor side-by-side, changes a number, saves, and sees the result in-game without restarting.

### Implementation

The `ParticleEffectManager::checkForChanges()` method (shown earlier) compares file modification timestamps against cached values. When a file is newer, it reloads the JSON and replaces the cached definition.

Call it from the main game loop:

```cpp
// In the main update loop
static float reloadTimer = 0.0f;
reloadTimer += dt;

if (reloadTimer >= 2.0f) {   // check every 2 seconds
    reloadTimer = 0.0f;
    effectManager.checkForChanges();
}
```

Checking every 2 seconds is a good balance. The `std::filesystem::last_write_time` call is cheap — it queries file metadata, not file contents. The actual JSON parse only happens when a change is detected.

### What Happens to Active Emitters

When a definition is reloaded, active emitters that reference it by pointer need to update. There are two strategies:

```
STRATEGY 1: POINTER REFRESH (simple)
─────────────────────────────────────────────────
  Emitter stores effectName (string).
  Each frame, the system looks up effectDef = manager.getEffect(effectName).
  After reload, the pointer automatically points to the new definition.
  Newly spawned particles use updated values.
  Already-alive particles keep their old values (they were set at spawn time).

STRATEGY 2: FORCE RESTART (aggressive)
─────────────────────────────────────────────────
  On reload, kill all active particles from that effect.
  Restart all emitters using the reloaded definition.
  Gives an immediate visual reset.
  Useful during active tuning — artist sees the full effect fresh.
```

Strategy 1 is simpler and usually sufficient. New particles appear with the new settings while old particles fade out naturally. Within one particle lifetime (usually under 2 seconds), the entire effect reflects the updated definition.

For Strategy 1, change the emitter system to re-fetch the pointer each frame:

```cpp
// At the top of particleEmitterSystem, refresh pointers
for (auto entity : view) {
    ParticleEmitter& emitter = view.get<ParticleEmitter>(entity);
    emitter.effectDef = effectManager.getEffect(emitter.effectName);
}
```

---

## The Full File Layout

After this chapter, the particle-related source files look like this:

```
src/engine/particles/
    particle.h                  ← Particle struct (Ch 20, extended Ch 45 & 46)
    particle_pool.h             ← Object pool (Ch 20)
    particle_effect_def.h       ← ParticleEffectDef, all sub-structs (Ch 46)
    particle_effect_loader.h    ← JSON → ParticleEffectDef (Ch 46)
    particle_effect_manager.h   ← Load, cache, spawn, hot-reload (Ch 46)
    particle_emitter.h          ← ParticleEmitter ECS component (Ch 46)
    particle_emitter_system.cpp ← Emitter system, spawn logic, sub-emitters (Ch 46)
    particle_update_system.cpp  ← Physics, curves, collision (Ch 20/45/46)
    particle_render_system.cpp  ← Billboard batching, soft particles (Ch 20/45)

assets/effects/
    blood_splatter.json
    fire_torch.json
    smoke_wisp.json
    impact_metal.json
    impact_stone.json
    rocket_trail.json
    explosion_large.json
    explosion_flash.json
    explosion_shockwave.json
    muzzle_flash.json
    shell_casing.json
```

---

## C++ Concepts Introduced

### Data-Driven Design

Data-driven design moves behaviour definitions from code into data files. The code becomes a generic interpreter — "read the definition, do what it says" — while the data files describe specific behaviours.

The benefits are concrete:

1. **No recompilation.** Changing a JSON field and saving the file is instant. A full C++ rebuild on a large project can take minutes.
2. **Separation of roles.** An artist who understands particle effects but not C++ can create and tune every effect in the game.
3. **Runtime flexibility.** Effects can differ between levels, difficulty modes, or platforms by loading different data directories.
4. **Testability.** JSON files can be validated with a schema checker before they ever reach the engine.

The trade-off is indirection. Debugging "why does this particle look wrong" now means checking both the JSON file and the generic system code, instead of one specific C++ function. Good logging and error messages mitigate this.

### Factory Pattern

The `ParticleEffectManager` is a factory. It takes a name (or definition), constructs the corresponding object (an emitter entity with the right component), and returns it. The caller does not know the construction details — it just says "give me a blood_splatter at this position."

Factories appear whenever you need to create objects from a description rather than writing explicit construction code at every call site. The description can come from a file (as here), a network message, a script, or a level editor.

### Piecewise Linear Interpolation

The colour and size curve evaluation performs piecewise linear interpolation: the curve is a series of straight line segments between keys. Within each segment, we lerp between the two endpoints.

```
  VALUE
    │
  1.0 ──── * ─────────────────── *
    │      /                      \
  0.7 ── * ─                       \
    │    /                          \
  0.3 ─ * ─                         * ──
    │  /                                 \
  0.0 *                                    *
    └───┬────┬────┬────┬────┬────┬────┬────┬──
       0.0  0.15 0.25  0.4  0.55  0.7  0.85 1.0
                        TIME

  Each * is a key. Between keys, the value changes linearly.
  More keys = smoother curve. 4-6 keys is typical.
```

This is the simplest useful curve representation. More sophisticated engines use cubic splines (Catmull-Rom, Bezier) for smoother transitions, but piecewise linear is easy to implement, easy to edit in JSON, and visually sufficient for particle effects where individual particles are small and short-lived.

---

## What's Next — Series Summary

This chapter completes the particle system. Over three chapters (20, 45, and 46) we went from a basic object pool with gravity and colour interpolation to a full data-driven particle engine with collision, forces, curves, sub-emitters, flipbook animation, trails, soft rendering, and hot-reload. An artist can define any effect the game needs by editing a JSON file.

This also concludes the QEngine tutorial series. Here is the complete journey across all 46 chapters:

**Foundation (Chapters 1-10):** Window creation, OpenGL context, input handling, the game loop, ECS architecture with EnTT, basic rendering, textures, cameras, and the entity-component-system philosophy that every subsequent chapter builds on.

**Core Gameplay (Chapters 11-20):** Player movement and FPS controls, collision detection and response, weapon systems, enemy AI, health and damage, pickups and inventory, level loading, audio, basic HUD, and the first particle system.

**Engine Infrastructure (Chapters 21-30):** Game state machine, menus, save/load with JSON serialisation, skyboxes, weapon view models, boss encounters, developer console, framebuffers and post-processing, shadow mapping, and font rendering.

**Visual Polish (Chapters 31-40):** Decals, frustum culling, skeletal animation, level transitions, normal mapping, model loading, pathfinding, instanced rendering, water rendering, and animation events.

**Advanced Systems (Chapters 41-46):** Ragdoll physics, animation layers, inverse kinematics, PBR materials, advanced particle physics and rendering, and this chapter's data-driven particle effects.

The engine that started as an empty window with a triangle is now a complete Quake-style FPS framework: an ECS-driven architecture, skeletal animation with IK and ragdolls, PBR materials with normal and shadow mapping, a full particle system rivalling commercial middleware, AI pathfinding, save/load, and developer tools.

Every system follows the same principles: components hold data, systems hold logic, data files drive content, and the code stays readable. The architecture scales. Adding a new feature means adding a component and a system — not rewriting what came before.

Build something with it.