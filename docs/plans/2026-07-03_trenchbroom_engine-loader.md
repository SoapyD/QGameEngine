# Plan — TrenchBroom: Engine `.map` Loader

**Group:** `trenchbroom` (part 2 of 2 — **depends on**
[2026-07-03_trenchbroom_install.md](2026-07-03_trenchbroom_install.md) for a sample `.map`).
**Graduated from** [archive/2026-06-08_trenchbroom-setup.md](archive/2026-06-08_trenchbroom-setup.md) Parts 2–3.

**Goal:** load a TrenchBroom `.map` into the running engine, replacing the hard-coded
`createShowcaseLevel()`. This is the real build effort (Phase 5, Ch 17–20).

**Status (2026-07-05):** ✅ **Shipped at MVP fidelity** (commit `ff3db55`, branch `trenchbroom`).
`smoke.map` loads and is playable (walk the room, walls collide). Steps 2.2–2.5 done via the
**AABB box-fidelity** approach rather than the general plane∩plane∩plane algorithm originally
sketched below — each brush → its axis-aligned bounding box → 6 quad surfaces. This is **lossless
for axis-aligned box maps** (all `smoke.map` is) and reuses the engine's existing AABB-per-surface
collision + nav-grid path. **Deferred:** general angled-brush geometry (true plane intersection +
convex-hull collision), brush-entity movers/triggers exercised by a real map, texture-name→GL for
non-preloaded textures, and deleting the showcase (kept as `buildWorld` fallback). Documented in
tutorial Ch 26–28.

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
| **2.2** ✅ | **Brush → geometry (MVP)** — DONE 2026-07-05 via **brush AABB → 6 quad surfaces** (`map_transform.h` Z-up→Y-up ÷32; `map_to_level.{h,cpp}`; per-texture render meshes in `build_textured_meshes.{h,cpp}`, owned by `Level.renderMeshes`). CCW-from-outside winding under back-face cull; majority-texture per brush. General plane∩plane∩plane geometry **deferred**. | reused `Surface`/`buildSectorMeshes` path | 28 |
| **2.3** ✅ | **Entity mapping** — DONE. `map_to_descriptors.{h,cpp}`: `MapEntity` → `SpawnParams` (origin/size, spatial props → engine space), then the **existing** `factories::spawnScene` two-pass dispatch/linking — no new factory code. | reused 2.0 dispatch table | 28 |
| **2.4** ✅ | **Brush collision** — DONE via the existing `createLevelBodies` (AABB box per surface, ±0.1 fatten) run on the map's surfaces. Convex-hull from brush planes deferred with the general geometry. | reused `createLevelBodies` | 28 |
| **2.5** ✅ | **Wire `buildWorld`** — DONE. `scene_setup_map.{h,cpp}` + `buildWorld(mapPath)` / `main.cpp` arg; empty path ⇒ showcase fallback (not deleted). Texture-name resolution via ResourceManager. Hot-reload not done. | `simulation.cpp::buildWorld`, `main.cpp` | 28 |

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
