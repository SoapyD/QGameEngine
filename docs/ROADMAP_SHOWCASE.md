# QEngine — Showcase Tutorial Roadmap

An evolving "dev room" that grows with each cleanup chapter. After every code cleanup pass, a new showcase tutorial updates the dev room to demonstrate all features built so far.

Showcases are cumulative — each one replaces the previous dev room with a larger, more feature-rich version.

---

## What Is a Dev Showcase Room?

A common practice in game development: a dedicated test level designed to display and test engine features in isolation. Think of it like a museum gallery for your code — each exhibit demonstrates one capability.

The room uses **developer grid textures** (the classic orange/grey checkerboard with distance markings) so you can judge scale, alignment, and movement without being distracted by art.

---

## Showcase Schedule

| Showcase | After Cleanup | Features Demonstrated | Status |
|----------|--------------|----------------------|--------|
| 1 | 10a | Rendering, lighting, level geometry, collision, physics | Pending |
| 2 | 15a | Doors, lifts, triggers, weapons, items, enemies, HUD | Pending |
| 3 | 20a | Audio, particles, screen effects, polish | Pending |

Showcases beyond #3 will be planned once we reach those phases. The dev room evolves — it is never rewritten from scratch, only expanded.

---

## Showcase 1: Foundation & Physics (after Ch 10a)

### What It Demonstrates

| Feature | Source Chapter | Exhibit |
|---------|--------------|---------|
| Textured rendering | Ch 5 | Walls and floor with grid texture, one wall with a different texture |
| Mesh loading | Ch 6 | OBJ-loaded cubes placed around the room |
| Directional lighting | Ch 7 | Sun-like light casting across the room |
| Point lighting | Ch 7 | Coloured point lights illuminating specific areas |
| Level geometry | Ch 8 | Multi-room layout with connected sectors |
| AABB collision | Ch 9 | Solid walls and objects that block movement |
| Multiple point lights | Ch 7 + 10a | Multiple coloured lights rendering simultaneously |
| Gravity & ground detection | Ch 10 | Objects falling and landing on surfaces |
| Friction | Ch 10 | Objects sliding and coming to a stop |
| Demo reset loop | Showcase 1 | Physics demos loop on a timer for repeated viewing |

### Room Layout

```
┌────────────────────────────┬──────────────────┐
│                            │                  │
│   MAIN HALL                │   PHYSICS LAB    │
│                            │                  │
│   - Grid textured walls    │   - Falling cubes│
│   - Sun light              │   - Ramps/shelves│
│   - Point light exhibits   │   - Friction demo│
│                            │                  │
│                            │                  │
├──────────────┐             │                  │
│              │             └──────────────────┘
│  LIGHT ROOM  │
│              │
│  - RGBW      │
│    point     │
│    lights    │
│  - Low       │
│    ambient   │
│              │
└──────────────┘
```

### Resources Needed

| Resource | Type | Notes |
|----------|------|-------|
| `grid_orange.png` | Texture (512x512) | Orange/grey checkerboard with 1m grid lines. Standard dev texture. |
| `grid_grey.png` | Texture (512x512) | Neutral grey grid. Used for floors and ceilings. |
| `grid_blue.png` | Texture (512x512) | Blue-tinted grid. Used to differentiate rooms. |
| `cube.obj` | Mesh | Already exists in project — reuse it. |

**Where to get grid textures:**
- **Kenney.nl** — free "Prototype Textures" pack (CC0 licence, no attribution required)
- **Alternatively**: create a simple 512x512 checkerboard in any image editor with grid lines every 64px

### Level Design Notes

- Main hall: approximately 20m x 12m, 4m ceiling height
- Physics lab: approximately 10m x 12m, connected to main hall via open doorway
- Light room: approximately 8m x 8m, low ambient, connected to main hall
- All rooms use grid textures so scale is immediately obvious
- Place 3 cubes in the physics lab with DemoReset components (shelf fall, ceiling drop, friction slide)
- Place 4 point lights in the light room (red, green, blue, white) to show how lighting blends

---

## Showcase 2: Gameplay (after Ch 15a)

### What It Adds

| Feature | Source Chapter | Exhibit |
|---------|--------------|---------|
| Doors | Ch 11 | Sliding door between main hall and a new arena room |
| Lifts | Ch 11 | Platform that carries player to upper level |
| Triggers | Ch 11 | Pressure plate that opens a door or activates a light |
| Weapons | Ch 12 | Weapon pickup + shooting range with targets |
| Pickups | Ch 13 | Health, ammo, and armour pickups scattered around |
| Enemy AI | Ch 14 | Basic enemy in an arena enclosure |
| HUD | Ch 15 | Health bar, ammo counter, crosshair visible throughout |

### Resources Needed

Resources from Showcase 1, plus:

| Resource | Type | Notes |
|----------|------|-------|
| `grid_red.png` | Texture (512x512) | Red-tinted grid for arena/danger areas |
| `grid_green.png` | Texture (512x512) | Green-tinted grid for pickup/safe areas |
| Target mesh or simple geometry | Mesh | For shooting range targets |
| Pickup item meshes or coloured cubes | Mesh | Cubes with colour tints work fine |

---

## Showcase 3: Polish (after Ch 20a)

### What It Adds

| Feature | Source Chapter | Exhibit |
|---------|--------------|---------|
| Audio | Ch 16 | Positional audio sources (ambient hum, footsteps) |
| Particles | Ch 20 | Torch flame particles, impact sparks |
| Screen shake | Ch 20 | Triggered by weapon fire or explosion |
| View bob | Ch 20 | Active while walking |

### Resources Needed

Resources from Showcases 1+2, plus:

| Resource | Type | Notes |
|----------|------|-------|
| Sound effects | Audio | Ambient hum, gunshot, footstep (free packs from freesound.org or kenney.nl) |
| No new textures required | — | Particle system uses programmatic quads |

---

## Directory Structure

```
D:\Documents\AI\documents\Game Learning\
└── Game_Showcase_Tutorials\
    ├── Showcase_01_Foundation_And_Physics.md
    ├── Showcase_02_Gameplay.md          (future)
    └── Showcase_03_Polish.md            (future)
```

---

## Design Principles

1. **Cumulative, not disposable.** Each showcase builds on the previous one. The main hall from Showcase 1 is still there in Showcase 3, just with more features added.

2. **Grid textures for clarity.** Dev rooms are not art showcases. Grid textures make scale, alignment, and physics visible at a glance.

3. **One exhibit per feature.** Each engine feature gets a clear, isolated demonstration. Don't combine features in a way that makes it hard to tell what's working.

4. **Minimal external assets.** Use existing meshes (cube.obj), simple textures (grids), and basic geometry. The showcase should be buildable with what you already have, plus a few free texture downloads.

5. **Playable, not just viewable.** The showcase should be something you can walk around in, interact with, and use to verify that everything works together.
