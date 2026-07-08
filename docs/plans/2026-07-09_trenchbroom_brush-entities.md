# Plan — TrenchBroom: Brush Entities (movers, triggers, collision)

**Group:** `trenchbroom` (follow-on to the shipped engine-loader + general-geometry work).
**Status:** 📝 Proposed 2026-07-09. **Priority: HIGH** — the biggest remaining unlock; it's what makes
a TrenchBroom-authored level actually *playable* (doors, lifts, hazards), not just walkable geometry.

**Goal:** make **brush entities** authored in TrenchBroom — `func_door`, `func_plat`, `trigger_multiple`,
`trigger_teleport`, `trigger_hurt` — fully work from a loaded `.map`: geometry, behaviour, **and**
collision, the same way they do in the hard-coded showcase.

---

## Why (current limitation)
The loader resolves **point** entities (dispatch → factory) and **worldspawn** brush geometry
(`buildBrushGeometry` → surfaces + convex hulls). But a brush *entity* — a door/lift/lava drawn as a
brush and tagged `func_door`/`trigger_hurt` — is only partly handled:
- Its brushes are **not** turned into render geometry or colliders (`mapWorldspawnToLevel` only reads
  `worldspawn`; `createLevelBodies` only builds hulls from `Level.collisionHulls`, which worldspawn
  fills). So a door drawn in TB is invisible and non-colliding.
- Its `Mover`/`TriggerVolume` component + the kinematic body (movers) / sensor overlap (triggers) that
  the showcase sets up in C++ aren't derived from the map entity.

## Scope
| # | Task | Notes |
|---|------|-------|
| 1 | **Brush-entity geometry** | Run each brush-entity brush through `buildBrushGeometry` (render surfaces + hull), like worldspawn. Group its surfaces so they can move with the entity (movers). |
| 2 | **Mover mapping** | `func_door`/`func_plat` → `Mover` (start = brush centre; end from `angle`+`lip`/`height` or explicit `endpos`) + a **kinematic hull body** that sweeps with the door (reuse `createKinematicBody`, but from the brush hull, not an AABB). |
| 3 | **Trigger mapping** | `trigger_multiple`/`trigger_teleport`/`trigger_hurt` → `TriggerVolume` (action + value + two-pass `target` link). Triggers stay **ECS-AABB overlap** (no Jolt body) — derive the AABB from the brush bounds. |
| 4 | **Entity origin / linking** | Reuse the existing two-pass `targetname` dispatch. Brush-entity geometry is authored in world space — no separate origin offset (unlike point entities). |
| 5 | **Render binding** | The mover's surfaces need a `MeshRenderer` that follows the kinematic body (like the C++ movers) so the door visibly opens. |

## Nice-to-have (defer)
Rotating doors (`func_rotating`), `func_button`, breakable brushes, brush-entity light volumes.

## Verification
Author a small `.map` with a `func_door` (+ `trigger_multiple` targeting it), a `trigger_hurt` lava
volume, and a floor. New headless scenario `map_brush_entities`: load it, assert the door's body
**blocks** then **opens** on trigger overlap (position moves toward `endpos`), the hurt volume damages
the player, and the door geometry has a hull. Keep `map_scene` (36) + `brush_geometry` green.

## Docs to update on ship
`SYSTEMS.md`/`COMPONENTS.md` (brush-entity path in the loader), `JOLT_PHYSICS.md` (mover hull bodies),
the [engine-loader plan](2026-07-03_trenchbroom_engine-loader.md) deferred list, `status/_overview.md`.
