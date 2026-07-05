# assets/maps

TrenchBroom `.map` source files live here.

The engine **cannot load these yet** — the `.map` loader is the second half of the
TrenchBroom work (see [`docs/plans/2026-07-03_trenchbroom_engine-loader.md`](../../docs/plans/2026-07-03_trenchbroom_engine-loader.md),
steps 2.1–2.5). The point of authoring a map now is to produce a **real sample file**
to develop the parser against.

## Getting a sample map

1. Install TrenchBroom (https://trenchbroom.github.io/) and register the QEngine game
   config from [`tb/`](../../tb/) — see
   [`docs/plans/2026-07-03_trenchbroom_install.md`](../../docs/plans/2026-07-03_trenchbroom_install.md).
2. **New Map → QEngine**, draw a box, *Edit → Make Hollow*, texture the faces with
   `grid_grey`, drop an `info_player_start` and a `light`, and **Save As** here.
3. A throwaway room is enough to unblock the parser. The full goal is `showcase.map`
   below.

**What exactly to place, and the settings that silently break loading (format, texture
names, scale):** see [`docs/roadmap/TRENCHBROOM_TEST_MAP.md`](../../docs/roadmap/TRENCHBROOM_TEST_MAP.md).

## Textures available (assets/textures)

`grid_grey`, `grid_blue`, `grid_green`, `grid_orange`, `grid_red`, `wall`
(all `.png`; reference them by name without the extension).

## Rebuilding `showcase.map`

The hard-coded showcase (`src/engine/level/showcase_descriptor.cpp`) is the target
to reproduce as an authored map. Every entity below has a matching class in
[`tb/QEngine.fgd`](../../tb/QEngine.fgd); the property keys are identical to the
descriptor so the loader maps them 1:1.

| Showcase element | TrenchBroom class | Key properties |
|------------------|-------------------|----------------|
| Player @ (15, 1.7, 15) | `info_player_start` | `angle` |
| Sun | `light_environment` | `direction`, `_color`, `ambient` |
| Ceiling / torch lights | `light` | `_color`, `ambient`, `linear`, `quadratic` |
| Shelf | `func_static` | face textures |
| Demo cubes | `prop_dynamic` | `velocity`, `interval` |
| Door → `door1` | `func_door` | `endpos`, `speed`, `wait`, `startdelay`, `targetname` |
| Lift → `lift1` | `func_plat` | `endpos`, `speed`, `wait`, `startdelay`, `targetname` |
| Door / lift trigger zones | `trigger_multiple` | `target`, `cooldown` |
| Teleporter + destination | `trigger_teleport` + `info_teleport_destination` | `target` / `targetname` |
| Lava surface + damage | `func_decor` + `trigger_hurt` | `dmg`, `cooldown` |
| Pickups | `item_*` / `weapon_*` | `amount` |
| Enemies | `monster_grunt` | `angle` |

> Coordinate note: TrenchBroom is Z-up / integer Quake units; the engine is Y-up /
> small float units. The loader (step 2.4) converts axes and scale on import — decide
> the unit ratio there, not in the editor.
