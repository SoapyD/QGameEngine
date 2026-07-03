# Plan — Docs: Architecture Sync

**Group:** `docs` (part 1 of 2 — pairs with [2026-07-03_docs_engine-overview.md](../2026-07-03_docs_engine-overview.md)).
**Graduated from** [archive/2026-06-08_architecture-docs-and-engine-overview.md](2026-06-08_architecture-docs-and-engine-overview.md) Part A.
**Status:** ✅ Shipped 2026-07-03 — COMPONENTS/SYSTEMS/TICK_ORDER/ARCHITECTURE/SCENE_SETUP/JOLT_PHYSICS synced, `FACTORIES.md` added, anti-drift rule added. Several items (A.1 links, Gravity removal, stepHeight 0.7, death/respawn) were found already done during the pass.

**Goal:** bring `docs/architecture/*` back in sync with the code after the drift from
code-only commits. Mechanical; do in one pass.

> **Verify first:** this task list was written 2026-06-08; pickups, HUD and audio have shipped
> since. Check each item against the current docs/code before editing — some may already be fixed,
> and new drift (audio system, pickup/armour components, HUD draw split) may need adding too.

---

## Task list
| # | Fix |
|---|-----|
| A.1 | **Broken links** — root `README.md` points at `docs/ARCHITECTURE.md` etc.; the six live under `docs/architecture/`. `architecture/README.md` links `ROADMAP*`/`FUTURE_TUTORIALS` as siblings but they're in `docs/roadmap/`. Repoint. |
| A.2 | **`COMPONENTS.md`** (biggest gap) — add `PrevPosition`, `SpawnPoint`, `DamageFlash`, `PendingKnockback`, `CameraDirection`, `Health.invulnerableTimer` (+ now: `Armor`, `Pickup`, `PickupMessage`, ammo pools, audio `SoundQueue`). Remove deleted `Gravity`. Fix `CharacterPhysics.stepHeight` = **0.7** (doc says 1.5). Note `AABBCollider.layer/mask` inert. |
| A.3 | **`SYSTEMS.md`** — add `playerDeathSystem` (+ now: `pickupSystem`, `audioSystem`, HUD draw split). Confirm the documented order matches `simulation.cpp::stepSimulation`. |
| A.4 | **`SCENE_SETUP.md`** — drop `Gravity` from the player list; reconcile the lava damage value (doc "25 dmg/s" vs the real per-tick number); add `SpawnPoint`/`DamageFlash`/`PendingKnockback` (+ `Armor`, `WeaponInventory`, `Ammo`). |
| A.5 | **`ARCHITECTURE.md`** — move "Death/respawn" (and now pickups, HUD, audio) to *Implemented*. Fix the `extern/` tree: Jolt is FetchContent (not a submodule); no `tinyobjloader` (in-repo `obj_loader.cpp`); submodules are glfw/glm/entt; glad/stb vendored. Add `src/engine/app/` + `src/harness/`. |
| A.6 | **`JOLT_PHYSICS.md` / `TICK_ORDER.md`** — verify `stepHeight 0.7` and that the tick list includes `playerDeathSystem` (+ any new systems). |
| A.7 | **`FACTORIES.md`** (new) — now unblocked (factories shipped 2026-07-02). Document the `classname`→factory dispatch layer; it's the contract the TrenchBroom entity mapping targets. |
| A.8 | **Anti-drift rule** — add to `architecture/README.md`: *"When you change `components.h` or the system tick order, update COMPONENTS.md / SYSTEMS.md / TICK_ORDER.md in the same commit."* All the drift came from code-only commits. |

## Done when
`docs/architecture/*` matches `components.h`, the tick order, and the current `extern/` layout,
and the root/architecture READMEs link correctly. (The new `ENGINE_OVERVIEW.md` is the sibling
[docs engine-overview plan](../2026-07-03_docs_engine-overview.md).)
