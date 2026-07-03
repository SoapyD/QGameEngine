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

### Enemies — group `AI`
| Plan | Scope |
|------|-------|
| [2026-07-03_AI_setup.md](2026-07-03_AI_setup.md) | `monster_grunt` archetype: factory, Jolt body, health, spawning, trigger generalisation. Exists + shootable, no behaviour. |
| [2026-07-03_AI_behaviour.md](2026-07-03_AI_behaviour.md) | State machine (Idle→Chase→Attack→Dead), line-of-sight, seek movement, attacks, death. Depends on AI_setup. |

### Level authoring — group `trenchbroom`
| Plan | Scope |
|------|-------|
| [2026-07-03_trenchbroom_install.md](2026-07-03_trenchbroom_install.md) | Install + configure TrenchBroom (`tb/` config, FGD). No engine code — do now. |
| [2026-07-03_trenchbroom_engine-loader.md](2026-07-03_trenchbroom_engine-loader.md) | `.map` parser → brush mesh → entity mapping → brush collision → wire `buildWorld`. |

*(The `docs` group — architecture-sync + engine-overview — has shipped; see below.)*

---

## Recently shipped & archived
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
