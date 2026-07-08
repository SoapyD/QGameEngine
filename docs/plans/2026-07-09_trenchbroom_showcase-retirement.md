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

## Scope
| # | Task | Notes |
|---|------|-------|
| 1 | **Parity regression FIRST** | New headless scenario(s) that build from `showcase.map` and assert the invariants the C++ scenarios check: player spawns grounded, a door opens on its trigger, spawn counts match, lava damages. Prove `showcase.map` ≡ the C++ showcase **before** deleting anything. Depends on brush-entities (doors/triggers) landing. |
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
