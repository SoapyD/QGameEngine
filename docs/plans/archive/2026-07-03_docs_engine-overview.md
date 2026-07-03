# Plan — Docs: Engine Overview

**Group:** `docs` (part 2 of 2 — pairs with [2026-07-03_docs_architecture-sync.md](2026-07-03_docs_architecture-sync.md)).
**Graduated from** [archive/2026-06-08_architecture-docs-and-engine-overview.md](2026-06-08_architecture-docs-and-engine-overview.md) Part B.
**Status:** ✅ Shipped 2026-07-03 — `docs/architecture/ENGINE_OVERVIEW.md` created from Part B, refreshed to current reality (pickups/HUD/audio shipped, pickupSystem in tick order, descriptor-driven scene), and linked as "start here" from the root and architecture READMEs.

**Goal:** give the project a "start here" narrative — how the engine works, top to bottom —
so a new session can re-orient without reading source. The README currently jumps straight into
reference tables.

---

## The content already exists
The full overview is written in **Part B** of the archived original
([archive/2026-06-08_architecture-docs-and-engine-overview.md](2026-06-08_architecture-docs-and-engine-overview.md)):
one-paragraph summary · module map · frame loop · one fixed tick (`stepSimulation`) · the
ECS↔Jolt relationship · rendering · `buildWorld` · solid-vs-missing edges.

This plan is mostly **promotion + refresh**, not authoring from scratch.

## Work items
| # | Task |
|---|------|
| 1 | **Create `docs/architecture/ENGINE_OVERVIEW.md`** from Part B. |
| 2 | **Refresh it to current reality** — Part B was written 2026-06-08. Update the "shipped since / missing" edges: pickups, graphical HUD (crosshair/bars/ammo/flash/toast), and audio (miniaudio + SFX/music) are **shipped**; enemies/AI and TrenchBroom authored levels remain the open edges. Add the `audio/`, `pickup/`, and `debug_hud/` draw-split to the module map. |
| 3 | **Link it as "start here"** from both the root `README.md` and `docs/architecture/README.md` (coordinate with the link fixes in the [architecture-sync plan](2026-07-03_docs_architecture-sync.md) A.1). |

## Done when
`ENGINE_OVERVIEW.md` exists, reflects the current shipped feature set, and is the first link a
newcomer follows from either README.
