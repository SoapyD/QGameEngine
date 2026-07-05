# TrenchBroom — What the Test Map Needs to Contain

**Purpose:** tells you exactly what to build in TrenchBroom to exercise the `.map`
loader as it comes online (parser → mesh → entities → collision → wire-up, steps
2.1–2.5 of [`../plans/2026-07-03_trenchbroom_engine-loader.md`](../plans/2026-07-03_trenchbroom_engine-loader.md)).
You don't need the full showcase to start — a tiny room drives the whole pipeline.

**Prereq:** the QEngine game config from [`../../tb/`](../../tb/) is installed — see
[`../plans/2026-07-03_trenchbroom_install.md`](../plans/2026-07-03_trenchbroom_install.md).

> **Just want click-by-click steps?** See the hands-on walkthrough
> [`TRENCHBROOM_WALKTHROUGH.md`](TRENCHBROOM_WALKTHROUGH.md) (tuned to v2026.1). This doc
> is the *what/why*; that one is the *how*.

---

## TL;DR — the smallest map that tests the process

A **hollow room + a player start + a light**. That's it. Save it to
`assets/maps/smoke.map`. It touches every loader stage:

| What you place | Loader step it exercises | Why it's needed |
|----------------|--------------------------|-----------------|
| A **hollow box** (6 world brushes) | 2.1 parse · 2.2 brush→mesh · 2.4 collision | Gives geometry to render *and* a floor to stand on. Without a solid brush there's nothing to test past the parser. |
| `info_player_start` | 2.3 entity mapping | Without it the player never spawns — the map "loads" but you fall through nothing. This is the one **mandatory** entity. |
| One `light` | 2.3 · rendering | Proves point-entity props (`_color`, attenuation) reach the `PointLight` factory, and you can see the room. |

If those three load, render, and you spawn standing on the floor, the core
pipeline works. Everything else is breadth.

---

## Tier 2 — add these once the smoke map loads

These extend coverage to the interactive/brush-entity paths. Add them to a second
map (`assets/maps/showcase.map`) — don't overload the smoke test.

| What you place | Loader step | What it proves |
|----------------|-------------|----------------|
| A `func_door` **+** a `trigger_multiple` whose **`target`** = the door's **`targetname`** | 2.3 two-pass linking · 2.4 kinematic collision · movers | The single trickiest path: brush entities become moving colliders, and trigger→mover linking survives the load (targetname resolved to an entity in a second pass). |
| A `func_plat` (lift) + its trigger | same | Vertical mover + "carry the player" physics on loaded geometry. |
| `trigger_teleport` + `info_teleport_destination` (linked by name) | 2.3 linking | Name-linking to a *point* entity (the destination), not a mover. |
| `trigger_hurt` with `dmg` | 2.3 | Damage-volume prop mapping. |
| An `item_health` and a `weapon_nailgun` | 2.3 | Point-entity item/weapon factories from map data. |
| A `monster_grunt` | 2.3 | Enemy factory + it needs floor/nav to path on. |
| An **angled** (non-axis-aligned) brush | 2.2 | Forces real plane∩plane∩plane math, not just axis-aligned boxes. Rotate a brush ~30°. |

The full element→class→property mapping (to reproduce the current hard-coded
showcase exactly) is in [`../../assets/maps/README.md`](../../assets/maps/README.md)
and mirrors [`src/engine/level/showcase_descriptor.cpp`](../../src/engine/level/showcase_descriptor.cpp).

---

## Critical settings — get these wrong and the loader breaks silently

1. **Map format = `Standard`.** Our parser reads Standard face lines
   (`( x y z ) ( x y z ) ( x y z ) TEX offX offY rot sclX sclY`). TrenchBroom's
   **Valve** format adds texture-axis brackets and the parser will reject it.
   - New Map dialog → pick **Standard** (the QEngine config lists only Standard, so
     it should be the sole/default option).
   - Verify: open the saved `.map` in a text editor; a face line should have exactly
     **5 numbers after the texture name**, no `[ ... ]` brackets.

2. **Texture names must match `assets/textures/`, no path, no extension.** Available:
   `grid_grey`, `grid_blue`, `grid_green`, `grid_orange`, `grid_red`, `wall`. In the
   `.map` these appear as bare names (`grid_grey`), which is what the loader resolves
   against `ResourceManager`. Untextured faces default to a placeholder — texture at
   least the floor so you can tell up from down.

3. **One `info_player_start`, placed inside the room, above the floor.** Multiple
   spawns or a spawn embedded in a brush will misbehave once 2.3 lands.

4. **Keep entity keys as-authored by the FGD.** The FGD ([`../../tb/QEngine.fgd`](../../tb/QEngine.fgd))
   exposes exactly the keys the factories read (`endpos`, `speed`, `targetname`,
   `_color`, `dmg`, `amount`, …). Don't hand-add keys the FGD doesn't list — the
   loader ignores unknown ones.

---

## Coordinate & scale — what to expect (you don't have to compensate)

- **TrenchBroom is Z-up; QEngine is Y-up.** The loader converts axes on import
  (step 2.4). Build the room "flat on the grid" as TrenchBroom normally does — the
  floor at low Z — and it'll come in with the floor at low Y. Don't pre-rotate.
- **Units:** TrenchBroom works in large integer Quake units; the engine's showcase
  room is ~30 engine units across. The loader bakes a unit ratio (planned **1 engine
  unit = 32 map units**), so build at Quake scale — e.g. a **~512-unit** room ≈ 16
  engine units, a **~1024-unit** room ≈ 32. Grid size 16–64 is comfortable.
- The exact ratio is finalized in the loader (2.4); if the first load feels too
  big/small we tune it there, not in the editor.

---

## How we'll test it (engine side)

- Until 2.5 wires `buildWorld` to a file, the parser is covered by the `map_parse`
  headless scenario (embedded fixture): `build/QEngineHeadless.exe map_parse`.
- When 2.5 lands, we'll add a headless scenario that **loads your `smoke.map`** and
  asserts the player spawns on the ground (and, for `showcase.map`, that the door
  opens) — the regression guard before the hard-coded showcase is deleted.
- Drop your saved files in `assets/maps/`. Even before the loader is done, a real
  editor-saved `.map` is the best test input for the parser — hand-written fixtures
  can miss quirks TrenchBroom actually emits.

---

## Checklist

- [ ] QEngine game config installed; **New Map → QEngine** lists our textures + entities.
- [ ] `smoke.map`: hollow room + `info_player_start` + one `light`, floor textured, format **Standard**, saved to `assets/maps/`.
- [ ] Opened the `.map` in a text editor and confirmed **5 numbers after each texture name** (Standard, not Valve).
- [ ] (Tier 2) `showcase.map`: adds a `func_door` + linked `trigger_multiple`, a lift, teleporter, hazard, a couple of items, a grunt, and one angled brush.
