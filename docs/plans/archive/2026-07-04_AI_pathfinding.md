# Plan — Enemy AI: Pathfinding

**Group:** `AI` (follow-on to the shipped setup + behaviour plans).
**Status:** ✅ Shipped 2026-07-04. **Option A (uniform grid + A\*)** as recommended. New `engine/ai/`: `NavGrid` (`build_nav_grid` — blocks cells under wall surfaces + solid props/movers, inflated by enemy clearance; skips triggers/player/enemies/demo cubes; built in `buildWorld`, stored in ctx) and `find_path` (8-connected A\*, octile heuristic, no corner-cutting, endpoint-snap, node budget). New `AIPath` component. `aiSystem` reworked to a `target`-latched **aggro/pursue** model (acquire on LoS within detect range, pursue to pursue range even without LoS, path toward the player's live cell). New headless scenario `monster_path` (aggroed grunt routes *around* the shelf — no-clip — and reaches the player). Fixed a `walk_floor_seams` regression (aggroed grunt disturbed the pure-physics test → added `clearEnemies`). 14/14 green, conventions clean. **Deviations from plan:** used the target's live position rather than a separate `lastKnownPlayerPos` (simpler; enemies "know" the player within pursue range). **Deferred:** navmesh, dynamic-obstacle avoidance, path smoothing, `CharacterVirtual` locomotion, rebuild-on-level-change (TrenchBroom).

**Goal:** enemies **navigate around walls and props** to reach the player, instead of the current
straight-line seek. Also gives them short-term memory so they path toward the player's last-known
position when line of sight breaks, rather than freezing the instant they lose sight.

---

## Why (current limitation)
`aiSystem` (behaviour plan) chases by steering the **kinematic** body straight at the player and
only while it has line of sight. Two consequences:
- **No obstacle routing** — a grunt walks *at* a wall/shelf between it and the player and gets stuck
  (or, being kinematic, clips through it). It can't go *around*.
- **No memory** — the instant LoS breaks (player ducks behind cover) the grunt drops to `Idle`. It
  won't pursue around the corner.

Pathfinding fixes both: a route that avoids blocked cells means the follower never needs to walk
through a wall, and a path to the last-known position keeps the chase alive around cover.

## Approach decision (pick before building)
| Option | Fits QEngine? | Effort | Notes |
|--------|---------------|--------|-------|
| **A. Uniform grid + A\*** ⭐ | ✅ | M–L | Sample the level into a 2D walkability grid (cell = walkable if floor present + no static collider + clearance). A\* over it → waypoint list. Simple, robust, works for the hardcoded showcase **and** future TrenchBroom maps (same derivation from geometry). |
| B. Navmesh | ✅ (overkill now) | L–XL | Polygonal nav mesh (better paths, more work to build/maintain). Defer until levels are large. |
| C. Steering / obstacle avoidance only | ⚠️ | S–M | Raycast "whiskers" + wall-follow. No real routing — fails on concave obstacles. Cheap but limited. |

**Recommendation: A (grid + A\*)** for v1 — the standard FPS-scale choice, and the grid derivation
is reusable once TrenchBroom lands.

## Scope
| # | Task | Notes |
|---|------|-------|
| 1 | **Walkability grid** | Build once at `buildWorld` from the level: a 2D grid over the room's XZ bounds (cell ~0.5–1.0 u). A cell is blocked if a level surface (wall) or a static-body `AABBCollider` overlaps it, or it lacks floor. Store in the registry context (`NavGrid`). Rebuilt when the level changes (TrenchBroom). |
| 2 | **A\* search** | `findPath(grid, startCell, goalCell) → std::vector<glm::vec3>` (waypoint centres). 8-connected, Euclidean heuristic, with a node-budget cap. Own file(s); pure function, unit-testable. |
| 3 | **Path component** | `AIPath { std::vector<glm::vec3> waypoints; size_t index; float repathTimer; glm::vec3 lastKnownPlayerPos; }` on enemies. |
| 4 | **Integrate with `aiSystem`** | In `Chase`: if LoS + in range → keep the direct-attack behaviour. Else follow the path toward the player's cell (or last-known cell). **Recompute** on a timer (e.g. every 0.3–0.5 s) or when the player changes cells — never every tick (A\* is not free). Steer the kinematic body toward the current waypoint (reuse the existing `MoveKinematic` follow). |
| 5 | **Last-known-position memory** | On losing LoS, remember `lastKnownPlayerPos`; keep pathing there. On reaching it with still no LoS → `Idle`. This is what makes them pursue around corners. |
| 6 | **Perf guardrails** | Cap concurrent repaths per tick (stagger enemies), cap A\* nodes, and `log`/document the caps. Grid + paths are cheap at showcase scale but must stay bounded for many enemies / big maps. |

## Nice-to-have (defer)
- Navmesh, dynamic obstacle avoidance (other grunts), path smoothing / string-pulling, jump links,
  crowd separation, off-mesh links for doors/lifts (enemies using `TagTriggerable`).

## Interactions / dependencies
- **Locomotion body type:** grid A\* routes *around* walls, so a kinematic follower mostly won't
  clip — but corner-cutting between waypoints can still clip. If that shows, revisit the
  `CharacterVirtual` option flagged in the behaviour plan. Decide during implementation.
- **TrenchBroom** ([2026-07-03_trenchbroom_engine-loader.md](../2026-07-03_trenchbroom_engine-loader.md)):
  the grid builder must derive from *loaded* level geometry, not the hardcoded showcase. Keep the
  derivation level-data-driven so it transfers.

## Verification
- **Headless scenario `monster_path`**: place a solid prop (the shelf, or a spawned wall) directly
  between a grunt and the player; assert the grunt **reaches attack range** (its distance to the
  player drops below the wall-blocked straight-line minimum) and damages the player — i.e. it went
  *around*, not into, the obstacle. Contrast with the pre-pathfinding behaviour (stuck at the wall).
- Unit-test `findPath` directly on a hand-built grid (open path, blocked path, no-path).
- Keep `monster_ai` + `monster_grunt` green.

## Docs to update on ship (anti-drift)
`SYSTEMS.md` (aiSystem now paths; any new nav system), `COMPONENTS.md` (`AIPath`, `NavGrid` ctx),
`TICK_ORDER.md` if a nav system is added, `ARCHITECTURE.md`/`ENGINE_OVERVIEW.md`/`status`.
