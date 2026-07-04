# QEngine — Active Plans

Forward-looking implementation plans. Each proposes and scopes outstanding work; plans move to
[`archive/`](archive/) once shipped.

## Naming convention
`YYYY-MM-DD_DESCRIPTION.md` — the date is when the plan was **authored**, so files sort
chronologically. When one effort is split into several plans **of the same type**, they share a
group token: `YYYY-MM-DD_GROUP_DESCRIPTION.md`
(e.g. `2026-07-03_AI_setup.md` + `2026-07-03_AI_behaviour.md`). `DESCRIPTION` is lowercase-kebab.

These are **planning documents** — they propose work, they don't perform it.

## Active plans

### Fixes
| Plan | Scope |
|------|-------|
| [2026-07-04_mouse-look-fix.md](2026-07-04_mouse-look-fix.md) | Mouse look drops motion while moving — `InputManager` overwrites the frame's mouse delta instead of accumulating it. ~2-line fix. |

### Level authoring — group `trenchbroom`
| Plan | Scope |
|------|-------|
| [2026-07-03_trenchbroom_install.md](2026-07-03_trenchbroom_install.md) | Install + configure TrenchBroom (`tb/` config, FGD). No engine code — do now. |
| [2026-07-03_trenchbroom_engine-loader.md](2026-07-03_trenchbroom_engine-loader.md) | `.map` parser → brush mesh → entity mapping → brush collision → wire `buildWorld`. |

*(The `docs` group — architecture-sync + engine-overview — has shipped; see below.)*

---

## Recently shipped & archived
- **Fix: player speed run-away** — shipped. `playerCharacterSystem` now does ground accel/friction
  in the ground-relative frame (platform velocity inherited once, not double-counted each tick —
  the bug the new horizontally-moving enemies exposed) + a `CharacterPhysics.maxHorizontalSpeed`
  (20) backstop. New `speed_cap` scenario. Done directly (no dated plan). Shipped 2026-07-04.
- [2026-07-04_AI_pathfinding.md](archive/2026-07-04_AI_pathfinding.md) — enemies route around
  walls/props: new `engine/ai/` (`NavGrid` + A\* `find_path`), `AIPath` component, `aiSystem`
  aggro/pursue + path-follow. `monster_path` scenario. Shipped 2026-07-04.
- [2026-07-03_AI_behaviour.md](archive/2026-07-03_AI_behaviour.md) — enemy behaviour: `aiSystem`
  (LoS-gated `Idle→Chase→Attack`, kinematic-steer chase, melee attack), tick slot before the
  physics step. `monster_ai` headless scenario. Shipped 2026-07-04.
- **Enemy hit/death feedback** — shipped (grunt white hit-flash via `DamageFlash` + `renderSystem`
  `colorOverride`; `combat.flesh_hit` on hit, `combat.explosion_small` on death; `enemyDeathSystem`
  fades the flash). Done directly (no dated plan). Shipped 2026-07-03.
- [2026-07-03_AI_setup.md](archive/2026-07-03_AI_setup.md) — enemy foundation: `monster_grunt`
  archetype (new `AIState` component, coloured box, kinematic body that blocks the player,
  shootable + dies via a new `enemyDeathSystem`), 2 in the showcase, `monster_grunt` headless
  scenario. Behaviour (chase/attack) still open in AI_behaviour. Shipped 2026-07-03.
- **First-person weapon viewmodel + weapon readability** — shipped: procedural per-weapon gun
  shapes + colours drawn in view space (idle bob, fire recoil, switch drop/raise; `useAlbedo` path
  added to the lit shader). Weapon pickups render as the same coloured gun models (shared `gun_*`
  meshes + a `Colour` albedo path in `renderSystem`), and the HUD gained a top weapon bar showing
  slots 1-7 with un-collected weapons greyed out. Done directly (no dated plan). Shipped 2026-07-03.
- [2026-07-03_weapons-ammo-wiring.md](archive/2026-07-03_weapons-ammo-wiring.md) — fixed 7-slot
  weapon inventory (keys 1-7 = weapon type); start with shotgun + rocket launcher, collect the rest.
  Per-pool ammo + showcase pickups were already in place. All 11 headless scenarios green. Shipped 2026-07-03.
- [2026-07-03_docs_engine-overview.md](archive/2026-07-03_docs_engine-overview.md) — created
  `docs/architecture/ENGINE_OVERVIEW.md` (the "start here" narrative) from the archived Part B,
  refreshed to current reality and linked from both READMEs. Shipped 2026-07-03.
- [2026-07-03_docs_architecture-sync.md](archive/2026-07-03_docs_architecture-sync.md) — synced
  `docs/architecture/*` to the code (COMPONENTS/SYSTEMS/TICK_ORDER/ARCHITECTURE/SCENE_SETUP/
  JOLT_PHYSICS), added `FACTORIES.md` + an anti-drift rule. Shipped 2026-07-03.
- **Graphical HUD + crosshair** — shipped (crosshair, health/armour bars, ammo, damage-flash
  overlay, pickup toast). Graduated from the next-features build order (#3).
- **Audio (miniaudio)** — shipped 2026-07-03; tutorial Chapter 20.
- [2026-07-02_item-pickups.md](archive/2026-07-02_item-pickups.md) — item pickups
  (health/ammo/armour/weapons) via trigger-overlap; armour absorption, ammo consumption, weapon
  keys, HUD polish. Shipped 2026-07-02; Chapter 19.
- [2026-07-02_entity-factory-classname-dispatch.md](archive/2026-07-02_entity-factory-classname-dispatch.md)
  — data-driven spawning: `classname`→factory dispatch + `SpawnParams` + two-pass `targetname`
  linking. Shipped 2026-07-02; Chapter 18.
- [conventions/](archive/conventions/README.md) — WyrdWars-style C++ file conventions +
  C++-aware CI checks. 7 plans; shipped 2026-06-15.
- [2026-06-14_project-improvements.md](archive/2026-06-14_project-improvements.md) — current-system
  improvements & layout review. Implemented and verified 2026-06-14.

## Superseded planning docs (archived)
These combined June planning docs were split into the focused plans above and archived:
- [2026-06-08_next-features.md](archive/2026-06-08_next-features.md) — build-order inventory
  → weapons-ammo-wiring + AI setup/behaviour (HUD, factories, pickups, audio already shipped).
- [2026-06-08_trenchbroom-setup.md](archive/2026-06-08_trenchbroom-setup.md)
  → trenchbroom install + engine-loader.
- [2026-06-08_architecture-docs-and-engine-overview.md](archive/2026-06-08_architecture-docs-and-engine-overview.md)
  → docs architecture-sync + engine-overview.
