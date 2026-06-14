# QEngine — Scene Setup Reference

The showcase scene is a single 30x30x6 room containing all gameplay elements introduced through the tutorials. Defined in `scene_setup.cpp` (entity spawning) and `showcase_level.cpp` (level geometry).

---

## Level Geometry

A single sector: origin `(0,0,0)` to `(30,6,30)`.

6 surfaces: floor, ceiling, 4 walls. All textured with `grid_grey.png`. Each surface becomes a static Jolt body via `createLevelBodies`.

---

## Entities

### Player
| Component | Value |
|-----------|-------|
| `Position` | `(15, 1.7, 15)` — centre of room |
| `Velocity` | `(0, 0, 0)` |
| `AABBCollider` | halfExtents `(0.3, 0.85, 0.3)` |
| `OnGround` | false |
| `CharacterPhysics` | defaults |
| `Health` | 100/100 |
| `PlayerInput` | defaults |
| `TagPlayer` | — |
| `WeaponInventory` | Shotgun (slot 0), Rocket Launcher (slot 1) |
| `Ammo` | 25 shells, 5 rockets |

### Lighting

**Directional light (sun):**
- Direction: `(-0.2, -1, -0.3)`, white, ambient 0.08

**Point lights:**
| Light | Position | Colour | Range |
|-------|----------|--------|-------|
| Ceiling 1 | `(15, 5.5, 10)` | Bright white (2,2,2) | Wide |
| Ceiling 2 | `(15, 5.5, 20)` | Bright white (2,2,2) | Wide |
| Red torch | `(3, 2, 10)` | Red (3,0.2,0.2) | Tight pool |
| Green torch | `(3, 2, 15)` | Green (0.2,3,0.2) | Tight pool |
| Blue torch | `(3, 2, 20)` | Blue (0.2,0.2,3) | Tight pool |

Each point light has a small grey debug cube at its position.

### Physics Demos

**Shelf (blue box):**
- Position: `(20, 1, 5)`, Scale: `(4, 2, 4)`, halfExtents: `(2, 1, 2)`
- Static Jolt body (`createStaticBody`)

**Cube 1 (orange, shelf demo):**
- Start: `(20.5, 4, 5)`, Velocity: `(-6, 0, 0)`
- Falls onto shelf, slides off edge to floor
- Resets every 6 seconds

**Cube 2 (orange, gravity demo):**
- Start: `(20, 5, 8)`, Velocity: `(0, 0, 0)`
- Pure gravity drop from near ceiling
- Resets every 4 seconds

**Cube 3 (orange, friction demo):**
- Start: `(20, 0.5, 12)`, Velocity: `(3, 0, 1)`
- Slides across floor with low friction (1.5)
- Resets every 5 seconds

All cubes are dynamic Jolt bodies (`createDynamicBody`).

### Door

- Closed: `(25, 1.5, 15)`, Open: `(25, 4.5, 15)`
- Scale: `(0.2, 3, 4)`, halfExtents: `(0.1, 1.5, 2)`
- Speed: 3 u/s, wait: 4s, no start delay
- Kinematic Jolt body
- Trigger zone: halfExtents `(2, 1.5, 2.5)` centred on door

### Lift

- Bottom: `(10, 0.2, 25)`, Top: `(10, 4.2, 25)`
- Scale: `(3, 0.2, 3)`, halfExtents: `(1.5, 0.1, 1.5)`
- Speed: 2 u/s, wait: 2s, start delay: 2s
- Kinematic Jolt body
- Bottom y=0.2 so the body (bottom at y=0.1) clears the floor body (top at y=0.1)
- Trigger zone: position `(10, 0.5, 25)`, halfExtents `(1.5, 0.3, 1.5)`

### Teleporter

- Position: `(5, 0.5, 5)`
- Destination: `(25, 1, 25)` (far corner)
- halfExtents: `(1, 1.5, 1)`, cooldown: 1s
- Blue pole marker at `(5, 1.5, 5)`

### Lava Pool

- Surface visual: `(20, 0.1, 25)`, Scale: `(6, 0.2, 6)`, red
- Damage trigger: `(20, 0.5, 25)`, halfExtents: `(3, 0.5, 3)`
- 25 damage/second, no cooldown

### Debug Wireframes

Green wireframe cubes mark trigger volumes for: door, lift, teleporter, lava. Tagged with `TagDebugWireframe` — `renderSystem` draws them in `GL_LINE` mode.

---

## Combat Resources (Registry Context)

```cpp
CombatResources {
    cubeVAO            = cube mesh VAO
    cubeIndexCount     = cube mesh index count
    shaderId           = lit shader
    projectileTextureId = grid_red (rockets)
    tracerTextureId     = grid_orange (hitscan tracers)
}
```
