# Plan — Enemy AI: Ranged Attacks & Navmesh

**Group:** `AI` (follow-on to the shipped setup + behaviour + pathfinding plans).
**Status:** ✅ Shipped 2026-07-08 (both parts).
- **Part 2, Option A (grid-hardening) — ✅ shipped 2026-07-08.** Enemies now move with a
  `CharacterVirtual` (new `initEnemyCharacters`) whose kinematic **inner body** (`Layers::MOVING`)
  keeps blocking the player; `aiSystem` drives it with `ExtendedUpdate` (collided locomotion — no
  corner-clip) and owns the enemy `Position` (`joltSyncSystem` skips them — no more `JoltBody`).
  **Task 2 (rebuild-on-load):** no code needed — `buildNavGrid` already derives from the loaded
  `level` in `buildWorld`, and there is no runtime level-switch to rebuild for. **Task 3 (off-mesh
  links):** deferred. Navmesh (Option B) remains deferred — grid A\* is adequate at current scale.
- **Part 1 (ranged attacks) — ✅ shipped 2026-07-08.** New `Faction` on `Projectile` (derived from
  the shooter in `fireProjectile`); `updateProjectiles` skips same-faction targets **and** other
  projectiles, `raycastEntities` ignores projectiles (so an enemy's own bolt can't block its LoS).
  New `RangedAttack` component + `monster_ranged` archetype (blue, in the showcase). `aiSystem` gains
  a ranged branch: hold at standoff → telegraph (`windup`) → fire a dodgeable Enemy-faction bolt via
  `aiFireEnemyBolt`; melee grunts unchanged. Helpers split into `ai_step_character`/`ai_line_of_sight`
  /`ai_fire_bolt` (`ai_support.h`) to stay under the size cap. New headless scenarios `monster_ranged`
  + `friendly_fire`; all 18 scenarios green, conventions clean. **Deviations:** derived `Faction` from
  the shooter (no fire-helper signature change) and used projectile travel-time + a hold-and-aim pause
  as the telegraph (no separate muzzle-flash cue). **Deferred nice-to-haves:** target leading, burst
  fire, pain-interrupts-windup, a second hitscan archetype, muzzle-flash light.
**Depends on:** [2026-07-03_AI_setup.md](2026-07-03_AI_setup.md),
[2026-07-03_AI_behaviour.md](2026-07-03_AI_behaviour.md),
[2026-07-04_AI_pathfinding.md](2026-07-04_AI_pathfinding.md) (all shipped).

**Goal:** give enemies a **ranged attack** so they're a threat at distance instead of only in
melee, and decide the **navigation upgrade path** (keep grid A\* vs. move to a navmesh) now that
enemies path and TrenchBroom maps are landing.

---

## What's already in place (don't rebuild)
From the shipped AI trilogy:
- `systems/enemy/ai_system` — `target`-latched **aggro/pursue**: acquire on LoS within detect range,
  pursue to pursue range without LoS, path toward the player's live cell. Kinematic-steer follow +
  face-the-player + **melee** `applyDamage` on a cooldown (`weapon.gauntlet` swing sound).
- `engine/ai/` — `build_nav_grid` (blocks cells under walls/solid props, inflated by clearance;
  built in `buildWorld`, stored in ctx) + `find_path` (8-connected A\*, octile heuristic, no
  corner-cutting, node budget). `AIPath` component holds the waypoint list.
- `enemyDeathSystem` — Health ≤ 0 → death sound + body removed + entity destroyed.
- `systems/combat/` primitives reusable for enemy fire: `fire_hitscan`, `fire_projectile`,
  `spawn_tracer`, `splash_damage`, `raycast_entities`, `apply_spread`.

So ranged is: **give the enemy a weapon, add a fire branch to the Attack state, and route the shot
back at the player** — the projectile/hitscan machinery already exists (it just fires player→enemy
today and must be generalised to fire from any origin at any target).

---

## Part 1 — Ranged attacks

### Approach decision: shot type (pick before building)
| Option | Dodgeable? | Reuses | Effort | Notes |
|--------|-----------|--------|--------|-------|
| **A. Slow visible projectile** ⭐ | ✅ | `fire_projectile` + `update_projectiles` | M | A coloured bolt with `Velocity`+`Lifetime` the player can sidestep. Best-feeling FPS ranged enemy; telegraphs naturally by travel time. |
| B. Telegraphed hitscan | ⚠️ (windup only) | `fire_hitscan` + `spawn_tracer` | S | Instant hit after a wind-up flash. Cheapest; feels like a hitscan turret. Good for a second archetype. |
| C. Both, per archetype | ✅ | both | M–L | `monster_gunner` (hitscan) + `monster_lobber` (projectile). Defer the second until one feels good. |

**Recommendation: A** for the first ranged enemy — a dodgeable projectile is the readable,
skill-testing choice and reuses the rocket path almost wholesale. Keep B in reach for a later
turret-style archetype.

### Factions / friendly-fire (critical — do first)
Projectiles today are **player→enemy** only. Before enemies can shoot, shots need an owner side so
enemy fire hits the player, player fire hits enemies, and neither friendly-fires:
- Add a `Faction { Player, Enemy }` (or a `TagEnemyProjectile`) tag on the projectile/hitscan.
- `update_projectiles` / `raycast_entities` / `splash_damage` skip same-faction targets and hit the
  opposite one (player projectiles → `AABBCollider` enemies; enemy projectiles → the player
  character). Route enemy hits through the existing `applyDamage` → `DamageFlash` + `player.pain` +
  `PendingKnockback` path (same as melee today).
- Collision layers ([JOLT_PHYSICS.md](../../architecture/JOLT_PHYSICS.md)) may need an enemy-projectile
  layer if projectiles are physical bodies; keep it minimal.

### Scope
| # | Task | Notes |
|---|------|-------|
| 1 | **Ranged archetype** | New `monster_ranged` classname/factory (coloured box variant) **or** a `RangedAttack { range, damage, windup, cooldown, projectileSpeed }` component added to a grunt. Prefer a component so melee/ranged are data, not a class split. Add to showcase. |
| 2 | **Faction tagging** | Per "Factions" above — the enabling refactor. Own small change; keep melee working. |
| 3 | **Generalise fire helpers** | `fire_projectile`/`fire_hitscan` take an explicit origin + direction + faction (today they read the player/camera). Enemy fires from its eye toward the player, with `apply_spread` for miss chance. |
| 4 | **Attack-state fire branch** | In `Attack` with LoS: if the enemy has `RangedAttack` and target is within ranged range, **wind up** (brief telegraph — face + a flash/`DamageFlash` self-tint or muzzle cue + sound), then fire, then cooldown. Melee stays the close-range branch. |
| 5 | **Standoff behaviour** | Ranged enemies **keep distance**: pursue to enter ranged range, then hold / back off if the player closes inside a min range (so they don't walk into melee). Reuse the path-follow to retreat toward a cell away from the player. |
| 6 | **Perf/telegraph guardrails** | Cap concurrent enemy shots, enforce the wind-up so shots are dodgeable/readable, cap projectile count. `log`/document caps. |

### Nice-to-have (defer)
- Leading the target, burst fire, pain/flinch interrupting a wind-up, cover-seeking, a second
  hitscan archetype (Option B), muzzle flash light, distinct impact FX.

## Part 2 — Navmesh (decision, likely defer)

The pathfinding plan already **deferred navmesh** ("defer until levels are large") and shipped grid
A\*. Re-evaluate against the actual blocking gaps rather than rebuilding nav wholesale.

### Approach decision: navigation upgrade (pick before building)
| Option | Fixes | Effort | Notes |
|--------|-------|--------|-------|
| **A. Harden the existing grid** ⭐ | corner-clip, level reload, verticality | S–M | (a) **`CharacterVirtual` locomotion** so followers stop clipping walls on corner-cuts (the deferred item most likely to bite); (b) **rebuild `NavGrid` on TrenchBroom level load** (grid is hardcoded-showcase-derived today); (c) optional **off-mesh/jump links** for lifts/gaps. Keeps A\*. |
| B. Polygonal navmesh | smoother paths, big/multi-level maps | L–XL | Real navmesh (build from loaded brush geometry, poly A\* + string-pull). Only pays off once maps are large or multi-storey. Supersedes the grid. |
| C. Recast/Detour integration | industrial nav | XL | Third-party navmesh gen. Heavy dependency; overkill for current scale. |

**Recommendation: A now, B later.** The grid A\* is adequate at showcase scale; the real pain points
are corner-clipping (locomotion) and grid-rebuild-on-load (TrenchBroom), both solvable **without** a
navmesh. Do a full navmesh (B) only when a shipped TrenchBroom map is large/multi-level enough that
grid resolution or memory hurts — scope it then from measured need. **This plan implements Part 1
(ranged) + Part 2 Option A; Option B stays a documented future plan.**

### Scope (Part 2, Option A)
| # | Task | Notes |
|---|------|-------|
| 1 | **`CharacterVirtual` enemy locomotion** | Swap the kinematic-steer follow for `CharacterVirtual` (reuse the player's move primitives) so path-following collides instead of clipping. Verify enemy-vs-enemy and enemy-vs-player blocking (may keep a thin kinematic blocker shape). This is the deferred item from the behaviour plan. |
| 2 | **Rebuild `NavGrid` on level load** | Derive the grid from **loaded** TrenchBroom geometry, not the hardcoded showcase; rebuild when the level changes. Depends on the `.map` loader ([2026-07-03_trenchbroom_engine-loader.md](../2026-07-03_trenchbroom_engine-loader.md)). |
| 3 | *(optional)* **Off-mesh links** | Mark jump/lift transitions so enemies can traverse `TagTriggerable` lifts/gaps. Defer if not needed by the showcase. |

## Verification
- **Headless scenario `monster_ranged`**: place the player in LoS at ranged distance; step and assert
  the enemy enters `Attack`, **winds up then fires**, an enemy projectile travels toward the player,
  and the player's `Health` drops on hit — while a projectile that misses does **not** damage.
  Assert the enemy **holds standoff range** (doesn't close to melee).
- **Friendly-fire test**: an enemy projectile does not damage another enemy; a player projectile does
  not damage the player.
- Keep `monster_ai`, `monster_grunt`, `monster_path` green (melee grunt unchanged).
- If Option A #1 lands: `CharacterVirtual` follower does **not** clip the shelf on a corner-cut in
  `monster_path`.

## Docs to update on ship (anti-drift)
`SYSTEMS.md` (aiSystem ranged branch; any faction handling in combat systems),
`COMPONENTS.md` (`RangedAttack`, `Faction`/projectile-owner tag), `TICK_ORDER.md` (if a slot
changes), `JOLT_PHYSICS.md` (enemy-projectile layer, `CharacterVirtual` enemy body),
`ARCHITECTURE.md`/`ENGINE_OVERVIEW.md`/`status` (enemies → ranged; nav hardened).
Refresh the stale `status/combat.md` (verified 2026-06-14) while touching combat.
