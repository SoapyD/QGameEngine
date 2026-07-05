# Plan — TrenchBroom: Engine `.map` Loader

**Group:** `trenchbroom` (part 2 of 2 — **depends on**
[2026-07-03_trenchbroom_install.md](2026-07-03_trenchbroom_install.md) for a sample `.map`).
**Graduated from** [archive/2026-06-08_trenchbroom-setup.md](archive/2026-06-08_trenchbroom-setup.md) Parts 2–3.

**Goal:** load a TrenchBroom `.map` into the running engine, replacing the hard-coded
`createShowcaseLevel()`. This is the real build effort (Phase 5, Ch 17–20).

---

## Prerequisites
- ✅ **Entity-factory refactor (step 2.0)** — DONE (2026-07-02): `classname`→factory dispatch +
  `SpawnParams` + two-pass `targetname` linking; showcase already built from a descriptor list.
  See [archive/2026-07-02_entity-factory-classname-dispatch.md](archive/2026-07-02_entity-factory-classname-dispatch.md).
- A sample `.map` saved from the install plan.

## Steps (ordered by dependency; each leaves the build runnable)
| Step | Deliverable | Adds/replaces | Ch |
|------|-------------|---------------|----|
| **2.1** ✅ | **`.map` parser** — DONE (2026-07-04). Standard-format text→struct front end: [`map_loader.{h,cpp}`](../../src/engine/level/map_loader.cpp) (`qmap::parseMapString`/`loadMapFile`) → [`types/map_data.h`](../../src/engine/level/types/map_data.h) (`MapData` → `MapEntity` → `MapBrush` → `MapFace`). Tokenizer strips `//` comments; recursive descent parses entities/brushes/faces with line-numbered errors on malformed input. **No** coordinate/scale conversion or spawning yet (that's 2.2–2.4). Covered by the `map_parse` headless scenario. `.qlvl` path still dead, not yet deleted. | new `MapLoader`; retire the unused `.qlvl` path | 17 |
| **2.2** | **Brush → mesh** — plane∩plane∩plane (Cramer), Sutherland-Hodgman face clipping, fan-triangulate, plane normals, axial UV | `buildSectorMeshes` analogue for brushes | 17 |
| **2.3** | **FGD + entity mapping** — `classname` → existing factories; `target`/`targetname` two-pass linking | hooks the 2.0 dispatch table to map data | 18 |
| **2.4** | **Brush collision** — convex-hull (or AABB) Jolt static/kinematic/sensor bodies from brush planes | `createLevelBodies` analogue | 19 |
| **2.5** | **Wire `buildWorld` to a `.map`** instead of `createShowcaseLevel()`; texture-name resolution; optional hot-reload | `simulation.cpp::buildWorld` | 20 |

## Coordinate-system gotchas (call out early)
- **Z-up (Quake/TB) vs Y-up (QEngine)** — convert axes consistently across geometry, entity
  origins, *and* `angle`.
- **Scale** — TB uses integer Quake units; pick a ratio (e.g. 1 engine unit = 32 map units) and
  bake it in. Bunny-hop/step-height tuning assumes the current scale.
- **Convex-radius fattening** — `createLevelBodies` fattens thin surfaces ±0.1 to clear Jolt's
  0.05 convex radius; the brush collider needs the same guard.

## Property mapping (entity factories in 2.3)
`func_door`/`func_plat` → `Mover` + kinematic body · `trigger_multiple target X` →
`TriggerVolume{ActivateMover, target}` (needs the two-pass link) · `trigger_teleport` → Teleport ·
`trigger_hurt dmg N` → Damage · `light` brightness/color → `PointLight` · `info_player_start` →
player spawn. Full table: archived original §3.2.

## Regression safety
Before deleting the hard-coded showcase, add a headless scenario: *load `showcase.map`, assert the
player spawns on ground and the door opens.* Rebuild the current showcase as `assets/maps/showcase.map`
(archived original §3.3 has the step-by-step) and diff behaviour against the C++ version.

## Bottom line
The install is an afternoon; this plan is Phase 5. Do 2.1→2.5 in order; each step keeps the build green.
