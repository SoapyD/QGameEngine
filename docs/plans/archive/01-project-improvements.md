# Plan 01 — Current-System Improvements & Layout Review

**Status: ✅ COMPLETE — shipped & archived 2026-06-14.** Every item below (§A/§B
findings + Tiers 1–3) was implemented, built green, and verified against the 6
headless regression scenarios. See the Completion record for the per-item summary.

**Scope:** What can we improve in the engine as it stands (through Ch 15d + the
2026-06-08 eval fixes), and does the project layout still make sense before Phase 5
(TrenchBroom) lands?

**Verdict up front:** the architecture is sound — clean ECS/Jolt split, fixed
timestep, library/harness separation. The problems are **drift and dead-ends**, not
design: docs that describe removed code, a second (unused) level format, and entity
spawning that's hard-coded inline in a way that will block the TrenchBroom work.
None are urgent bugs; all are cheap to fix and pay off directly in Phase 5.

---

## Completion record — 2026-06-14

Shipped and verified (build green + all 6 headless scenarios pass identically
before/after each change — no behavioural drift):

| Item | Status | Notes |
|------|--------|-------|
| §B Documentation drift | ✅ done | All listed doc mismatches swept (README links, COMPONENTS `Gravity`/`stepHeight`, SCENE_SETUP, ARCHITECTURE death-respawn + extern tree, CMake C++20). |
| C-Tier1 #1 Entity factories | ✅ done | New `ecs/factories.{h,cpp}` (`spawnPlayer/PointLight/StaticBox/DemoCube/Mover/Trigger/DebugWireframe/DecorBox`); `scene_setup.cpp` now declarative. |
| C-Tier2 #5 Constants → PhysicsConfig | ✅ done | `PhysicsConfig::gravity` single-sources world + player-system gravity. |
| C-Tier2 #6 Input/camera as systems | ✅ done | `playerInputSystem` + `cameraFollowSystem` extracted from `main.cpp`. |
| C-Tier2 #7 Harness no-GL (Phase B) | ✅ done | GL-free stub resources + skip `buildSectorMeshes` in headless; hidden window removed. `QEngineHeadless` now needs no GPU/driver. |
| §A #3 Label/retire legacy code | ✅ done | Banner-marked the genuinely dead code only: `collision.{h,cpp}`, `spatial_hash.{h,cpp}`, `.qlvl` parser (`LevelLoader::*`), `test.qlvl`, `systems/archived/*` (folder README). The falsely-flagged **live** code (`aabb.h`, `collision_layers.h`, `buildSectorMeshes`) is now documented in `docs/processes/physics.md` → "Legacy & retained code" so it isn't re-flagged. |
| C-Tier1 #4 Visual interp check | ✅ done | Smooth motion + no teleport/respawn streak confirmed in-app (2026-06-14). |
| C-Tier2 #8 Generalise triggers | ✅ done | New `TagTriggerable` tag; `triggerSystem` keys off it instead of `TagPlayer` (player carries it — behaviour identical, enemies/props can opt in). |
| C-Tier3 #9 Uniform caching | ✅ done | `Shader` caches uniform locations (name→location map) instead of `glGetUniformLocation` every set. |
| C-Tier3 #10 Split `components.h` | ✅ done | Split into `ecs/components/{core,physics,combat,gameplay,rendering,tags}.h`; `components.h` is now a barrel so all includers are unchanged. |

---

## A. Layout assessment — what makes sense, what doesn't

### Makes sense (keep as-is)
- **`src/engine/{core,ecs,physics,renderer,level,app}`** — clear, conventional layering.
- **`qengine_lib` + `QEngine` + `QEngineHeadless`** (CMakeLists) — the library/two-entry-point
  split is excellent and rare in a hobby engine. Keep it; it's what makes the harness honest.
- **`docs/architecture/` vs `docs/roadmap/` vs `docs/plans/`** — good separation of
  *reference* / *intent* / *work*.
- **Pure-data components in one `components.h`, free-function systems** — correct ECS discipline.

### Doesn't make sense / needs a decision

| Issue | Detail | Recommended action |
|-------|--------|--------------------|
| **Two level systems, one unused** | `level/level_loader.cpp` parses a custom `.qlvl` text format (sectors/surfaces/portals/entities), but the running game **never calls it** — the showcase is built by `createShowcaseLevel()` (hard-coded geometry) + `scene_setup.cpp` (hard-coded entities). `assets/levels/test.qlvl` even references textures that don't exist (`floor_stone.png`, `wall_brick.png`, `ceil_dark.png`). | Decide explicitly: the `.qlvl` path is a **dead-end** because Phase 5 replaces it with TrenchBroom `.map`. Either (a) mark `level_loader` + `test.qlvl` as legacy/reference in a header comment and stop touching it, or (b) delete it when the `.map` loader lands. Don't invest in `.qlvl`. |
| **Inline entity spawning** | `scene_setup.cpp` hand-builds the door, lift, teleporter, lava, and lights inline (only `createWeapon`/`createStaticBody`/`createDynamicBody` are reused helpers). There are **no `spawnDoor` / `spawnLight` / `spawnTrigger` / `spawnPickup` factories.** | Extract an **entity-factory layer** (`ecs/factories.*` or per-type functions). This is the single highest-leverage refactor — Phase 5 entity mapping (`classname` → factory) *requires* it, and it makes the showcase readable. See Plan 02 §"Pre-work" and Plan 03. |
| **Legacy physics/code retained but unmarked** | `physics/{collision,spatial_hash,aabb,collision_layers}.*`, the legacy `raycast` triangle path, and `ecs/systems/archived/*` are deliberately kept as "old-vs-new" tutorial reference (per eval), but they sit alongside live code with no signpost. New readers can't tell live from dead at a glance. | Add a one-line banner comment to each retained-but-dead file (`// LEGACY / reference only — not compiled. See docs/architecture/...`) **or** move them under `src/engine/_legacy/`. Keep them (they're tutorial value), just label them. |
| **`AABBCollider.layer`/`mask` fields** | Unused since Jolt took over object-layer filtering; retained as harmless. | Leave for now, but note in COMPONENTS.md that they're inert (see §B). |
| **Root README structure block is stale** | README's tree lists `tinyobjloader` and `JoltPhysics` under `extern/` (they're FetchContent'd, not submodules) and omits `app/`, `harness/`. | Refresh the README tree (Plan 04 covers doc sync). |

---

## B. Documentation drift (concrete, verified against code)

These are factual mismatches between the docs and the current source — they erode trust
in the docs and should be swept in one pass (tracked in Plan 04, listed here as the
*findings*):

| Doc | Says | Code reality |
|-----|------|--------------|
| **Root `README.md`** | Links to `docs/ARCHITECTURE.md`, `docs/SYSTEMS.md`, … | Files live in `docs/architecture/`. **All six links are broken.** |
| `docs/architecture/README.md` | Links `ROADMAP.md`, `CPP_CONCEPTS_BY_CHAPTER.md`, `FUTURE_TUTORIALS.md` as siblings | `ROADMAP*`/`FUTURE_TUTORIALS` live in `docs/roadmap/`. Broken cross-links. |
| `COMPONENTS.md` | Documents a **`Gravity`** component | `struct Gravity` was **removed** in the eval cleanup — no longer in `components.h`. |
| `COMPONENTS.md` | `stepHeight` default **1.5** | `components.h` is **0.7** (JOLT_PHYSICS.md is correct at 0.7; COMPONENTS.md wasn't synced). |
| `SCENE_SETUP.md` | Player has `Gravity strength 20` | Player no longer gets a `Gravity` component. |
| `ARCHITECTURE.md` | "What's Not Yet Implemented: Death / respawn" | `player_death_system.cpp` **exists and is in the tick order** — death/respawn is implemented. |
| `ARCHITECTURE.md` extern tree | lists `tinyobjloader`, `JoltPhysics` as `extern/` dirs | Jolt is FetchContent; OBJ loading is the in-repo `obj_loader.cpp` (no tinyobjloader submodule present). |
| `CMakeLists.txt` | comment "Use C++17" | sets `CMAKE_CXX_STANDARD 20`. |

---

## C. Functional / engineering improvements (ranked by value)

### Tier 1 — do before Phase 5 (they unblock or de-risk it)
1. **Extract entity factories** (see §A). Blocker for TrenchBroom entity mapping.
2. **Doc-sync pass** (see §B). Cheap; restores docs as a reliable map for re-onboarding.
3. **Label/retire legacy code** (see §A). Removes "which of these is real?" friction.
4. **Confirm render interpolation visually.** The eval added `PrevPosition` + `getAlpha()`
   lerp but flagged it *"needs visual confirmation — harness can't render."* Run
   `build/QEngine.exe`, verify smooth motion + no streak on teleport/respawn. Close the loop.

### Tier 2 — quality / robustness
5. **De-magic the constants.** Gravity `-20` is hard-coded in both `JoltWorld::init` and
   `playerCharacterSystem` (and terminal velocity lives in `PhysicsConfig`). Route all three
   through `PhysicsConfig` so they can't disagree. (Cleanup-10a territory, but the duplication
   is a real foot-gun now.)
6. **Move input→`PlayerInput` glue and camera-follow out of `main.cpp` into systems.**
   `stepSimulation`/`buildWorld` are extracted, but `main.cpp` still owns input mapping and
   camera follow inline. Making them systems means the harness exercises the *same* input
   pipeline (today it scripts `PlayerInput` directly, bypassing the mapping).
7. **CI-ready headless (harness Phase B).** Today the harness needs a GL driver (hidden
   window). Decoupling `buildSectorMeshes`/`ResourceManager` from GL (stub ids in headless)
   gives true no-GPU CI — worth having before Phase 5 adds a `.map` parser that *must* be
   regression-tested.
8. **Generalise triggers beyond `TagPlayer`.** Currently triggers only fire for the player
   (documented in FUTURE_TUTORIALS). Once enemies exist (Plan 02), wire the already-stubbed
   `layer`/`mask` or a `TagTriggerable`.

### Tier 3 — nice, not now
9. Uniform-location caching in shaders (FUTURE_TUTORIALS) — negligible at current scale.
10. Split `components.h` if/when it grows past a few hundred lines (cleanup roadmap handles this).

---

## D. Suggested execution order

```
1. Doc-sync pass            (Plan 04)   — 1 sitting, no code risk
2. Label/retire legacy      (this plan) — mechanical
3. Extract entity factories (this plan) — enables Plan 02 & 03
4. Visual interp check      (this plan) — closes eval's open item
5. Constants → PhysicsConfig + input/camera as systems  — before Phase 5
6. Harness Phase B (no-GL CI)           — before the .map parser
```

Items 1–4 are a low-risk "tidy the desk" sprint; 5–6 are the real pre-Phase-5 hardening.
