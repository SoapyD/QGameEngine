# Chapter 20: Particles, Effects & Polish

## What You'll Learn
- A particle system using object pools
- Muzzle flash, explosions, blood splats, and sparks
- Screen shake
- Interpolation functions — lerp, smoothstep, ease-in/out
- View bobbing (head movement while walking)
- Putting it all together into a polished game feel

---

## Why Polish Matters

The difference between a tech demo and a game is **feel**. A shotgun that spawns a flash, kicks the camera, throws sparks off the wall, and plays a punchy sound feels powerful. The same shotgun with no effects feels like clicking a button. Every piece of feedback reinforces the player's actions.

Quake nailed this: rockets leave smoke trails, explosions throw gibs, the lightning gun crackles with light, and taking damage jerks the camera. All built from simple primitives.

---

## The Particle System

A particle system manages hundreds of tiny, short-lived visual elements. Each particle has:
- Position, velocity (moves through space)
- Lifetime (fades and dies)
- Colour (can change over time)
- Size (can grow or shrink)

### Particle Data

```cpp
struct Particle {
    glm::vec3 position;
    glm::vec3 velocity;
    glm::vec4 colorStart;
    glm::vec4 colorEnd;
    float sizeStart;
    float sizeEnd;
    float lifetime;         // Total lifetime in seconds
    float age;              // Current age in seconds
    bool active = false;
};
```

### The Object Pool

Creating and destroying hundreds of particles per frame would thrash the allocator. Instead, we pre-allocate a fixed pool and reuse slots:

```cpp
class ParticlePool {
public:
    static constexpr int MAX_PARTICLES = 4096;

    ParticlePool() {
        m_particles.resize(MAX_PARTICLES);
    }

    // Get a free particle slot (returns nullptr if pool is full)
    Particle* allocate() {
        for (int i = 0; i < MAX_PARTICLES; i++) {
            int index = (m_nextFree + i) % MAX_PARTICLES;
            if (!m_particles[index].active) {
                m_nextFree = (index + 1) % MAX_PARTICLES;
                m_particles[index].active = true;
                return &m_particles[index];
            }
        }
        return nullptr;  // Pool exhausted
    }

    // Update all active particles
    void update(float dt) {
        for (auto& p : m_particles) {
            if (!p.active) continue;

            p.age += dt;
            if (p.age >= p.lifetime) {
                p.active = false;
                continue;
            }

            p.position += p.velocity * dt;

            // Optional: gravity on particles
            p.velocity.y -= 9.81f * dt;
        }
    }

    const std::vector<Particle>& particles() const { return m_particles; }

private:
    std::vector<Particle> m_particles;
    int m_nextFree = 0;
};
```

### C++ Concept: Object Pools

An object pool pre-allocates a fixed block of objects and recycles them. The benefits:
- **No allocation during gameplay** (allocations can cause frame hitches)
- **Cache-friendly** — all particles are contiguous in memory
- **Predictable memory usage** — you know the maximum upfront

The trade-off: fixed capacity. If you need more than `MAX_PARTICLES`, you either increase the pool or accept that excess particles are silently dropped. For a game, dropping a few particles is invisible — a frame hitch from allocation is not.

---

## Rendering Particles

Particles are **billboards** — flat quads that always face the camera. This creates the illusion of a volumetric effect.

### Billboard Math

```cpp
glm::mat4 createBillboardMatrix(const glm::vec3& particlePos,
                                  const glm::vec3& cameraPos,
                                  const glm::vec3& cameraUp) {
    glm::vec3 look = glm::normalize(cameraPos - particlePos);
    glm::vec3 right = glm::normalize(glm::cross(cameraUp, look));
    glm::vec3 up = glm::cross(look, right);

    glm::mat4 billboard(1.0f);
    billboard[0] = glm::vec4(right, 0.0f);
    billboard[1] = glm::vec4(up, 0.0f);
    billboard[2] = glm::vec4(look, 0.0f);
    billboard[3] = glm::vec4(particlePos, 1.0f);

    return billboard;
}
```

### Particle Shader

### assets/shaders/particle.vert

```glsl
#version 460 core

layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;

out vec2 TexCoord;
out vec4 ParticleColor;

uniform mat4 view;
uniform mat4 projection;
uniform vec3 particlePos;
uniform float particleSize;
uniform vec4 particleColor;

// Billboard: extract right and up from the view matrix
void main() {
    vec3 right = vec3(view[0][0], view[1][0], view[2][0]);
    vec3 up    = vec3(view[0][1], view[1][1], view[2][1]);

    vec3 worldPos = particlePos
        + right * aPos.x * particleSize
        + up    * aPos.y * particleSize;

    gl_Position = projection * view * vec4(worldPos, 1.0);
    TexCoord = aTexCoord;
    ParticleColor = particleColor;
}
```

### assets/shaders/particle.frag

```glsl
#version 460 core

in vec2 TexCoord;
in vec4 ParticleColor;
out vec4 FragColor;

uniform sampler2D particleTexture;
uniform bool useTexture;

void main() {
    vec4 color = ParticleColor;

    if (useTexture) {
        color *= texture(particleTexture, TexCoord);
    }

    // Soft circular falloff (if no texture)
    if (!useTexture) {
        float dist = length(TexCoord - vec2(0.5));
        float alpha = 1.0 - smoothstep(0.3, 0.5, dist);
        color.a *= alpha;
    }

    if (color.a < 0.01) discard;  // Skip nearly invisible fragments

    FragColor = color;
}
```

### Drawing All Particles

Add this function to `src/engine/ecs/systems/render_system.cpp` (or a dedicated `src/engine/ecs/systems/particle_render.cpp` if you prefer to keep rendering concerns separate):

```cpp
void renderParticles(const ParticlePool& pool, const Shader& particleShader,
                      unsigned int quadVAO, const Camera& camera,
                      float aspectRatio) {

    particleShader.use();
    particleShader.setMat4("view", camera.getViewMatrix());
    particleShader.setMat4("projection", camera.getProjectionMatrix(aspectRatio));
    particleShader.setInt("useTexture", 0);

    // Additive blending for fire/energy effects
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDepthMask(GL_FALSE);  // Don't write to depth buffer (particles are transparent)

    for (const auto& p : pool.particles()) {
        if (!p.active) continue;

        float t = p.age / p.lifetime;  // 0.0 → 1.0

        // Interpolate colour and size over lifetime
        glm::vec4 color = glm::mix(p.colorStart, p.colorEnd, t);
        float size = glm::mix(p.sizeStart, p.sizeEnd, t);

        particleShader.setVec3("particlePos", p.position);
        particleShader.setFloat("particleSize", size);
        particleShader.setVec4("particleColor", color);

        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    // Restore normal blending
    glDepthMask(GL_TRUE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}
```

**Additive blending** (`GL_SRC_ALPHA, GL_ONE`) makes overlapping particles brighter — perfect for fire, sparks, and energy. Standard alpha blending is better for smoke and dust.

---

## Particle Emitters — Spawning Effects

### Muzzle Flash

```cpp
void emitMuzzleFlash(ParticlePool& pool, const glm::vec3& position,
                       const glm::vec3& direction) {
    // 1 large flash particle
    Particle* flash = pool.allocate();
    if (flash) {
        flash->position = position + direction * 0.3f;
        flash->velocity = direction * 2.0f;
        flash->colorStart = glm::vec4(1.0f, 0.9f, 0.5f, 1.0f);   // Bright yellow
        flash->colorEnd   = glm::vec4(1.0f, 0.5f, 0.0f, 0.0f);   // Fade to transparent
        flash->sizeStart = 0.3f;
        flash->sizeEnd = 0.1f;
        flash->lifetime = 0.06f;  // Very brief
        flash->age = 0.0f;
    }

    // Small spark particles
    std::uniform_real_distribution<float> spread(-0.3f, 0.3f);
    std::uniform_real_distribution<float> speed(3.0f, 8.0f);
    std::mt19937 rng(std::random_device{}());

    for (int i = 0; i < 5; i++) {
        Particle* spark = pool.allocate();
        if (!spark) break;

        spark->position = position + direction * 0.2f;
        spark->velocity = direction * speed(rng)
            + glm::vec3(spread(rng), spread(rng), spread(rng));
        spark->colorStart = glm::vec4(1.0f, 0.8f, 0.3f, 1.0f);
        spark->colorEnd   = glm::vec4(1.0f, 0.3f, 0.0f, 0.0f);
        spark->sizeStart = 0.05f;
        spark->sizeEnd = 0.02f;
        spark->lifetime = 0.15f;
        spark->age = 0.0f;
    }
}
```

### Explosion

```cpp
void emitExplosion(ParticlePool& pool, const glm::vec3& position) {
    std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<float> dir(-1.0f, 1.0f);
    std::uniform_real_distribution<float> speed(2.0f, 10.0f);

    // Core flash
    Particle* core = pool.allocate();
    if (core) {
        core->position = position;
        core->velocity = glm::vec3(0.0f);
        core->colorStart = glm::vec4(1.0f, 0.9f, 0.7f, 1.0f);
        core->colorEnd   = glm::vec4(1.0f, 0.3f, 0.0f, 0.0f);
        core->sizeStart = 0.5f;
        core->sizeEnd = 3.0f;
        core->lifetime = 0.3f;
        core->age = 0.0f;
    }

    // Debris / sparks
    for (int i = 0; i < 30; i++) {
        Particle* p = pool.allocate();
        if (!p) break;

        glm::vec3 randomDir = glm::normalize(
            glm::vec3(dir(rng), dir(rng) * 0.5f + 0.5f, dir(rng)));

        p->position = position;
        p->velocity = randomDir * speed(rng);
        p->colorStart = glm::vec4(1.0f, 0.7f, 0.2f, 1.0f);
        p->colorEnd   = glm::vec4(0.3f, 0.1f, 0.0f, 0.0f);
        p->sizeStart = 0.15f;
        p->sizeEnd = 0.05f;
        p->lifetime = 0.5f + dir(rng) * 0.2f;
        p->age = 0.0f;
    }

    // Smoke (use standard alpha blend, not additive)
    for (int i = 0; i < 10; i++) {
        Particle* p = pool.allocate();
        if (!p) break;

        glm::vec3 randomDir = glm::normalize(
            glm::vec3(dir(rng), dir(rng) * 0.3f + 0.7f, dir(rng)));

        p->position = position;
        p->velocity = randomDir * 2.0f;
        p->colorStart = glm::vec4(0.3f, 0.3f, 0.3f, 0.6f);
        p->colorEnd   = glm::vec4(0.1f, 0.1f, 0.1f, 0.0f);
        p->sizeStart = 0.5f;
        p->sizeEnd = 2.0f;
        p->lifetime = 1.5f;
        p->age = 0.0f;
    }
}
```

### Wall Hit Sparks

```cpp
void emitWallSparks(ParticlePool& pool, const glm::vec3& position,
                      const glm::vec3& normal) {
    std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<float> spread(-0.5f, 0.5f);
    std::uniform_real_distribution<float> speed(2.0f, 6.0f);

    for (int i = 0; i < 8; i++) {
        Particle* p = pool.allocate();
        if (!p) break;

        glm::vec3 dir = normal + glm::vec3(spread(rng), spread(rng), spread(rng));
        dir = glm::normalize(dir);

        p->position = position + normal * 0.05f;
        p->velocity = dir * speed(rng);
        p->colorStart = glm::vec4(1.0f, 0.9f, 0.6f, 1.0f);
        p->colorEnd   = glm::vec4(0.5f, 0.2f, 0.0f, 0.0f);
        p->sizeStart = 0.04f;
        p->sizeEnd = 0.01f;
        p->lifetime = 0.3f;
        p->age = 0.0f;
    }
}
```

---

## Screen Shake

Camera shake sells impact. When a rocket explodes nearby or you take heavy damage:

```cpp
struct ScreenShake {
    float intensity = 0.0f;    // Current shake strength
    float duration = 0.0f;     // How long to shake
    float timer = 0.0f;
    float frequency = 25.0f;   // Oscillations per second
};

void triggerShake(ScreenShake& shake, float intensity, float duration) {
    // Only override if new shake is stronger
    if (intensity > shake.intensity) {
        shake.intensity = intensity;
        shake.duration = duration;
        shake.timer = 0.0f;
    }
}

void updateShake(ScreenShake& shake, float dt) {
    if (shake.duration <= 0.0f) return;

    shake.timer += dt;
    if (shake.timer >= shake.duration) {
        shake.intensity = 0.0f;
        shake.duration = 0.0f;
        return;
    }

    // Decay intensity over time
    float progress = shake.timer / shake.duration;
    shake.intensity *= (1.0f - progress);
}

glm::vec3 getShakeOffset(const ScreenShake& shake) {
    if (shake.intensity <= 0.001f) return glm::vec3(0.0f);

    // Use sine waves at different frequencies for organic feel
    float t = shake.timer * shake.frequency;
    float x = std::sin(t * 1.0f) * shake.intensity;
    float y = std::sin(t * 1.7f) * shake.intensity;  // Different frequency for Y
    float z = std::sin(t * 0.5f) * shake.intensity * 0.3f;  // Less Z shake

    return glm::vec3(x, y, z);
}
```

Apply the shake offset to the camera's view matrix:

```cpp
glm::mat4 view = camera.getViewMatrix();

// Add shake
glm::vec3 shakeOffset = getShakeOffset(screenShake);
view = glm::translate(view, shakeOffset);
```

Trigger from game events:
```cpp
// Nearby explosion
float distToExplosion = glm::length(playerPos - explosionPos);
float shakeStrength = std::max(0.0f, 1.0f - distToExplosion / 20.0f);
triggerShake(screenShake, shakeStrength * 0.5f, 0.3f);

// Taking damage
triggerShake(screenShake, 0.2f, 0.15f);

// Own rocket firing
triggerShake(screenShake, 0.05f, 0.1f);
```

---

## View Bobbing

The camera bobs gently while walking — simulating head movement:

```cpp
struct ViewBob {
    float bobTime = 0.0f;
    float bobAmountY = 0.03f;    // Vertical bob strength
    float bobAmountX = 0.015f;   // Horizontal bob strength
    float bobSpeed = 10.0f;      // Oscillation speed
};

glm::vec3 getViewBobOffset(ViewBob& bob, float playerSpeed, float dt) {
    if (playerSpeed < 0.5f) {
        // Not moving — decay bob
        bob.bobTime = 0.0f;
        return glm::vec3(0.0f);
    }

    bob.bobTime += dt * bob.bobSpeed;

    float y = std::sin(bob.bobTime) * bob.bobAmountY;
    float x = std::sin(bob.bobTime * 0.5f) * bob.bobAmountX;

    return glm::vec3(x, y, 0.0f);
}
```

---

## Interpolation Functions

Throughout this chapter (and the whole engine), we interpolate between values. Here are the common functions:

### Lerp (Linear)

```cpp
float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}
// GLM: glm::mix(a, b, t)
```

Constant speed from A to B. Used for most interpolation.

### Smoothstep (Ease In-Out)

```cpp
float smoothstep(float edge0, float edge1, float x) {
    float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
```

Starts slow, speeds up, slows down. Used for smooth transitions (door opening, fade effects).

### Ease In (Accelerating)

```cpp
float easeIn(float t) {
    return t * t;
}
```

Starts slow, ends fast. Used for things falling, building energy.

### Ease Out (Decelerating)

```cpp
float easeOut(float t) {
    return 1.0f - (1.0f - t) * (1.0f - t);
}
```

Starts fast, ends slow. Used for things coming to rest, explosions expanding.

### Spring (Damped Oscillation)

```cpp
float spring(float t, float frequency = 4.0f, float damping = 5.0f) {
    return 1.0f - std::exp(-damping * t) * std::cos(frequency * 3.14159f * t);
}
```

Overshoots and oscillates. Used for weapon recoil, UI elements.

---

## Weapon Recoil

The camera kicks back briefly when firing:

```cpp
struct WeaponRecoil {
    float currentPitch = 0.0f;   // Current recoil offset
    float targetPitch = 0.0f;    // Where we're recoiling to
    float recovery = 10.0f;      // How fast we return to center
};

void applyRecoil(WeaponRecoil& recoil, float amount) {
    recoil.targetPitch = amount;
}

void updateRecoil(WeaponRecoil& recoil, float dt) {
    // Snap to target
    recoil.currentPitch = glm::mix(recoil.currentPitch,
                                     recoil.targetPitch, dt * 20.0f);
    // Recover toward zero
    recoil.targetPitch = glm::mix(recoil.targetPitch, 0.0f,
                                    dt * recoil.recovery);
}
```

Apply to camera:
```cpp
camera.addPitchOffset(recoil.currentPitch);
```

Different weapons kick differently:
```cpp
// Shotgun: sharp kick
applyRecoil(recoil, 3.0f);

// Rocket launcher: heavy push
applyRecoil(recoil, 5.0f);

// Nailgun: rapid small kicks
applyRecoil(recoil, 0.5f);
```

---

## The Complete Render Order

```cpp
// 1. Clear
glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

// 2. Build view matrix with effects
glm::mat4 view = camera.getViewMatrix();
view = glm::translate(view, getShakeOffset(screenShake));
view = glm::translate(view, getViewBobOffset(viewBob, playerSpeed, dt));

// 3. Render 3D world (opaque)
glEnable(GL_DEPTH_TEST);
glDisable(GL_BLEND);
renderSystem(registry, view, projection);

// 4. Render particles (transparent, after opaque)
glEnable(GL_BLEND);
renderParticles(particlePool, particleShader, quadVAO, camera, aspectRatio);
glDisable(GL_BLEND);

// 5. Render HUD (2D, no depth)
glDisable(GL_DEPTH_TEST);
glEnable(GL_BLEND);
glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
// crosshair, health, ammo, damage flash, messages...
glDisable(GL_BLEND);
glEnable(GL_DEPTH_TEST);

// 6. Swap
window.swapBuffers();
```

---

## Hooking Effects Into Game Events

Each game event triggers the appropriate effects:

```cpp
// Weapon fired:
emitMuzzleFlash(particles, fireOrigin, fireDir);
triggerShake(shake, 0.05f, 0.1f);
applyRecoil(recoil, 3.0f);
registry.emplace_or_replace<PlaySoundOnce>(player, "shotgun_fire", 1.0f);

// Hitscan hit wall:
emitWallSparks(particles, hitPoint, hitNormal);
registry.emplace_or_replace<PlaySoundOnce>(/* wall entity or temp */, "ricochet", 0.7f);

// Rocket exploded:
emitExplosion(particles, explosionPos);
float dist = glm::length(cameraPos - explosionPos);
triggerShake(shake, std::max(0.0f, 1.0f - dist / 20.0f) * 0.5f, 0.3f);

// Player took damage:
triggerShake(shake, 0.2f, 0.15f);
auto& flash = registry.get<DamageFlash>(player);
flash.timer = flash.duration;

// Player picked up item:
registry.emplace_or_replace<PlaySoundOnce>(player, "pickup_health", 0.8f);
auto& hudMessages = registry.get<HUDMessages>(player);
hudMessages.add("Picked up Health +50");

// Enemy died:
emitExplosion(particles, enemyPos);  // Or blood particles
registry.emplace_or_replace<PlaySoundOnce>(enemy, "enemy_death", 1.0f);
```

Every event becomes a combination of: particles + sound + camera effect + HUD feedback. This layering is what creates game feel.

---

## Congratulations

You've built a complete Quake-style FPS engine in C++ with ECS architecture. Here's what you have:

| System | Chapter |
|--------|---------|
| Window & OpenGL | 1 |
| Shaders | 2 |
| ECS (EnTT) | 3 |
| 3D Camera & Transforms | 4 |
| Textures | 5 |
| Mesh & Model Loading | 6 |
| Lighting | 7 |
| Level Geometry & BSP | 8 |
| Collision Detection | 9 |
| Physics & Movement | 10 |
| Doors, Lifts, Triggers | 11 |
| Weapons & Projectiles | 12 |
| Items & Pickups | 13 |
| Enemy AI | 14 |
| HUD & UI | 15 |
| Audio | 16 |
| Networking | 17 |
| State Sync & Interpolation | 18 |
| Client-Side Prediction | 19 |
| Particles & Polish | 20 |

From a blank window to a networked, polished FPS engine — all using the ECS principle: **components have no behaviour, systems have no state**.

---

## Where to Go From Here

- **Add more enemy types** — flying enemies, bosses, different attack patterns
- **Build a level editor** — even a simple one saves hours over hand-editing files
- **Implement a console** — Quake's console let you tweak any variable at runtime
- **Add skeletal animation** — for player models and enemy animations
- **Optimise rendering** — frustum culling, occlusion queries, instanced rendering
- **Expand the shader system** — normal maps, shadow maps, post-processing (bloom, SSAO)
- **Build a BSP compiler** — generate BSP trees from brush geometry
- **Polish the netcode** — bandwidth limiting, packet loss handling, anti-cheat

The engine is a foundation. Every feature from here builds on what you already understand.
