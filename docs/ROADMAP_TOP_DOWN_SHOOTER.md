# QEngine — Top-Down Shooter Adaptation Roadmap

How to adapt the Quake-style FPS engine into a top-down shooter (think Hotline Miami, Nuclear Throne, Enter the Gungeon). This demonstrates ECS flexibility — most engine code stays, only the gameplay layer changes.

---

## What Changes vs What Stays

This is the key insight. The engine/game boundary in ECS makes genre-switching surgical:

### Stays Untouched
| System | Why It Stays |
|--------|-------------|
| Window & OpenGL context (Ch 1) | A window is a window |
| Shader system (Ch 2) | Shaders are game-agnostic |
| ECS foundation (Ch 3) | The whole point — components change, architecture doesn't |
| Textures & materials (Ch 5) | Same texture loading, different art |
| Mesh system (Ch 6) | Quads, sprites, and meshes all use the same VBO/VAO pipeline |
| Audio system (Ch 16) | 3D positional audio works in top-down too (just flatten the Y) |
| Networking (Ch 17-19) | Client-server model is genre-agnostic |
| Particles (Ch 20) | Explosions and muzzle flash work from any camera angle |

### Modified
| System | What Changes |
|--------|-------------|
| Camera (Ch 4) | FPS camera → fixed overhead camera, orthographic or perspective |
| Lighting (Ch 7) | Optional: switch to 2D lighting / shadows or keep 3D with top-down view |
| Level geometry (Ch 8) | 3D sectors → 2D tilemap or simple floor + wall geometry |
| Collision (Ch 9) | 3D AABB → 2D AABB or circle collision (simpler) |
| Physics (Ch 10) | Remove gravity/jumping, add 2D omnidirectional movement |
| HUD (Ch 15) | Different layout — score, wave counter, minimap |

### Replaced
| System | What It Becomes |
|--------|----------------|
| FPS input (mouselook + WASD) | Twin-stick input (WASD move, mouse aim) |
| Weapons (Ch 12 — hitscan/projectile) | Top-down weapons (same concepts, 2D trajectory) |
| AI (Ch 14 — 3D line of sight, A*) | 2D pathfinding, simpler LoS, wave spawning |
| Items/Pickups (Ch 13) | Weapon drops, power-ups, score multipliers |
| Doors/Triggers (Ch 11) | Room transitions, arena locks, spawn triggers |

---

## Phase 1: Camera & Rendering

**Goal**: See the world from above.

### Top-Down Camera

Replace the FPS camera with a fixed overhead camera. Two options:

**Option A: Orthographic (pure 2D feel, like Hotline Miami)**
```cpp
struct TopDownCamera {
    glm::vec3 target;          // What the camera looks at (player position)
    float height = 20.0f;      // How far above the action
    float zoom = 1.0f;         // Orthographic zoom level

    glm::mat4 getView() const {
        return glm::lookAt(
            glm::vec3(target.x, target.y + height, target.z),  // Eye above target
            target,                                              // Look at target
            glm::vec3(0.0f, 0.0f, -1.0f)                       // "Up" is -Z in top-down
        );
    }

    glm::mat4 getProjection(float aspect) const {
        float halfW = 10.0f / zoom;
        float halfH = halfW / aspect;
        return glm::ortho(-halfW, halfW, -halfH, halfH, 0.1f, 100.0f);
    }
};
```

**Option B: Perspective with high angle (slight 3D depth, like Diablo)**
```cpp
glm::mat4 getProjection(float aspect) const {
    return glm::perspective(glm::radians(45.0f), aspect, 0.1f, 200.0f);
}
```

Both reuse the existing shader uniform system — just swap the view/projection matrices.

### Sprite Rendering

Top-down games often use 2D sprites rather than 3D models. A sprite is just a textured quad (two triangles) — you already know how to render these from Chapters 5-6.

```cpp
struct Sprite {
    unsigned int textureId;    // Which texture to draw
    glm::vec2 size;            // World-space size
    glm::vec4 uvRect;          // Region of texture atlas (for animation frames)
    int layer = 0;             // Draw order (floor < entities < effects)
    glm::vec4 tint = {1,1,1,1}; // Colour tint / flash on damage
};
```

Render all sprites as camera-facing quads sorted by layer. The shader is nearly identical to the textured quad from Chapter 5, just with a spritesheet UV offset.

### What's New in Rendering
- **Sprite batching**: group sprites by texture, draw in one call (performance)
- **Draw order by layer**: floor tiles → shadows → entities → projectiles → effects → HUD
- **Rotation**: sprites rotate to face the aim direction (pass rotation as a uniform or vertex attribute)

---

## Phase 2: Input & Movement

**Goal**: WASD movement + mouse aiming (twin-stick style).

### New Input Components

```cpp
// Replaces PlayerInput from FPS
struct TopDownInput {
    glm::vec2 moveDirection;    // Normalised WASD input
    glm::vec2 aimDirection;     // Direction from player to mouse cursor
    float aimAngle;             // Angle in radians (for sprite rotation)
    bool firing = false;
    bool dashing = false;       // Optional: dodge roll
};
```

### Mouse-to-World Conversion

The mouse gives screen coordinates. You need world coordinates to calculate aim direction:

```cpp
glm::vec2 screenToWorld(const TopDownCamera& camera, glm::vec2 screenPos,
                         int screenWidth, int screenHeight) {
    // Normalise to [-1, 1]
    float ndcX = (2.0f * screenPos.x / screenWidth) - 1.0f;
    float ndcY = 1.0f - (2.0f * screenPos.y / screenHeight);

    // Inverse projection and view
    glm::mat4 invPV = glm::inverse(camera.getProjection(aspect) * camera.getView());
    glm::vec4 worldPos = invPV * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);

    return glm::vec2(worldPos.x / worldPos.w, worldPos.z / worldPos.w);
}
```

### Movement System

Simpler than FPS — no gravity, no jumping, no air control:

```cpp
void topDownMovementSystem(entt::registry& registry, float dt) {
    auto view = registry.view<Position, Velocity, TopDownInput, MoveSpeed>();

    for (auto [entity, pos, vel, input, speed] : view.each()) {
        // Direct movement from input
        vel.value.x = input.moveDirection.x * speed.value;
        vel.value.z = input.moveDirection.y * speed.value;  // Z is "forward" in top-down
        vel.value.y = 0.0f;  // No vertical movement

        // Apply velocity
        pos.value += vel.value * dt;

        // Face the aim direction (rotation is purely visual)
        if (glm::length(input.aimDirection) > 0.01f) {
            input.aimAngle = atan2(input.aimDirection.y, input.aimDirection.x);
        }
    }
}
```

### Optional: Dodge Roll

A staple of top-down shooters — brief invincibility + fast movement:

```cpp
struct DodgeRoll {
    float duration = 0.3f;
    float speed = 25.0f;
    float timer = 0.0f;
    float cooldown = 0.5f;
    float cooldownTimer = 0.0f;
    bool active = false;
    glm::vec2 direction;
};
```

---

## Phase 3: 2D Collision

**Goal**: Simplified collision for a top-down world.

### 2D AABB or Circle Colliders

Top-down games often use circles (simpler, no rotation issues):

```cpp
struct CircleCollider {
    float radius;
    uint16_t layer = 0x0001;    // Collision layer bitmask (Ch 10)
    uint16_t mask = 0xFFFF;     // What layers this collides with
};
```

Or keep 2D AABB from Chapter 9, just ignore the Y axis:

```cpp
struct AABB2D {
    glm::vec2 min;
    glm::vec2 max;
};

bool overlaps(const AABB2D& a, const AABB2D& b) {
    return a.min.x <= b.max.x && a.max.x >= b.min.x &&
           a.min.y <= b.max.y && a.max.y >= b.min.y;
}
```

### Spatial Hash (Reuse from Chapter 9)

The spatial hash works identically in 2D — just use (x, z) instead of (x, y, z). The broad-phase/narrow-phase pattern is the same.

### Wall Collision

Walls are axis-aligned rectangles (or tilemap cells marked as solid). Collision resolution is simpler than 3D — just push the player out of solid tiles along the nearest axis.

---

## Phase 4: Weapons & Combat

**Goal**: Top-down shooting mechanics.

### What Carries Over from Chapter 12

The weapon concepts are the same — hitscan and projectiles. The difference is 2D trajectory:

```cpp
// FPS: ray in 3D along camera forward
// Top-down: ray/projectile in 2D along aim direction

struct Projectile2D {
    glm::vec2 direction;
    float speed;
    float damage;
    float lifetime;
    float timer = 0.0f;
};
```

### Weapon Variety (Top-Down Style)

```cpp
enum class TopDownWeaponType {
    Pistol,         // Single accurate shot
    Shotgun,        // Fan of pellets (spread angle)
    MachineGun,     // Rapid fire, slight spread
    RocketLauncher, // Slow projectile, splash damage
    Flamethrower,   // Continuous cone of particles
    Laser,          // Instant hitscan beam (visible for a few frames)
};
```

### Spread Patterns

Top-down shooters often use visible bullet spread:

```cpp
// Shotgun: 5 pellets in a 30-degree arc
for (int i = 0; i < pelletCount; i++) {
    float angle = aimAngle + spreadAngle * ((float)i / (pelletCount - 1) - 0.5f);
    glm::vec2 dir(cos(angle), sin(angle));
    spawnProjectile(registry, origin, dir, damage);
}
```

### Splash Damage

Reuse Chapter 12's splash damage system directly — it already queries entities by distance.

---

## Phase 5: Level Design

**Goal**: Top-down levels (rooms, corridors, arenas).

### Option A: Tilemap

The simplest approach — a 2D grid where each cell is floor, wall, or special:

```cpp
struct Tilemap {
    int width, height;
    float tileSize = 1.0f;
    std::vector<uint8_t> tiles;  // 0 = floor, 1 = wall, 2 = pit, etc.

    uint8_t at(int x, int y) const {
        if (x < 0 || x >= width || y < 0 || y >= height) return 1; // walls
        return tiles[y * width + x];
    }

    bool isSolid(int x, int y) const { return at(x, y) == 1; }
};
```

Rendered as textured quads — one draw call per tile type (batched).

### Option B: TrenchBroom (from the TrenchBroom Roadmap)

Use TrenchBroom to build top-down levels with the camera locked overhead. This gives you more organic geometry than a tilemap — angled walls, irregular rooms, decorative detail.

The `.map` loader from the TrenchBroom roadmap works unchanged. You just view the level from above.

### Room-Based Progression (Hotline Miami Style)

```cpp
struct Room {
    AABB2D bounds;
    std::vector<SpawnPoint> enemySpawns;
    bool cleared = false;
    bool locked = false;          // Doors lock when combat starts
};

struct Floor {
    std::vector<Room> rooms;
    int currentRoom = 0;
};
```

When the player enters a room → lock doors → spawn enemies → unlock on clear. This reuses trigger volumes from Chapter 11.

---

## Phase 6: Enemy AI

**Goal**: Top-down enemy behaviours.

### Simpler Than FPS AI

Top-down AI is generally simpler because:
- Line of sight is a 2D raycast (cheaper)
- Pathfinding is on a 2D grid (A* from Ch 14 works directly)
- Enemies are often more about patterns than intelligence

### Enemy Types (Data-Driven, Same as Ch 14)

```cpp
// All using the same AIBrain component from Ch 14, different data:

// Chaser — runs straight at player
{ .detectRange = 15.0f, .attackRange = 1.5f, .moveSpeed = 5.0f, .melee = true }

// Shooter — keeps distance, fires projectiles
{ .detectRange = 20.0f, .attackRange = 12.0f, .moveSpeed = 3.0f, .melee = false,
  .preferredRange = 8.0f }

// Shotgunner — rushes to close range, fires spread
{ .detectRange = 15.0f, .attackRange = 5.0f, .moveSpeed = 6.0f, .melee = false }

// Bomber — runs at player and explodes
{ .detectRange = 20.0f, .attackRange = 1.0f, .moveSpeed = 7.0f, .melee = true,
  .explodeOnDeath = true }

// Turret — stationary, rotates to track player
{ .detectRange = 25.0f, .attackRange = 25.0f, .moveSpeed = 0.0f, .melee = false,
  .stationary = true }
```

### Wave Spawning

A core mechanic in top-down shooters:

```cpp
struct WaveSpawner {
    struct Wave {
        std::vector<std::pair<std::string, int>> enemies;  // {"grunt", 5}, {"bomber", 2}
        float spawnDelay = 0.5f;    // Time between individual spawns
    };

    std::vector<Wave> waves;
    int currentWave = 0;
    float timer = 0.0f;
    bool active = false;
};
```

---

## Phase 7: Top-Down Specific Features

Things that don't exist in the FPS but are expected in top-down shooters.

### 7.1 Screen Shake (Already Done — Ch 20)

The screen shake from Chapter 20 works perfectly. Top-down games use it heavily.

### 7.2 Combo / Score System

```cpp
struct ScoreTracker {
    int score = 0;
    int combo = 0;
    float comboTimer = 0.0f;
    float comboWindow = 2.0f;    // Seconds before combo resets
    float multiplier() const { return 1.0f + combo * 0.25f; }
};
```

Kill an enemy → increment combo, reset timer. Timer expires → combo resets. Score = base points * multiplier.

### 7.3 Procedural Levels (Optional)

Top-down shooters often use procedural generation:

```
1. Place rooms as rectangles on a grid
2. Connect adjacent rooms with corridors
3. Fill rooms with enemies based on difficulty curve
4. Place items/weapons in reward rooms
```

This is a big feature but natural for the tilemap approach — you're just filling a 2D array algorithmically instead of by hand.

### 7.4 2D Lighting & Shadows (Optional)

Cast shadows from walls using 2D raycasting:
- Cast rays from the player (or light source) outward
- Find where they hit walls
- The lit area is a polygon rendered as a bright overlay
- Everything else is darkened

This creates a "fog of war" / flashlight effect common in top-down games.

---

## Implementation Order

```
Phase 1: Camera & Rendering      ← See the world from above
Phase 2: Input & Movement         ← Move and aim with twin-stick
Phase 3: 2D Collision              ← Bump into walls
Phase 4: Weapons & Combat         ← Shoot things
Phase 5: Level Design             ← Rooms to fight in
Phase 6: Enemy AI                  ← Things to shoot at
Phase 7: Genre-Specific Polish    ← Score, waves, screen shake
```

### Estimated Changes by File Count

| Category | New Files | Modified Files | Deleted Files |
|----------|-----------|---------------|---------------|
| Camera | 1 | 0 | 0 (keep FPS camera for potential reuse) |
| Input | 1 | 0 | 0 |
| Movement | 1 | 0 | 0 (FPS movement stays, just unused) |
| Collision | 1 (2D variants) | 1 (spatial hash) | 0 |
| Weapons | 1 | 0 | 0 |
| Levels | 1-2 (tilemap or reuse .map) | 0 | 0 |
| AI | 0 (reuse Ch 14) | 1 (2D LoS) | 0 |
| Rendering | 1 (sprite batcher) | 0 | 0 |

Total: ~7-8 new files, ~2-3 modified. The existing engine code is untouched.

---

## The ECS Lesson

Notice how this entire genre switch requires:
- **Zero changes** to the registry, entity management, or system architecture
- **New components** (TopDownInput, Sprite, CircleCollider, ScoreTracker, WaveSpawner)
- **New systems** (topDownMovementSystem, spriteRenderSystem, waveSpawnSystem)
- **Reused systems** (audio, particles, networking, spatial hash, A* pathfinding)

The engine doesn't know or care that the game changed genre. It just runs systems on entities that have certain components. That's ECS working as intended.
