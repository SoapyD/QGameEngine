# Plan — TrenchBroom: Retire the C++ Showcase (+ hot-reload)

**Group:** `trenchbroom`. **Status:** 📝 Proposed 2026-07-09. **Priority: LAST** — highest-risk, lowest-
urgency. **Do only after** brush-entities + textures land, so `showcase.map` is a fully faithful
replacement first.

**Goal:** make a loaded `.map` the single source of the showcase — delete the hard-coded
`createShowcaseLevel()`/descriptor path — and add runtime `.map` hot-reload.

---

## Why / the risk
`assets/maps/showcase.map` exists and loads, but the **C++ showcase is still the `buildWorld` default
and fallback**, and — critically — **every headless scenario builds the C++ showcase world**
(`buildWorld` with no map path → `setupScene`). Retiring it isn't a delete; it's **re-basing the whole
harness (~20 scenarios) onto `showcase.map`**. That's why this is sequenced last and gated behind a
parity proof — a premature delete would break the entire test suite.

## Prerequisite — manual `showcase.map` additions (author in TrenchBroom, **by Tom**)
`showcase.map` is currently **missing entities the C++ showcase has**, so it is NOT yet a faithful
replacement. These must be **added by hand in TrenchBroom** before Task 1's parity proof can pass — the
`.map` has to reproduce the C++ scene the harness asserts against (`spawn_counts` shape + the AI
scenarios). Targets below are in **engine space**; TrenchBroom is Z-up Quake units at ×32, so
map coords ≈ `(e.x·32, −e.z·32, e.y·32)`. Do **not** add `_wireframe` entities — the loader
auto-spawns a wireframe twin per trigger ([scene_setup_map.cpp](../../src/engine/app/scene_setup_map.cpp)).

| Add | Engine-space target | Why it's needed |
|-----|--------------------|-----------------|
| **`func_static` shelf** | origin (20, 1, 5), size (4, 2, 4), tex `grid_blue` | **Critical.** `monster_ai` + `monster_path` need this occluder for LoS-blocking + route-around tests. Map has **no `func_static` today**. |
| **+1 `monster_grunt`** | (8, 0.95, 22) | Map has 1 melee grunt; C++ has 2. |
| **+2 `prop_dynamic`** demo cubes | (20.5, 4, 5) vel `-6 0 0`; (20, 5, 8) vel `0 0 0` | Map has 1 demo cube; C++ has 3. |
| **+1 `light`** | (15, 5.5, 20), `_color 2 2 2` | Map has 4 point lights; `spawn_counts` expects **5**. |
| **Lava visible surface** (`func_decor`) | (20, 0.1, 25), size (6, 0.2, 6), tex `grid_red` | Damage zone exists but is invisible. Map has **no `func_decor` today**. |
| **Teleporter centre pole** (`func_decor`) | (5, 1.5, 5), size (0.1, 3, 0.1), tex `grid_blue` | Cosmetic marker over the teleport pad; part of the C++ scene. |

After these, `showcase.map` should match `spawn_counts` (1 player / 2 movers / 4 triggers / **5** lights /
1 sun / **3** demo cubes) and support both AI scenarios — the concrete parity bar for Task 1.

## Scope
| # | Task | Notes |
|---|------|-------|
| 1 | **Parity regression FIRST** | New headless scenario(s) that build from `showcase.map` and assert the invariants the C++ scenarios check: player spawns grounded, a door opens on its trigger, spawn counts match, lava damages. Prove `showcase.map` ≡ the C++ showcase **before** deleting anything. **Blocked on the manual map additions above** (+ brush-entities, now verified). |
| 2 | **Re-base the harness** | Point `headless_main`'s `buildWorld` at `showcase.map` (or make the map the default) so all sim scenarios run on the loaded map. Keep both paths until the full suite is green on the map. |
| 3 | **Delete the C++ showcase** | Remove `setupScene`/`showcase_descriptor`/`createShowcaseLevel` once parity is proven and the suite is green. Update `buildWorld` (map path becomes required, or a tiny built-in fallback remains). |
| 4 | **`.map` hot-reload** | Watch the loaded `.map` (or a reload key) → rebuild the world in place (tear down bodies/entities, `registry.clear()` respecting the [shutdown-order rule](#gotcha), reload). Fast iteration for level authoring. |

## Gotcha (carry over)
Rebuilding/reloading must destroy entities **before** tearing down Jolt bodies — enemy
`~CharacterVirtual` removes its inner body via the physics system (the shutdown use-after-free fixed
2026-07-08). Any hot-reload teardown must respect the same order.

## Verification
The **entire headless suite passes when re-based onto `showcase.map`** (not the C++ showcase). Hot-
reload: edit the map, reload, world reflects the change with no leak/crash (real exit code 0).

## Docs to update on ship
Everything referencing the C++ showcase (`SCENE_SETUP.md`, `ENGINE_OVERVIEW.md`, `ARCHITECTURE.md`,
`status/_overview.md`), and archive the [engine-loader plan](2026-07-03_trenchbroom_engine-loader.md)
once its whole deferred list is cleared.
