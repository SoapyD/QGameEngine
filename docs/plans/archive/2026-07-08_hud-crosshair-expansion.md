# Plan — HUD & Crosshair Expansion

**Group:** *(standalone)*.
**Status:** ✅ Shipped 2026-07-08.
- **Shipped:** dynamic crosshair (#1 — gap = base + weapon spread + movement + recoil, so it's
  per-weapon via `spread`), hit + kill markers (#2), low-ammo red cue (#3), and — a bonus from the
  ranged-AI synergy — the damage-direction chevron (#4). The debug/production split (#6) landed as a
  `HudSignals::showDebug` flag gating the FPS text. **Architecture:** all HUD *state* lives in a new
  `HudSignals` context struct, recomputed each tick by `hudSignalSystem` (tick slot 15) with the
  transient markers *set* at their event sites (`fireHitscan`/`updateProjectiles`/`aiSystem`) — so
  the whole thing is **tested headless** (`hud_signals` scenario) even though the GL draw needs a
  context. New draws `draw_hit_marker` + `draw_damage_arc`; `draw_crosshair` now takes a gap.
- **Deviations:** recoil is *derived* from the weapon cooldown (no combat write); the telegraph/
  per-weapon differentiation comes from `spread`, not distinct reticle shapes (#C).
- **Deferred (documented nice-to-haves):** floating enemy health bar (#5 — needs world→screen
  projection plumbing) and a **key** to flip `showDebug` (#6 — the flag exists, input wiring is a
  small follow-up). Menus/pause, minimap, resolution-independent layout remain out of scope.

**Goal:** grow the current immediate-mode HUD from a functional debug overlay into a **readable,
game-feel HUD** — a dynamic crosshair that reacts to firing, clearer combat feedback, and enemy
readability — without pulling in a UI framework.

---

## What's already in place (don't rebuild)
The "Graphical HUD + crosshair" milestone shipped. Current HUD lives in
`systems/debug_hud/` as immediate-mode `draw_*` passes over `stb_easy_font`:
- `draw_crosshair` — a **static** crosshair.
- `draw_bar` — health / armour bars.
- `draw_ammo` — current-weapon ammo.
- `draw_weapon_bar` — top slot bar 1-7 (uncollected greyed).
- `draw_flash_overlay` — full-screen damage-flash tint.
- `draw_panel` / `draw_text` — panels + debug text (FPS, position).

So this is **expansion**, not a build-from-scratch: new `draw_*` passes and making the crosshair
**dynamic**. Keep the immediate-mode, params-in style (the render/HUD pass takes camera/window/fps
directly — noted as inconsistent with ctx-singletons but fine; don't refactor that here).

> Note: `status/rendering.md` is stale (verified 2026-06-14, says "debug HUD only — no crosshair").
> Refresh it on ship.

---

## Approach decision: crosshair dynamics (pick before building)
| Option | Communicates | Effort | Notes |
|--------|-------------|--------|-------|
| **A. Spread + hit-marker crosshair** ⭐ | accuracy + "you hit something" | S–M | Gap scales with weapon spread / recoil / movement (`apply_spread` already exists); a brief **hit-marker** (X flash) when a shot connects, and a **kill-marker** variant. The two highest-value additions. |
| B. Full context crosshair | + interaction/ammo state | M | A also colour-shifts (e.g. red on enemy target, low-ammo tint) via a per-frame LoS ray. More work, more moving parts. |
| C. Per-weapon crosshair shapes | weapon identity | S | Distinct reticle per weapon slot. Cheap polish; stack on top of A. |

**Recommendation: A first** (spread + hit-marker are the felt improvements), then layer **C** as
cheap per-weapon polish. Fold B's enemy-target tint in only if it reads well.

## Scope
| # | Task | Notes |
|---|------|-------|
| 1 | **Dynamic crosshair spread** | `draw_crosshair` gap = base + f(weapon spread, recent recoil, movement). Source spread from the weapon def / `apply_spread`; decay recoil over time. Per-weapon base gap (ties into Option C). |
| 2 | **Hit-marker** | On a shot that damages an entity, flash a short-lived hit-marker (X) over the crosshair; a distinct **kill-marker** on a killing blow. Needs a HUD-facing "last hit" signal from combat (`applyDamage` → a transient `HudEvent`/ctx flag the HUD reads + times out). |
| 3 | **Low-ammo / no-ammo cue** | `draw_ammo` turns red (or pulses) at low ammo; a "click"/empty cue when firing with an empty pool. Reuse existing per-pool ammo. |
| 4 | **Damage-direction indicator** | When the player takes a hit, a directional arc/arrow at screen edge pointing at the damage source (from the attacker position vs. camera yaw). Extends `draw_flash_overlay`; feeds off the same `applyDamage` path enemies already use. Becomes genuinely useful once ranged enemies land ([2026-07-08_AI_ranged-and-navmesh.md](2026-07-08_AI_ranged-and-navmesh.md)). |
| 5 | **Enemy readability** | Optional floating enemy **health bar** (billboarded over aggroed grunts) — deferred nice-to-have from the AI behaviour plan. Screen-space bar over the entity's projected head position. |
| 6 | **Debug/production HUD split** | Gate the debug text (FPS/position) behind a toggle so the production HUD is clean; keep debug on a key. Small, but stops debug text shipping into "game" view. |

## Nice-to-have (defer)
- Menus / pause screen, objective/pickup log, minimap, scalable HUD (resolution-independent
  layout), a proper `HudConfig` in ctx (would resolve the params-in inconsistency — separate cleanup).

## Interactions / dependencies
- **Ranged AI** ([2026-07-08_AI_ranged-and-navmesh.md](2026-07-08_AI_ranged-and-navmesh.md)): the
  damage-direction indicator (#4) and enemy health bar (#5) pay off most once enemies attack at
  range. Fine to build HUD first — they degrade gracefully with only melee enemies.
- Combat needs to expose a **HUD-facing hit/damage signal** (#2, #4) — a transient ctx event the HUD
  reads and times out, rather than the HUD polling combat internals. Keep it one small addition.

## Verification
- **Headless** where possible: assert the HUD event/ctx state, not pixels — e.g. firing a shot that
  hits sets the hit-marker flag (and clears after its timeout); a killing blow sets the kill-marker;
  taking damage sets the damage-direction vector toward the attacker; ammo-low flag flips at the
  threshold. Add a `hud_signals` scenario.
- **Manual/visual** for layout: crosshair spread widens while moving/firing and tightens at rest;
  markers flash correctly; direction indicator points at the attacker. Capture a showcase screenshot.
- Keep the existing HUD passes (bars, ammo, weapon bar, flash overlay) rendering unchanged.

## Docs to update on ship (anti-drift)
`status/rendering.md` (refresh — it's stale; HUD now dynamic crosshair + markers + direction
indicator), `SYSTEMS.md` (HUD passes; any combat→HUD signal), `COMPONENTS.md`/ctx
(new HUD event/flag), `ARCHITECTURE.md`/`ENGINE_OVERVIEW.md` (HUD section).
