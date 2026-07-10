# Plan — TrenchBroom: Brush Entities (movers, triggers, collision)

**Group:** `trenchbroom` (follow-on to the shipped engine-loader + general-geometry work).
**Status:** ✅ Verified 2026-07-09 (box-approximation path). **Priority: HIGH** — the biggest remaining
unlock; it's what makes a TrenchBroom-authored level actually *playable* (doors, lifts, hazards), not
just walkable geometry.

---

## Reality check (2026-07-09) — the "Why" below was outdated
The premise below ("a door drawn in TB is invisible and non-colliding") was already false by the time
this plan was picked up: the **classname-dispatch** work (archived
`2026-07-02_entity-factory-classname-dispatch.md`) routes `func_door`/`func_plat`/`trigger_*` through
[classname_factory.cpp](../../src/engine/level/classname_factory.cpp), deriving each brush entity's
origin+size from its brush AABB ([map_to_descriptors.cpp](../../src/engine/level/map_to_descriptors.cpp)).
So brush entities already **work as box approximations**: movers get a `Mover` + a kinematic **box**
body + a cube renderer, triggers get a sensor AABB + `TriggerVolume` with two-pass `target` linking.
For the axis-aligned boxes in `showcase.map` the approximation is essentially exact.

What was genuinely missing — and is now **closed** — was an **end-to-end proof**. Added:
- `assets/maps/brush_entities.map` — a small, stable fixture: floor + `func_door` (+ linked
  `trigger_multiple`) + `trigger_hurt` lava + player start. Deliberately **not** `showcase.map`, which
  the showcase-retirement plan will re-base.
- Headless scenario `map_brush_entities` ([headless_main.cpp](../../src/harness/headless_main.cpp)):
  builds the world from that `.map`, finds entities by component, and asserts the door has a kinematic
  collider (**blocks**), starts closed, and travels to `endPos` on trigger overlap (**opens**), and that
  the lava volume damages the player. Result: `door body=1 closed=1 opened=1 (progress=1.00) hurt=1
  (hp 100→75)`. `map_scene` (36) + `brush_geometry` stay green.

**Still deferred (Tasks 1/2/5, geometry fidelity):** a non-cuboid/angled brush entity still renders as
an enclosing cube and collides as an AABB rather than its true convex hull (unlike worldspawn, which got
general-geometry hulls). No map currently authors such an entity, so this is lower value than the plan's
original framing implied. Revisit if/when angled doors/lifts are authored — build the brush's render
surfaces centre-relative and give movers a kinematic **hull** body instead of the AABB box.

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
