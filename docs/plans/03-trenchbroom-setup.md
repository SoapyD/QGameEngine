# Plan 03 — Getting the Map-Building Tool (TrenchBroom) Running

**Scope:** What it takes to author levels in **TrenchBroom** instead of hard-coding them,
and specifically how to rebuild the current showcase room as an editor-authored `.map`.

> **Reality check first.** There is **no working map tool in the project today.**
> The `.qlvl` text format + `LevelLoader` exist but the game never uses them (the showcase
> is built in C++ by `createShowcaseLevel()` + `scene_setup.cpp`), and `assets/levels/test.qlvl`
> references textures that don't even ship. TrenchBroom is the *planned* tool — it's fully
> designed in [`roadmap/ROADMAP_TRENCHBROOM.md`](../roadmap/ROADMAP_TRENCHBROOM.md) (Ch 17–20,
> "Written") but **none of the engine-side `.map` support is implemented.** So "getting it up
> and running" is a **build project**, not just an install. This plan splits it into
> (1) install/config you can do today, and (2) the engine code that has to land for a map to
> actually load.

---

## Part 1 — Install & configure TrenchBroom (can be done now)

### 1.1 Install
1. Download from **https://trenchbroom.github.io/** (Win/Mac/Linux). Current series is 2.x.
2. Install and launch. On first run it asks for a game — there's none for QEngine yet, so
   you'll add one in 1.3.

### 1.2 Create the integration folder in the repo
Add a `tb/` directory (new) holding the editor config, plus a textures/maps layout TrenchBroom
can read:

```
QEngine/
├── tb/
│   ├── GameConfig.cfg          ← tells TrenchBroom about QEngine
│   ├── QEngine.fgd             ← entity definitions (what you can place)
│   └── Icon.png                ← optional toolbar icon
├── assets/
│   ├── textures/               ← already exists (grid_*.png, wall.png)
│   └── maps/                   ← NEW — TrenchBroom .map sources live here
│       └── showcase.map
```

### 1.3 `GameConfig.cfg`
Use the template in [ROADMAP_TRENCHBROOM.md §Phase 4](../roadmap/ROADMAP_TRENCHBROOM.md).
Key fields for *our* assets:

```jsonc
{
    "version": 9,
    "name": "QEngine",
    "icon": "Icon.png",
    "fileformats": [ { "format": "Standard" } ],
    "filesystem": { "searchpath": "assets" },
    "textures": {
        "root": "textures",
        "extensions": [ ".png" ],
        "attribute": "_tb_textures"
    },
    "entities": {
        "definitions": [ "QEngine.fgd" ],
        "defaultcolor": "0.6 0.6 0.6 1.0"
    },
    "tags": {
        "brush": [
            { "name": "Trigger", "attribs": ["transparent"],
              "match": "classname", "pattern": "trigger_*" }
        ]
    }
}
```

### 1.4 Install the config
- **Windows:** copy `tb/` contents into `%APPDATA%\TrenchBroom\games\QEngine\`
  (or use *Preferences → Game → "QEngine" → set game path* to the repo root).
- **Linux/Mac:** `~/.TrenchBroom/games/QEngine/`.
- Restart TrenchBroom → **New Map → QEngine** should now appear, showing our textures and
  the FGD entities.

> At this point you can *draw* a level and *save* a `.map`. The engine still can't load it —
> that's Part 2.

---

## Part 2 — Engine work required to load a `.map` (the real effort)

This is Phase 5, Chapters 17–20. Ordered by dependency; each step leaves the build runnable.

| Step | Deliverable | Replaces / adds | Ref |
|------|-------------|-----------------|-----|
| **2.0** ✅ | **Entity-factory refactor** — DONE (2026-07-02). `classname`→factory dispatch + `SpawnParams` + two-pass `targetname` linking; showcase now built from a descriptor list. The parser (2.1) just emits `SpawnParams`; entity mapping (2.3) is registering FGD classnames against the existing dispatch table. See [2026-07-02-entity-factory-classname-dispatch.md](2026-07-02-entity-factory-classname-dispatch.md). | Inline spawns in `scene_setup.cpp` | Plan 02 §C #1 |
| **2.1** | **`.map` parser** — read entities → brushes → planes (3 points + texture + UV). | New `MapLoader`; `.qlvl` path retired | Ch 17 |
| **2.2** | **Brush → mesh** — plane-plane-plane intersection (Cramer's rule), Sutherland-Hodgman face clipping, fan-triangulate, normals from planes, axial UV projection. | `buildSectorMeshes` analogue for brushes | Ch 17 |
| **2.3** | **FGD + entity mapping** — `classname` → factory (`info_player_start`, `light`, `func_door`, `trigger_*`, `item_*`, …); `target`/`targetname` linking for triggers→movers. | Hooks 2.0 factories to map data | Ch 18 |
| **2.4** | **Brush collision** — convex-hull (or AABB) Jolt static/kinematic/sensor bodies from brush planes; coordinate conversion. | `createLevelBodies` analogue | Ch 19 |
| **2.5** | **Wire `buildWorld` to load a `.map`** instead of `createShowcaseLevel()`; texture-name resolution; (optional) hot-reload. | `simulation.cpp::buildWorld` | Ch 20 |

### Coordinate-system gotchas (call these out early — they bite everyone)
- **Quake/TrenchBroom is Z-up; QEngine is Y-up.** The loader must swap/convert axes
  (`(x, y, z)_TB → (x, z, -y)_engine` or similar — pick one and apply it consistently to
  geometry, entity origins, *and* `angle`).
- **Scale/units.** TrenchBroom works in integer Quake units (the showcase room is 30 *engine*
  units ≈ small). Decide a unit ratio (e.g. 1 engine unit = 32 map units) and bake it into the
  loader so brush sizes feel right. Bunny-hop/step-height tuning assumes the current scale.
- **Convex-radius fattening** — `createLevelBodies` already fattens thin surfaces ±0.1 to clear
  Jolt's 0.05 convex radius; the brush collider needs the equivalent guard.

---

## Part 3 — Rebuilding the current showcase as a `.map`

This is the concrete "what objects/options need to change" answer. Map each hard-coded
showcase element to a TrenchBroom construct + FGD entity.

### 3.1 FGD entries needed (`tb/QEngine.fgd`)
The showcase uses these archetypes, so the FGD must define at least:

| Showcase element (today, hard-coded) | TrenchBroom entity | Type | Key properties |
|--------------------------------------|--------------------|------|----------------|
| Room walls/floor/ceiling | `worldspawn` brushes | brush | texture per face |
| Player at (15,1.7,15) | `info_player_start` | point | `angle` |
| Directional sun | `light_environment` (or a worldspawn key) | point | `_color`, `angle/pitch` |
| Point lights (ceiling/torches) | `light` | point | `light` (brightness), `_color`, attenuation |
| Shelf (static box) | `func_static` (or a worldspawn brush) | brush | texture |
| Demo cubes | `prop_dynamic` / `func_physics` (**new** classname) | point/brush | spawns `DemoReset` cube |
| Door (25,1.5,15 → 4.5) | `func_door` | brush | `speed`, `wait`, `lip`, `angle`, `targetname` |
| Lift (10,0.2,25 → 4.2) | `func_plat`/`func_door` (vertical) | brush | `speed`, `wait`, `startdelay`, `targetname` |
| Door/lift trigger zones | `trigger_multiple` | brush | `target` → door/lift `targetname` |
| Teleporter (5→25,1,25) | `trigger_teleport` + `info_teleport_destination` | brush+point | `target` / `targetname` |
| Lava pool (damage zone) | `trigger_hurt` | brush | `dmg` (per second) |
| Pickups (Plan 02) | `item_health`, `item_shells`, `item_rockets`, `weapon_*` | point | `amount` |
| Enemy (Plan 02) | `monster_grunt` | point | `angle` |

The roadmap already drafts most of these — extend its FGD with `light_environment`,
`func_plat`, `trigger_teleport`/`info_teleport_destination`, `trigger_hurt`, and the
`prop_dynamic` demo cube.

### 3.2 Property-mapping changes the engine needs
For the showcase to behave identically, the entity factories (step 2.3) must translate FGD
properties into the existing components:

- `func_door`/`func_plat` → `Mover{ startPos, endPos (from angle+lip/height), speed, waitTime,
  startDelay, requiresTrigger }` + `createKinematicBody`.
- `trigger_multiple target X` → `TriggerVolume{ action=ActivateMover, target=entity(X) }` —
  needs a **two-pass load** (spawn all entities, then resolve `target`→`targetname` to `entt::entity`).
- `trigger_teleport` → `TriggerVolume{ action=Teleport, destination=info_teleport_destination.origin }`.
- `trigger_hurt dmg N` → `TriggerVolume{ action=Damage, value=N }`.
- `light` brightness/color → `PointLight` attenuation/`color` (define a brightness→linear/quadratic curve).
- `info_player_start` → existing player spawn (`SpawnPoint` + player archetype).

### 3.3 Build-the-room steps in TrenchBroom (once Part 2 lands)
1. New Map (QEngine game). Draw a box, **Make Hollow** (thickness ~16) → 6 wall brushes.
2. Texture faces with `grid_grey` (walls) / pick others per zone.
3. Place `info_player_start` in the centre, set `angle`.
4. Add `light` entities for the ceiling/torches; set `_color`.
5. Draw a brush for the shelf; make it `func_static`.
6. Draw the door brush → tie to `func_door`; draw a `trigger_multiple` in front, set its
   `target` to the door's `targetname`.
7. Draw the lift brush → `func_plat`; add its trigger.
8. Add `trigger_teleport` + `info_teleport_destination`; add `trigger_hurt` over the lava area.
9. Save as `assets/maps/showcase.map`.
10. Point `buildWorld` at it (step 2.5) and run.

---

## Part 4 — Recommended sequencing

```
Now (no engine code):     Part 1 (install + tb/ config + FGD skeleton). Draw a throwaway room, save a .map — you have a real sample file to parse.
Prereq:                   Plan 02 #1 entity-factory refactor (step 2.0).
Then Phase 5 in order:    2.1 parser → 2.2 brush mesh → 2.3 entity mapping → 2.4 brush collision → 2.5 wire-up.
Regression safety:        extend the headless harness (Plan 01 Tier-2 #7) with a "load showcase.map, assert player spawns on ground / door opens" scenario before deleting the hard-coded showcase.
```

**Bottom line:** the *install* is an afternoon; the *enable* is Phase 5 (≈Chapters 17–20).
The single most useful thing to do immediately is Part 1 + the entity-factory refactor, which
gives you a real `.map` to develop the parser against and unblocks both this plan and Plan 02.
