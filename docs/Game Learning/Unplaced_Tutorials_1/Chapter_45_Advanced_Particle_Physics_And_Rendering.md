# Chapter 45: Advanced Particle Physics & Rendering

## What You'll Learn
- Why the basic particle system from Chapter 20 breaks immersion and what upgrades fix it
- Particle-world collision: raycasting along the velocity vector, reflection, friction, and kill-on-hit with decal spawning
- Extending the Particle struct with new properties while keeping backward compatibility
- Force fields: drag, wind, point attractors/repulsors, and noise-based turbulence
- Particle rotation: angular velocity and screen-space billboard spinning
- Flipbook texture animation: atlas UVs, frame indexing, and optional frame blending
- Particle trails and ribbons: ring buffers of position history, triangle strip construction, and tapering
- Soft particles: depth-buffer comparison to fade hard intersection edges
- Performance trade-offs for each feature and when to use them
- C++ concepts: ring buffers, force accumulation, and hash-based noise functions

---

## Where We Left Off

In Chapter 20 we built a particle system from scratch. It works: an object pool of 4096 particles, billboard rendering with batched draw calls, gravity-based physics, colour and size interpolation over lifetime, additive and alpha blending modes, and a handful of specific effects — three-layer explosions, muzzle flashes, wall impact sparks, and smoke.

Here is what each particle currently stores:

```cpp
struct Particle {
    glm::vec3 position;
    glm::vec3 velocity;
    glm::vec4 colorStart;
    glm::vec4 colorEnd;
    float     sizeStart;
    float     sizeEnd;
    float     lifetime;
    float     age;
    bool      active;
};
```

The update loop is simple: for each active particle, add gravity to velocity, integrate position, advance age, kill if expired. Rendering builds a batch of camera-facing quads, interpolates colour and size, and draws them in one call.

This gets us 80% of the way. But the remaining 20% is where the polish lives:

```
WHAT WE HAVE                        WHAT'S MISSING
──────────────────────────           ──────────────────────────────
Particles fall with gravity          Sparks pass through floors
Explosions look decent               No tumbling debris rotation
Smoke fades out                      Smoke moves in straight lines
Muzzle flash pops                    No animated fire textures
Bullets leave sparks                 No bullet tracer trails
Particles clip into walls            Hard edges where they meet geometry
```

This chapter upgrades the existing system. We are not replacing it — we are extending the `Particle` struct, adding new systems alongside the existing update loop, and writing new shaders that build on the existing billboard renderer.

---

## Extended Particle Properties

Before we build any new features, we need to extend the particle struct. Every new field gets a default value that matches the old behaviour so that existing emitter code continues to work without changes.

```cpp
struct Particle {
    // --- existing fields (from Ch 20) ---
    glm::vec3 position;
    glm::vec3 velocity;
    glm::vec4 colorStart;
    glm::vec4 colorEnd;
    float     sizeStart;
    float     sizeEnd;
    float     lifetime;
    float     age;
    bool      active;

    // --- new fields (Ch 45) ---
    float     rotation         = 0.0f;   // current angle in radians
    float     angularVelocity  = 0.0f;   // radians per second
    float     drag             = 0.0f;   // velocity damping coefficient
    bool      collisionEnabled = false;  // test against world geometry?
    float     restitution      = 0.5f;   // bounciness (0 = dead stop, 1 = full bounce)
    float     friction         = 0.3f;   // tangential velocity reduction on bounce
    bool      killOnCollision  = false;  // die on first hit?
    int       textureIndex     = -1;     // flipbook frame override (-1 = use age-based)
    int       flipbookColumns  = 1;      // atlas grid width
    int       flipbookRows     = 1;      // atlas grid height
    bool      trailEnabled     = false;  // spawn trail geometry?
};
```

With `drag = 0`, `collisionEnabled = false`, `rotation = 0`, `flipbookColumns = 1`, and `trailEnabled = false`, the particle behaves exactly as it did in Chapter 20. Existing emitters that spawn explosions and sparks do not need a single line changed.

---

## Particle-World Collision

### The Problem

Fire the shotgun at a wall. Sparks fly out — and then fall straight through the floor, disappearing into the void below the level. A grenade explodes near a pillar. Debris particles fly through the pillar as if it were not there. Blood from a headshot passes through the wall behind the enemy. Every time this happens, the player's brain registers that the particles are fake. Immersion breaks.

The fix: test each collision-enabled particle against world geometry every frame and bounce or kill it when it hits something.

### The Approach

Each frame, a particle moves from its old position to a new position. That movement defines a ray. If that ray intersects any world geometry and the intersection distance is less than the distance the particle moved, the particle has hit something.

```
PARTICLE COLLISION — RAYCAST ALONG VELOCITY

    oldPos ─────────────────●───── newPos (no collision)
                            │
                            │ velocity * dt
                            │
    oldPos ──────●──────────┼──── would-be newPos
                 │          │
                 │ hit!     │ wall
                 │          │
                 ▼          │
            reflect off     │
            the normal      │
                            │

    The ray is: origin = oldPos, direction = normalize(velocity), length = |velocity| * dt
    If the raycast hits geometry at distance d < length, the particle collided.
```

### Collision Response

When a particle hits a surface with normal `N`, we split its velocity into normal and tangential components, reflect the normal component, and reduce both by restitution and friction.

```
VELOCITY REFLECTION

         incident           reflected
          velocity           velocity
            \  ↑  N (normal)   /
             \ │              /
              \│             /
    ───────────●────────────────── surface
               hit point

    Vn = dot(V, N) * N           (normal component)
    Vt = V - Vn                  (tangential component)

    V_reflected = Vt * (1 - friction) - Vn * restitution
```

### The Code

This integrates directly into the particle update loop. We check collision only when the particle has `collisionEnabled` set to true.

```cpp
// Forward declaration — uses the spatial hash from Ch 9 and raycast from Ch 10
struct RaycastHit {
    glm::vec3 point;
    glm::vec3 normal;
    float     distance;
    bool      hit;
};

RaycastHit raycastWorld(const glm::vec3& origin, const glm::vec3& direction,
                        float maxDistance, const SpatialHash& spatialHash);

// Spawn a decal at the collision point (from Ch 31)
void spawnDecal(entt::registry& registry, const glm::vec3& position,
                const glm::vec3& normal, DecalType type);

void updateParticleCollision(Particle& p, float dt, const SpatialHash& spatialHash,
                             entt::registry& registry)
{
    if (!p.collisionEnabled || !p.active) return;

    glm::vec3 movement = p.velocity * dt;
    float moveLength = glm::length(movement);

    // Skip if the particle is barely moving
    if (moveLength < 0.0001f) return;

    glm::vec3 moveDir = movement / moveLength;

    RaycastHit hit = raycastWorld(p.position, moveDir, moveLength, spatialHash);

    if (!hit.hit) return;

    if (p.killOnCollision) {
        // Blood splatter, paint — die and leave a mark
        spawnDecal(registry, hit.point, hit.normal, DecalType::BloodSplat);
        p.active = false;
        return;
    }

    // Move particle to the hit point, pulled back slightly to avoid penetration
    p.position = hit.point + hit.normal * 0.01f;

    // Decompose velocity into normal and tangential components
    float vDotN = glm::dot(p.velocity, hit.normal);

    // Only respond if moving into the surface (negative dot = towards surface)
    if (vDotN >= 0.0f) return;

    glm::vec3 vNormal     = vDotN * hit.normal;
    glm::vec3 vTangential = p.velocity - vNormal;

    // Reflect: reverse normal component with restitution, reduce tangential with friction
    p.velocity = vTangential * (1.0f - p.friction) - vNormal * p.restitution;

    // Kill near-zero velocity particles resting on surfaces to avoid jittering
    if (glm::length(p.velocity) < 0.1f) {
        p.velocity = glm::vec3(0.0f);
    }
}
```

### Integrating Into the Update Loop

The collision check happens after velocity integration but before the position is finalised:

```cpp
void updateParticles(std::vector<Particle>& pool, float dt,
                     const SpatialHash& spatialHash, entt::registry& registry)
{
    for (auto& p : pool) {
        if (!p.active) continue;

        // Age and kill
        p.age += dt;
        if (p.age >= p.lifetime) {
            p.active = false;
            continue;
        }

        // Forces (gravity + drag + wind — see next section)
        glm::vec3 acceleration = glm::vec3(0.0f, -9.81f, 0.0f);
        acceleration += -p.drag * p.velocity;  // drag force
        p.velocity += acceleration * dt;

        // Collision (before position update)
        updateParticleCollision(p, dt, spatialHash, registry);

        // Position update (only if still active — collision may have killed it)
        if (p.active) {
            p.position += p.velocity * dt;
        }

        // Rotation
        p.rotation += p.angularVelocity * dt;
    }
}
```

---

## Force Fields: Drag, Wind, and Turbulence

Gravity alone makes every particle behave the same way: launch, arc, fall. Real particles interact with the air around them. Smoke billows because turbulence pushes it. Sparks slow down because of drag. Leaves drift because of wind. Adding these forces costs almost nothing in performance and massively improves visual quality.

### Force Accumulation

The pattern is simple: each frame, for each particle, gather all applicable forces into an acceleration vector, then integrate velocity.

```
FORCE ACCUMULATION

    ┌──────────┐
    │ Gravity  │──→ (0, -9.81, 0)
    └──────────┘         │
    ┌──────────┐         ▼
    │   Drag   │──→ -drag * velocity ──→  SUM  ──→ acceleration ──→ velocity += acc * dt
    └──────────┘         ▲
    ┌──────────┐         │
    │   Wind   │──→ windDir * strength
    └──────────┘         ▲
    ┌──────────┐         │
    │Turbulence│──→ noise-based force
    └──────────┘         ▲
    ┌──────────┐         │
    │Attractor │──→ directional pull/push
    └──────────┘
```

### Drag

Drag is a velocity-dependent force that opposes motion. The faster a particle moves, the more the air slows it down.

```
F_drag = -drag * velocity
```

With `drag = 0`, there is no air resistance (the Chapter 20 default). A value of `2.0` creates noticeable deceleration. Smoke uses high drag (3-5) so it slows to a gentle drift. Sparks use low drag (0.5-1.0) so they arc naturally.

Drag is essentially free — one multiply per particle per frame. Use it on everything.

### Wind

Wind is a constant directional force applied to all particles. It is defined globally:

```cpp
struct WindSettings {
    glm::vec3 direction = glm::vec3(1.0f, 0.0f, 0.0f);  // normalised wind direction
    float     strength  = 0.0f;                            // force magnitude
};
```

The force contribution is simply:

```cpp
glm::vec3 windForce = wind.direction * wind.strength;
```

Wind makes smoke drift sideways, rain fall at an angle, and leaves blow across the scene. It costs one addition per particle.

### Point Attractors and Repulsors

A local force field pulls or pushes particles within a radius. This creates vortex effects, implosions, and shockwave pushback.

```cpp
struct ForceField {
    glm::vec3 position;
    float     strength;   // positive = attract, negative = repel
    float     radius;     // falloff distance
};

glm::vec3 computeForceFieldEffect(const Particle& p, const ForceField& field)
{
    glm::vec3 toField = field.position - p.position;
    float dist = glm::length(toField);

    if (dist > field.radius || dist < 0.001f) {
        return glm::vec3(0.0f);
    }

    // Linear falloff: full strength at centre, zero at radius edge
    float falloff = 1.0f - (dist / field.radius);
    glm::vec3 direction = toField / dist;

    return direction * field.strength * falloff;
}
```

Use a positive strength to pull particles inward (black hole effect, vortex). Use a negative strength to push them away (explosion shockwave, forcefield repulsion).

### Turbulence

Turbulence is the key to organic-looking particles. Without it, smoke rises in a straight column and fire is a rigid cone. Turbulence adds pseudo-random forces that change smoothly over space and time, creating swirling, chaotic motion.

We need a noise function. A full Perlin noise implementation is overkill — a simple hash-based noise gives us enough variation for particle forces. Here is a minimal 3D noise function:

```cpp
// Simple hash-based noise — returns value in [-1, 1]
namespace ParticleNoise {

inline float hash(float n)
{
    // Bit-cast float to int, scramble, cast back
    int i = static_cast<int>(n * 127.1f);
    i = (i << 13) ^ i;
    return 1.0f - static_cast<float>((i * (i * i * 15731 + 789221) + 1376312589) & 0x7fffffff)
           / 1073741824.0f;
}

float noise3D(float x, float y, float z)
{
    // Floor to grid cell
    float fx = std::floor(x);
    float fy = std::floor(y);
    float fz = std::floor(z);

    // Fractional position within cell
    float dx = x - fx;
    float dy = y - fy;
    float dz = z - fz;

    // Smooth interpolation weights
    float wx = dx * dx * (3.0f - 2.0f * dx);
    float wy = dy * dy * (3.0f - 2.0f * dy);
    float wz = dz * dz * (3.0f - 2.0f * dz);

    // Hash at 8 corners of the cell
    float n = fx * 157.0f + fy * 113.0f + fz * 271.0f;

    float c000 = hash(n);
    float c100 = hash(n + 157.0f);
    float c010 = hash(n + 113.0f);
    float c110 = hash(n + 157.0f + 113.0f);
    float c001 = hash(n + 271.0f);
    float c101 = hash(n + 157.0f + 271.0f);
    float c011 = hash(n + 113.0f + 271.0f);
    float c111 = hash(n + 157.0f + 113.0f + 271.0f);

    // Trilinear interpolation
    float x0 = glm::mix(c000, c100, wx);
    float x1 = glm::mix(c010, c110, wx);
    float x2 = glm::mix(c001, c101, wx);
    float x3 = glm::mix(c011, c111, wx);

    float y0 = glm::mix(x0, x1, wy);
    float y1 = glm::mix(x2, x3, wy);

    return glm::mix(y0, y1, wz);
}

} // namespace ParticleNoise
```

The turbulence force samples noise at the particle's position, offset by time so the field evolves:

```cpp
glm::vec3 computeTurbulence(const glm::vec3& position, float time,
                            float frequency, float amplitude)
{
    float offsetTime = time * 0.7f;  // time evolution speed

    // Sample noise for each axis with different offsets to avoid correlation
    float fx = ParticleNoise::noise3D(
        position.x * frequency + offsetTime,
        position.y * frequency,
        position.z * frequency);
    float fy = ParticleNoise::noise3D(
        position.x * frequency,
        position.y * frequency + offsetTime + 31.416f,
        position.z * frequency);
    float fz = ParticleNoise::noise3D(
        position.x * frequency,
        position.y * frequency,
        position.z * frequency + offsetTime + 67.123f);

    return glm::vec3(fx, fy, fz) * amplitude;
}
```

`frequency` controls the spatial scale — low values (0.5) make broad, sweeping gusts; high values (3.0) make tight, jittery disturbance. `amplitude` controls the force strength.

### Complete Force Integration

Putting it all together in the particle update:

```cpp
void updateParticlePhysics(Particle& p, float dt, float currentTime,
                           const WindSettings& wind,
                           const std::vector<ForceField>& forceFields)
{
    // Start with gravity
    glm::vec3 acceleration = glm::vec3(0.0f, -9.81f, 0.0f);

    // Drag (velocity-dependent deceleration)
    acceleration += -p.drag * p.velocity;

    // Wind (global directional force)
    acceleration += wind.direction * wind.strength;

    // Turbulence (noise-based organic motion)
    // Only apply to particles that benefit — smoke, fire, magic effects
    if (p.drag > 0.0f) {  // cheap heuristic: dragged particles are floaty enough to be turbulent
        acceleration += computeTurbulence(p.position, currentTime, 1.5f, 3.0f);
    }

    // Local force fields
    for (const auto& field : forceFields) {
        acceleration += computeForceFieldEffect(p, field);
    }

    // Integrate velocity
    p.velocity += acceleration * dt;
}
```

---

## Particle Rotation

### Why Rotation Matters

A non-rotating billboard is fine for glowing dots, point sparks, and simple smoke. But debris chunks, large smoke puffs, and leaf particles look obviously fake when they always face the same orientation. Rotation breaks up the uniformity.

### The Update

The rotation update is trivial — one multiply and one add per particle:

```cpp
p.rotation += p.angularVelocity * dt;
```

When spawning a particle, randomise both values:

```cpp
p.rotation        = randomFloat(0.0f, glm::two_pi<float>());
p.angularVelocity = randomFloat(-3.0f, 3.0f);  // radians/sec
```

### Shader Modification

In the billboard vertex shader from Chapter 20, we already compute the four corners of a camera-facing quad. To add rotation, we rotate those corner offsets around the billboard's forward axis (which is the camera look direction) by the particle's rotation angle.

```glsl
// vertex shader — particle_billboard.vert
#version 460 core

layout(location = 0) in vec3  aWorldPos;     // particle centre (instanced)
layout(location = 1) in vec4  aColor;        // interpolated colour (instanced)
layout(location = 2) in float aSize;         // interpolated size (instanced)
layout(location = 3) in float aRotation;     // rotation angle in radians (instanced)
layout(location = 4) in vec2  aUVOffset;     // flipbook UV offset (instanced)
layout(location = 5) in vec2  aUVScale;      // flipbook UV scale (instanced)
layout(location = 6) in int   aVertexID;     // 0-3 for quad corners

uniform mat4 uView;
uniform mat4 uProjection;

out vec4 vColor;
out vec2 vTexCoord;

void main()
{
    // Base quad corners in billboard space (centred at origin)
    vec2 corners[4] = vec2[4](
        vec2(-0.5, -0.5),
        vec2( 0.5, -0.5),
        vec2( 0.5,  0.5),
        vec2(-0.5,  0.5)
    );

    // UV coordinates for the quad corners
    vec2 uvs[4] = vec2[4](
        vec2(0.0, 0.0),
        vec2(1.0, 0.0),
        vec2(1.0, 1.0),
        vec2(0.0, 1.0)
    );

    vec2 corner = corners[aVertexID];

    // --- Rotation in screen space ---
    float cosR = cos(aRotation);
    float sinR = sin(aRotation);
    vec2 rotated = vec2(
        corner.x * cosR - corner.y * sinR,
        corner.x * sinR + corner.y * cosR
    );

    // Scale
    rotated *= aSize;

    // Billboard: extract camera right and up from the view matrix
    vec3 camRight = vec3(uView[0][0], uView[1][0], uView[2][0]);
    vec3 camUp    = vec3(uView[0][1], uView[1][1], uView[2][1]);

    // World position of this vertex
    vec3 worldPos = aWorldPos + camRight * rotated.x + camUp * rotated.y;

    gl_Position = uProjection * uView * vec4(worldPos, 1.0);

    vColor = aColor;

    // Apply flipbook UV transform
    vTexCoord = uvs[aVertexID] * aUVScale + aUVOffset;
}
```

The rotation happens in 2D screen space before the billboard axes expand it into 3D. This means the particle rotates as the player sees it — a smoke puff spins like a pinwheel — which is the standard approach used by every particle engine.

---

## Flipbook Texture Animation

### The Problem

A single static texture can only represent one moment. An explosion is not a single frame — it is a sequence: flash, fireball expansion, smoke dissipation. A static orange circle never looks like a real explosion. Animated textures fix this.

### Texture Atlas Layout

A flipbook texture is a grid of animation frames packed into a single texture. For example, a 4x4 atlas contains 16 frames:

```
FLIPBOOK TEXTURE ATLAS (4x4 = 16 frames)

    ┌──────┬──────┬──────┬──────┐
    │  0   │  1   │  2   │  3   │  Row 0
    │ flash│ flash│ fire │ fire │
    ├──────┼──────┼──────┼──────┤
    │  4   │  5   │  6   │  7   │  Row 1
    │ fire │expand│expand│expand│
    ├──────┼──────┼──────┼──────┤
    │  8   │  9   │  10  │  11  │  Row 2
    │smoke │smoke │smoke │ fade │
    ├──────┼──────┼──────┼──────┤
    │  12  │  13  │  14  │  15  │  Row 3
    │ fade │ fade │ wisp │empty │
    └──────┴──────┴──────┴──────┘

    Each cell is 1/4 of the texture width and 1/4 of the texture height.
    Frame 0 is top-left. Frame 15 is bottom-right.
    UV offset for frame i: x = (i % cols) / cols, y = (i / cols) / rows
    UV scale: (1/cols, 1/rows)
```

### UV Calculation

Given a frame index and the grid dimensions, computing the UV offset and scale is straightforward:

```cpp
struct FlipbookUV {
    glm::vec2 offset;
    glm::vec2 scale;
};

FlipbookUV computeFlipbookUV(int frameIndex, int columns, int rows)
{
    int col = frameIndex % columns;
    int row = frameIndex / columns;

    FlipbookUV uv;
    uv.scale  = glm::vec2(1.0f / columns, 1.0f / rows);
    uv.offset = glm::vec2(col * uv.scale.x, row * uv.scale.y);
    return uv;
}
```

### Frame Selection

The current frame is determined by the particle's normalised age (age / lifetime) mapped across the total frame count:

```cpp
int computeFlipbookFrame(const Particle& p)
{
    int totalFrames = p.flipbookColumns * p.flipbookRows;
    float normalizedAge = p.age / p.lifetime;

    // Clamp to [0, totalFrames - 1]
    int frame = static_cast<int>(normalizedAge * totalFrames);
    return glm::clamp(frame, 0, totalFrames - 1);
}
```

### Frame Interpolation

For smoother animation, we can blend between two adjacent frames in the fragment shader. Instead of a hard cut from frame 5 to frame 6, we cross-fade:

```cpp
struct FlipbookBlendData {
    FlipbookUV current;
    FlipbookUV next;
    float      blendFactor;  // 0 = fully current, 1 = fully next
};

FlipbookBlendData computeFlipbookBlend(const Particle& p)
{
    int totalFrames = p.flipbookColumns * p.flipbookRows;
    float normalizedAge = p.age / p.lifetime;
    float frameFloat = normalizedAge * static_cast<float>(totalFrames);

    int currentFrame = static_cast<int>(frameFloat);
    int nextFrame    = currentFrame + 1;

    currentFrame = glm::clamp(currentFrame, 0, totalFrames - 1);
    nextFrame    = glm::clamp(nextFrame, 0, totalFrames - 1);

    FlipbookBlendData data;
    data.current     = computeFlipbookUV(currentFrame, p.flipbookColumns, p.flipbookRows);
    data.next        = computeFlipbookUV(nextFrame, p.flipbookColumns, p.flipbookRows);
    data.blendFactor = frameFloat - std::floor(frameFloat);
    return data;
}
```

### Flipbook Fragment Shader

The fragment shader samples the atlas texture at the computed UV coordinates. When frame interpolation is enabled, it samples both frames and blends:

```glsl
// fragment shader — particle_flipbook.frag
#version 460 core

in vec4 vColor;
in vec2 vTexCoord;

// For frame blending, the vertex shader passes both UV sets
in vec2 vTexCoordNext;
in float vBlendFactor;

uniform sampler2D uParticleAtlas;
uniform bool uEnableFrameBlend;

out vec4 FragColor;

void main()
{
    vec4 texCurrent = texture(uParticleAtlas, vTexCoord);

    vec4 texFinal;
    if (uEnableFrameBlend) {
        vec4 texNext = texture(uParticleAtlas, vTexCoordNext);
        texFinal = mix(texCurrent, texNext, vBlendFactor);
    } else {
        texFinal = texCurrent;
    }

    FragColor = texFinal * vColor;

    // Discard near-transparent fragments to avoid depth-buffer issues
    if (FragColor.a < 0.01) discard;
}
```

### Use Cases

- **Explosion fireballs**: 4x4 atlas, frames 0-15 showing flash → fireball → smoke → fade
- **Animated fire**: 4x2 atlas looping 8 frames of flickering flame
- **Smoke puffs**: 2x2 atlas with 4 variations of dissipating smoke
- **Electric sparks**: 4x1 atlas with jagged bolt shapes that change each frame

Flipbook animation is cheap. It is just UV math on the CPU and a single texture sample in the shader. Use it everywhere you can — it transforms flat billboard particles into convincing animated effects.

---

## Particle Trails / Ribbons

### The Concept

A trail is a strip of connected geometry that follows a particle's path. Instead of a single billboard at the particle's current position, the trail records where the particle has been and draws a ribbon connecting those positions. The result: rocket exhaust trails, bullet tracers, energy beams, and sword swipe arcs.

```
TRAIL GEOMETRY — TRIANGLE STRIP

    Camera view:
                         head (current position)
                        ╱│
                       ╱ │ wide, opaque
                      ╱  │
                     ╱   │
                    ╱    │
                   ╱     │  ← connected quads forming a strip
                  ╱      │
                 ╱       │
                ╱        │ narrow, transparent
               ╱         │
              tail (oldest position)

    Side view:
    pos[0]    pos[1]    pos[2]    pos[3]    pos[4]   (ring buffer)
      ●─────────●─────────●─────────●─────────●      (centre line)
     ╱│        ╱│        ╱│        ╱│        ╱│
    ● │       ● │       ● │       ● │       ● │      (top vertices)
    │ ●       │ ●       │ ●       │ ●       │ ●      (bottom vertices)
    └─┘       └─┘       └─┘       └─┘       └─┘
   tail                                     head

    Each position produces two vertices (above and below centre).
    Connected as a triangle strip, they form the ribbon.
```

### Trail Data Structure

Each trail particle stores a ring buffer of recent positions. A ring buffer is a fixed-size array with a write index that wraps around — when the buffer is full, new entries overwrite the oldest ones.

```cpp
constexpr int TRAIL_HISTORY_SIZE = 16;

struct TrailParticle {
    Particle    base;                                 // the underlying particle
    glm::vec3   positionHistory[TRAIL_HISTORY_SIZE];  // ring buffer
    int         historyWriteIndex = 0;                // next write position
    int         historyCount      = 0;                // how many valid entries (up to TRAIL_HISTORY_SIZE)
    float       trailWidth        = 0.2f;             // width at the head
};
```

### Recording Positions

Each frame, after updating the particle's position, we record the new position into the ring buffer:

```cpp
void recordTrailPosition(TrailParticle& tp)
{
    tp.positionHistory[tp.historyWriteIndex] = tp.base.position;
    tp.historyWriteIndex = (tp.historyWriteIndex + 1) % TRAIL_HISTORY_SIZE;
    if (tp.historyCount < TRAIL_HISTORY_SIZE) {
        tp.historyCount++;
    }
}
```

### Reading the Ring Buffer in Order

To build the trail geometry, we need to iterate from the oldest position to the newest:

```cpp
glm::vec3 getTrailPosition(const TrailParticle& tp, int index)
{
    // index 0 = oldest, index (historyCount-1) = newest
    int bufferIndex;
    if (tp.historyCount < TRAIL_HISTORY_SIZE) {
        bufferIndex = index;
    } else {
        bufferIndex = (tp.historyWriteIndex + index) % TRAIL_HISTORY_SIZE;
    }
    return tp.positionHistory[bufferIndex];
}
```

### Building the Triangle Strip

For each recorded position, we create two vertices — one on each side of the trail centre — perpendicular to both the trail direction and the camera view direction:

```cpp
struct TrailVertex {
    glm::vec3 position;
    float     alpha;
    float     texU;
};

void buildTrailGeometry(const TrailParticle& tp, const glm::vec3& cameraPos,
                        std::vector<TrailVertex>& outVertices)
{
    if (tp.historyCount < 2) return;

    outVertices.clear();
    outVertices.reserve(tp.historyCount * 2);

    for (int i = 0; i < tp.historyCount; ++i) {
        glm::vec3 pos = getTrailPosition(tp, i);

        // Trail direction: vector to the next point (or from the previous point for the last)
        glm::vec3 trailDir;
        if (i < tp.historyCount - 1) {
            trailDir = glm::normalize(getTrailPosition(tp, i + 1) - pos);
        } else {
            trailDir = glm::normalize(pos - getTrailPosition(tp, i - 1));
        }

        // Camera direction to this point
        glm::vec3 toCamera = glm::normalize(cameraPos - pos);

        // Perpendicular vector (cross product gives the "side" direction)
        glm::vec3 side = glm::normalize(glm::cross(trailDir, toCamera));

        // Taper: width goes from 0 at the tail to full width at the head
        float t = static_cast<float>(i) / static_cast<float>(tp.historyCount - 1);
        float width = tp.trailWidth * t;

        // Alpha fades from transparent (tail) to opaque (head)
        float alpha = t;

        // Texture coordinate: U stretches along the trail length
        float texU = t;

        // Two vertices — one on each side
        outVertices.push_back({pos + side * width, alpha, texU});
        outVertices.push_back({pos - side * width, alpha, texU});
    }
}
```

### Trail Rendering

Trails use a separate VAO and VBO from the billboard particles because they use `GL_TRIANGLE_STRIP` instead of instanced quads:

```cpp
void renderTrails(const std::vector<TrailParticle>& trailPool,
                  const glm::vec3& cameraPos,
                  GLuint trailVAO, GLuint trailVBO,
                  GLuint trailShader,
                  const glm::mat4& view, const glm::mat4& projection)
{
    std::vector<TrailVertex> vertices;

    glUseProgram(trailShader);
    glUniformMatrix4fv(glGetUniformLocation(trailShader, "uView"), 1, GL_FALSE, &view[0][0]);
    glUniformMatrix4fv(glGetUniformLocation(trailShader, "uProjection"), 1, GL_FALSE,
                       &projection[0][0]);

    glBindVertexArray(trailVAO);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    for (const auto& tp : trailPool) {
        if (!tp.base.active || tp.historyCount < 2) continue;

        buildTrailGeometry(tp, cameraPos, vertices);

        glBindBuffer(GL_ARRAY_BUFFER, trailVBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        vertices.size() * sizeof(TrailVertex),
                        vertices.data());

        glDrawArrays(GL_TRIANGLE_STRIP, 0, static_cast<GLsizei>(vertices.size()));
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glBindVertexArray(0);
}
```

The trail vertex shader is simple — it just passes through position and alpha:

```glsl
// vertex shader — particle_trail.vert
#version 460 core

layout(location = 0) in vec3  aPosition;
layout(location = 1) in float aAlpha;
layout(location = 2) in float aTexU;

uniform mat4 uView;
uniform mat4 uProjection;
uniform vec4 uTrailColor;

out vec4 vColor;
out vec2 vTexCoord;

void main()
{
    gl_Position = uProjection * uView * vec4(aPosition, 1.0);
    vColor = vec4(uTrailColor.rgb, uTrailColor.a * aAlpha);
    vTexCoord = vec2(aTexU, 0.5);
}
```

```glsl
// fragment shader — particle_trail.frag
#version 460 core

in vec4 vColor;
in vec2 vTexCoord;

uniform sampler2D uTrailTexture;

out vec4 FragColor;

void main()
{
    vec4 tex = texture(uTrailTexture, vTexCoord);
    FragColor = tex * vColor;
}
```

### Use Cases

- **Rocket trails**: white-to-grey trail with high width, moderate history length
- **Bullet tracers**: thin, bright yellow trail with short history (4-6 positions)
- **Energy beams**: wide trail with additive blending and bright colour
- **Sword swipe arcs**: trail attached to the weapon tip, fades quickly

---

## Soft Particles

### The Problem

When a billboard particle intersects world geometry, there is a hard, visible edge where the particle's quad cuts into the surface. The player sees a perfect straight line where the smoke meets the floor or where an explosion clips through a wall. It looks terrible.

```
HARD PARTICLES (before)                SOFT PARTICLES (after)

    ┌─────────────────────┐            ┌─────────────────────┐
    │     particle        │            │     particle        │
    │    ┌────────┐       │            │    .  · · · .       │
    │    │ smoke  │       │            │   ·  smoke   ·      │
    │    │ ████████│      │            │  ·  ░░░▒▒▓▓██ ·     │
    ════════════════════════            ════════════════════════
    │    │ CLIPPED│ floor │            │    ·faded · floor   │
    │    └────────┘       │            │     · · ·           │
    │  Hard edge visible  │            │   Smooth fade       │
    └─────────────────────┘            └─────────────────────┘

    Left: the particle quad's bottom edge is a visible straight line.
    Right: the particle fades to transparent near the floor surface.
```

### The Solution

In the particle fragment shader, we compare the particle's depth with the depth of the scene geometry behind it. If the particle is close to the geometry, we fade its alpha. The closer it is, the more transparent it becomes.

This requires the scene depth buffer, which we already have from Chapter 28's framebuffer setup. We render the scene to an FBO first, then render particles as a separate pass with the depth texture bound.

### Depth Comparison

The key calculation:

```
particleDepth = the linear depth of this particle fragment
sceneDepth    = the linear depth of the scene at the same screen position
depthDiff     = sceneDepth - particleDepth

If depthDiff is small, the particle is close to geometry → fade alpha
If depthDiff is large, the particle is far from geometry → full alpha
```

### The Shader

```glsl
// fragment shader — particle_soft.frag
#version 460 core

in vec4 vColor;
in vec2 vTexCoord;

uniform sampler2D uParticleTexture;
uniform sampler2D uSceneDepth;       // depth buffer from the scene FBO (Ch 28)
uniform vec2      uScreenSize;       // viewport dimensions
uniform float     uNearPlane;
uniform float     uFarPlane;
uniform float     uSoftDistance;     // fade distance (e.g., 0.5 world units)

out vec4 FragColor;

float lineariseDepth(float depthNDC)
{
    // Convert from [0,1] non-linear depth to linear view-space depth
    return (2.0 * uNearPlane * uFarPlane) /
           (uFarPlane + uNearPlane - depthNDC * (uFarPlane - uNearPlane));
}

void main()
{
    vec4 tex = texture(uParticleTexture, vTexCoord);
    vec4 color = tex * vColor;

    // Screen-space UV for depth buffer lookup
    vec2 screenUV = gl_FragCoord.xy / uScreenSize;

    // Scene depth at this pixel
    float sceneDepthNDC = texture(uSceneDepth, screenUV).r;
    float sceneDepthLinear = lineariseDepth(sceneDepthNDC);

    // Particle depth at this pixel
    float particleDepthLinear = lineariseDepth(gl_FragCoord.z);

    // Depth difference
    float depthDiff = sceneDepthLinear - particleDepthLinear;

    // Soft fade: 0 when touching geometry, 1 when far away
    float softFactor = clamp(depthDiff / uSoftDistance, 0.0, 1.0);

    color.a *= softFactor;

    if (color.a < 0.01) discard;

    FragColor = color;
}
```

### Setting Up the Depth Texture

When rendering particles, bind the scene depth buffer from the FBO:

```cpp
void renderParticlesSoft(GLuint particleShader, GLuint sceneDepthTexture,
                         int screenWidth, int screenHeight,
                         float nearPlane, float farPlane)
{
    glUseProgram(particleShader);

    // Bind the scene depth buffer to texture unit 1
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, sceneDepthTexture);
    glUniform1i(glGetUniformLocation(particleShader, "uSceneDepth"), 1);

    glUniform2f(glGetUniformLocation(particleShader, "uScreenSize"),
                static_cast<float>(screenWidth), static_cast<float>(screenHeight));
    glUniform1f(glGetUniformLocation(particleShader, "uNearPlane"), nearPlane);
    glUniform1f(glGetUniformLocation(particleShader, "uFarPlane"), farPlane);
    glUniform1f(glGetUniformLocation(particleShader, "uSoftDistance"), 0.5f);

    // ... proceed with normal particle rendering (billboard batch draw)
}
```

The `uSoftDistance` uniform controls how far from geometry the fade begins. A value of `0.5` means particles start fading when they are within half a world unit of a surface. Increase it for larger particles (explosion fireballs) and decrease it for small particles (sparks).

---

## Performance Considerations

Not every feature should be applied to every particle. Here is a cost breakdown:

```
FEATURE                  COST PER PARTICLE    RECOMMENDATION
─────────────────────    ──────────────────    ─────────────────────────────────
Drag                     1 multiply            Use everywhere. Essentially free.
Rotation                 1 sin + 1 cos         Use freely. Cheap.
Flipbook UV              2 divides + 1 mod     Use freely. Just UV math.
Wind                     1 addition            Use everywhere. Free.
Turbulence (noise)       8 hashes + lerps      Use on smoke/fire. Skip on sparks.
Force fields             1 length + 1 divide   Use sparingly. Per-field cost.
Collision (raycast)      1 raycast per frame    Use ONLY on debris, blood, shells.
Trail (ring buffer)      16 positions stored    Limit to ~50 trail particles max.
Trail (rendering)        32 vertices per trail  Separate draw call per trail.
Soft particles           1 depth texture read   Apply to all — cost is per-pixel.
```

### Pool Separation

If your game has many particle types, consider separate pools:

```cpp
// Lightweight pool — no collision, no trails (smoke, sparks, muzzle flash)
std::vector<Particle>      particlePool(4096);

// Heavy pool — collision-enabled (debris, blood, shell casings)
std::vector<Particle>      collisionPool(256);

// Trail pool — position history per particle (rocket trails, tracers)
std::vector<TrailParticle> trailPool(64);
```

This keeps the common case fast. The main pool of 4096 particles runs the cheap path — gravity, drag, colour interpolation, flipbook UVs. Only the 256 collision particles pay the raycast cost. Only the 64 trail particles store position history and generate strip geometry.

### Shader Branching

Rather than having one uber-shader with branches for every feature, use separate shader programs:

- `particle_billboard` — basic billboard with colour, size, rotation, flipbook UVs
- `particle_soft` — same as above but with depth-buffer soft fade
- `particle_trail` — triangle strip trail rendering

Bind the appropriate shader for each pool. This avoids GPU branch divergence.

---

## C++ Concepts Introduced

### Ring Buffers

A ring buffer (also called a circular buffer) is a fixed-size array that wraps around. When the write index reaches the end, it resets to zero and overwrites the oldest data. This is ideal for trail position history: we always want the most recent N positions, and we never need to grow or shrink the storage.

```cpp
// Ring buffer write
buffer[writeIndex] = newValue;
writeIndex = (writeIndex + 1) % BUFFER_SIZE;

// Ring buffer read (from oldest to newest)
for (int i = 0; i < count; ++i) {
    int index = (writeIndex + i) % BUFFER_SIZE;
    // use buffer[index]
}
```

The modulo operator `%` handles the wrapping. No dynamic allocation, no shifting elements, constant-time insert and read. Ring buffers appear everywhere in game engines: input history, network packet buffers, audio sample queues, and here, particle trail positions.

### Force Accumulation

Force accumulation is a physics pattern where you compute all forces independently, sum them into a net force, convert to acceleration (F = ma, or just F if we treat particles as unit mass), and integrate once. The key insight is that forces are additive — gravity, drag, wind, and turbulence each contribute independently, and the sum produces the correct combined motion.

```
Individual forces:   F_gravity + F_drag + F_wind + F_turbulence + F_field
                            ↓
Net acceleration:    a = sum(all forces)
                            ↓
Velocity update:     v += a * dt
                            ↓
Position update:     p += v * dt
```

This pattern scales to any number of force types without changing the integration code. Adding a new force means adding one more term to the sum.

### Hash-Based Noise

The noise function in this chapter uses integer hashing to generate pseudo-random values at grid points, then interpolates between them. The hash function takes a seed (derived from grid coordinates), scrambles it with prime multiplications and bit shifts, and produces a deterministic but seemingly random output.

The critical property is **determinism**: the same input always produces the same output. This means the noise field is consistent — a particle at position (3.5, 2.1, 7.8) always experiences the same turbulence force, creating smooth, coherent motion rather than random jitter. By adding time to the input coordinates, the noise field evolves smoothly, making the turbulence drift and swirl over time.

---

## What's Next

The particle system now has physics (collision, drag, wind, turbulence), visual variety (rotation, flipbook animation, trails), and polish (soft particles). But every effect is hardcoded: the explosion in `spawnExplosion()` sets specific values for colour, size, drag, and lifetime in C++ code. Adding a new effect means writing a new function, recompiling, and testing.

In **Chapter 46: Data-Driven Particles & Sub-Emitters**, we will:

- Define particle effects in JSON files that can be edited without recompiling
- Build a `ParticleEffectDef` structure loaded from JSON: spawn rate, initial property ranges, force settings, flipbook atlas reference
- Implement sub-emitters: particles that spawn other particles (a firework rocket trail that explodes into sparks, a torch flame that spawns smoke that spawns embers)
- Create an emitter component for the ECS that references a named effect definition
- Build a hot-reload system so artists can tweak effects in JSON and see changes live

The combination of this chapter's physics and rendering features with Chapter 46's data-driven definitions will give QEngine a particle system that rivals commercial engines — powerful, flexible, and fast.