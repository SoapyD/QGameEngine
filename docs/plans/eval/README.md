# QEngine — Whole-Codebase Evaluation Plan

**Status:** Draft — evaluation only (no code changes from these plans; fixes spin out separately).
**Goal:** Systematically evaluate every source file in `src/` for correctness, dead code, doc↔code divergence, and Phase-5 readiness — in dependency order, before the project moves on to TrenchBroom (Phase 5).

---

## Where plans live (output convention)

| Path | What it is |
|------|-----------|
| `docs/plans/eval/README.md` | **This file** — master index, ordering, conventions. |
| `docs/plans/eval/NN-<area>.md` | One evaluation plan per area, numbered by evaluation order (`01-`…`08-`). |
| `docs/plans/<area>-fixes.md` | **Implementation** plans — created *only after* an area eval confirms real issues. Kept out of `eval/` so evaluation and fixing stay separate. |
| `docs/plans/archive/` | Completed plans, moved here once their PR ships (mirrors the project's existing plan-archival habit). |

**Rules**
- An area plan is evaluation-only. It instruments, reproduces, and attributes — it never edits engine code.
- Every area plan **ends with a findings table**: `Issue → Severity (P0/P1/P2) → Confirmed cause → Proposed action → Target file:line`.
- Confirmed P0/P1 findings graduate into a `*-fixes.md` implementation plan. P2s (cleanup, doc fixes) can be batched into the PR that touches that area.
- Cross-link to the architecture docs in [`docs/architecture/`](../../architecture/README.md); flag any divergence as a finding.

---

## Evaluation order (dependency bottom-up)

Foundations first, so each layer is understood before the layers that build on it. Physics is out of strict order because it's the **active concern** (the lift glitch) and is already in progress.

| # | Area | Plan file | Status | Why this position |
|---|------|-----------|--------|-------------------|
| 01 | Build & dead-code census | [`01-build-and-dead-code.md`](01-build-and-dead-code.md) | ✅ Evaluated | Removes noise first — confirm what's actually compiled before reading anything. |
| 02 | Core foundation | [`02-core.md`](02-core.md) | ✅ Evaluated | No game deps; everything else sits on it. |
| 03 | ECS data model | [`03-ecs-data-model.md`](03-ecs-data-model.md) | ✅ Evaluated | `components.h` is the shared contract every system reads/writes. |
| 04 | Renderer | [`04-renderer.md`](04-renderer.md) | ✅ Evaluated | Self-contained; consumes ECS data, drives nothing else. |
| 05 | **Physics (Jolt)** | [`05-physics.md`](05-physics.md) | 🔬 Hypotheses drafted (needs instrumentation) | Active glitch; deepest risk. |
| 06 | Level & scene setup | [`06-level-and-scene.md`](06-level-and-scene.md) | ✅ Evaluated | Bridges geometry → ECS entities → physics bodies. |
| 07 | Gameplay systems | [`07-gameplay-systems.md`](07-gameplay-systems.md) | ✅ Evaluated | Depend on physics + ECS being correct. |
| 08 | Integration (`main.cpp`) | [`08-integration.md`](08-integration.md) | ✅ Evaluated | Ties tick order, init order, ownership together — evaluate last. |

---

## Area scopes

### 01 — Build & dead-code census
**Files:** [CMakeLists.txt](../../../CMakeLists.txt), every `.cpp` vs the `add_executable` list.
**Check:** Which `.cpp` files are **not** compiled (already spotted: `physics/collision.cpp`, `physics/spatial_hash.cpp`, `physics/jolt_sync_system.cpp`, and the whole `ecs/systems/archived/` tree). For each: is it dead (delete), reference-only (keep + document), or accidentally dropped (re-add)? Also confirm `physics/aabb.h`, `physics/collision.h`, `physics/spatial_hash.h` have no live includers.
**Output:** a keep/delete/document decision per orphan. This unblocks every later area by guaranteeing you only read live code.

### 02 — Core foundation
**Files:** `core/window.*`, `core/input_manager.*`, `core/resource_manager.*`, `core/fixed_timestep.h`.
**Check:** Resource ownership/lifetime, GLFW callback wiring, input edge-vs-held semantics, fixed-timestep accumulator & spiral-of-death clamp, error handling on window/context creation.

### 03 — ECS data model
**Files:** `ecs/components.h`, `ecs/systems/system_phase.h`, `ecs/weapon_definitions.h`.
**Check:** Component defaults sanity, redundant/overlapping components, anything that's data-but-should-be-derived, and **doc divergence** against [COMPONENTS.md](../../architecture/COMPONENTS.md) (the `stepHeight` 0.7-vs-1.5 mismatch already found lives here).

### 04 — Renderer
**Files:** `renderer/{shader,texture,mesh,obj_loader,camera,stb_image_impl}.*`, `ecs/systems/render_system.*`, `ecs/systems/debug_hud_system.*`.
**Check:** GL resource leaks (VAO/VBO/texture/shader deletion), shader compile/link error handling, OBJ loader robustness, draw-call batching, no-interpolation render stepping (cross-ref physics §9), HUD text path.

### 05 — Physics (Jolt) — *drafted*
**Files:** `physics/{jolt_setup.h,jolt_world.*,physics_config.h,collision_layers.h}`, `ecs/jolt_body_helpers.*`, `ecs/systems/{player_character_system,mover_system,mover_sync_system,jolt_sync_system,demo_reset_system}.cpp`.
**Plan:** see [`05-physics.md`](05-physics.md) — tick-order desync, ground-velocity inheritance, internal-edge snagging, resting jitter, gravity/terminal-velocity, doc divergences, body budget.

### 06 — Level & scene setup
**Files:** `level/{level.h,level_loader.*}`, `ecs/{showcase_level.*,scene_setup.*}`.
**Check:** Level parse/validation, surface→AABB→Jolt body fattening correctness (feeds physics §4 internal edges), entity spawn completeness, init-order coupling with `main.cpp` (§08), hardcoded showcase vs the coming TrenchBroom path.

### 07 — Gameplay systems
**Files:** `ecs/systems/{combat_system,weapon_switch_system,lifetime_system,trigger_system,player_death_system}.cpp`, `physics/raycast.*` (live), `ecs/weapon_definitions.h`.
**Check:** Hitscan/projectile raycast correctness against Jolt vs the legacy `raycast.*`, splash/knockback, trigger overlap (ECS AABB vs the unused Jolt sensors — cross-ref physics §8), death/respawn state, lifetime cleanup ordering.

### 08 — Integration (`main.cpp`)
**Files:** `src/main.cpp`.
**Check:** Tick order vs [TICK_ORDER.md](../../architecture/TICK_ORDER.md) (the physics §2 reorder decision lands here), init/shutdown ordering, Jolt world lifetime vs registry, camera-follow sync, any logic that should be a system but lives inline.

---

## Method (applies to every area)

1. Read the live files for the area (skip orphans flagged in 01).
2. Cross-check against the matching architecture doc; log divergences.
3. Where behaviour is in question, instrument and reproduce rather than guess.
4. Fill the findings table; assign severity.
5. Graduate P0/P1 into a `*-fixes.md` implementation plan.

Severity: **P0** = wrong/broken behaviour (e.g. lift glitch). **P1** = latent bug or Phase-5 blocker. **P2** = dead code, doc drift, cleanup.

---

## Consolidated findings → fix backlog

All areas are evaluated. The confirmed P0/P1 items, grouped into the implementation plans they graduate into (ordered by recommended execution):

| Order | Fix plan | Bundles | Sev | One-liner |
|-------|----------|---------|-----|-----------|
| 1 | `dead-code-cleanup.md` | [01](01-build-and-dead-code.md) §1.1–1.4, 1.7 | P2 | Delete orphaned `collision.*`, `spatial_hash.*`, duplicate `jolt_sync_system.*`, dead triangle raycast; relocate `archived/`. Do first — guarantees later work edits live code. |
| 1.5 | [`../headless-debug-harness.md`](../headless-debug-harness.md) | enabler for #2/#3/#4 | P1 | Windowless build + scripted input + per-tick recording + assertions. Makes the lift/teleport/projectile bugs reproducible and regression-tested without a display or human. **Do before #2.** |
| 2 | `physics-fixes.md` | [05](05-physics.md) §2/3/5, [06](06-level-and-scene.md) §6.3/6.4, [08](08-integration.md) §8.1 | **P0** | The lift glitch: tick-order reorder + ground-velocity inheritance + internal-edge removal + `IsValid()` guards. Verified via the harness's `ride_lift_up` scenario (red → green). |
| 3 | `teleport-fix.md` | [07](07-gameplay-systems.md) §7.1 | P1 | Teleport trigger must `SetPosition` the `CharacterVirtual` (mirror `playerDeathSystem`). |
| 4 | `projectile-collision-fix.md` | [07](07-gameplay-systems.md) §7.2, [06](06-level-and-scene.md) §6.1 | P1 | Projectiles must test against level geometry (decide Jolt-cast vs collider entities). |
| 5 | `renderer-robustness-fixes.md` | [04](04-renderer.md) §4.1–4.3 | P1 | Make shader/asset/OBJ load failures fatal & visible instead of silent black screen. |
| 6 | `core-robustness-fixes.md` | [02](02-core.md) §2.1/2.2 (+2.3) | P1 | Window-ctor failure signalling + fix resize aspect-ratio (stale `m_width/m_height`). |
| 7 | `level-loader-hardening.md` | [06](06-level-and-scene.md) §6.2/6.5 | P1 | Loader must bail on open failure & validate ids — **before** Phase 5 leans on it. |
| 8 | `render-interpolation.md` | [08](08-integration.md) §8.2, [02](02-core.md) §2.5 | P1 | Use `getAlpha()` to interpolate rendered transforms; rules out "fake" jitter. |
| 9 | `integration-cleanup.md` | [08](08-integration.md) §8.4–8.7, [05](05-physics.md) §8 | P2 | Named camera-dir ctx type, drop dead sensor bodies, route mouse via InputManager, de-magic constants. |
| 10 | `component-cleanup.md` | [03](03-ecs-data-model.md) §3.2–3.6 | P2 | Remove post-Jolt vestigial components after confirming no readers. |
| 11 | `docs-sync.md` | [03](03-ecs-data-model.md) §3.1, [05](05-physics.md) §7, [08](08-integration.md) §8.3 | P2 | Fix architecture-doc divergences (stepHeight, tick order, lift-carry explanation). |

**Headline:** the only **P0** is the lift glitch (fix plan #2), and it is genuinely an integration/physics-ordering problem — confirmed at three layers (player system, mover sync, `main` tick order). Two **P1 functional regressions** also found beyond the original report: the **teleporter doesn't move the player** (#3) and **projectiles pass through walls** (#4). Everything else is latent robustness or cleanup.

Fix plans themselves are **not yet written** — each is a one-line entry above until the corresponding work is scheduled. Recommended next step: instrument and write `physics-fixes.md` (#2), since it's the active P0 and gates the Phase-5 milestone.
