# Plan — Enemy AI: Behaviour (state machine & combat)

**Group:** `AI` (part 2 of 2 — **depends on** [2026-07-03_AI_setup.md](2026-07-03_AI_setup.md)).
**Graduated from** [archive/2026-06-08_next-features.md](archive/2026-06-08_next-features.md) §C #5.

**Goal:** make the grunt from the setup plan *act* — see the player, chase, attack, and die
convincingly. This is the "it's a game now" jump.

---

## Prerequisites
- `monster_grunt` archetype exists, has `Health` + a Jolt body + an `AIState` component, and can
  be shot to death (setup plan).
- Triggers generalised off `TagPlayer` (setup plan) so enemies can path through doors/pads.

## Scope
| # | Task | Notes |
|---|------|-------|
| 1 | **`aiSystem`** (new free-function system) | insert in the tick order — after `joltSync` (positions current), before/around `combatSystem`. Document the placement in `TICK_ORDER.md`. |
| 2 | **State machine** | `Idle → Chase → Attack → Dead`. Drive from `AIState` + distance/LoS to the player. |
| 3 | **Line-of-sight check** | reuse the combat raycast (`raycastEntities` / level ray) from the grunt's eye to the player; gate `Idle→Chase`. |
| 4 | **Seek movement** | steer the grunt's Jolt body toward the player (velocity for `CharacterVirtual`, or force/velocity for a dynamic body — matches the setup plan's body choice). Respect walls; no full pathfinding required for v1. |
| 5 | **Attack** | in `Attack` range, apply damage to the player on a cooldown (melee first; optional hitscan/projectile reusing `combatSystem` primitives). Feeds the player's existing `DamageFlash` + knockback. |
| 6 | **Death** | on `Health ≤ 0` → `Dead` state → despawn (or drop a pickup). Play a death sound (`monster.death` already in the manifest if wired). |

## Nice-to-have (defer)
- Multiple archetypes, ranged grunts, pain/stagger states, group aggro, navmesh pathing.

## Verification
- Headless scenario: place a grunt with LoS to a stationary player; assert it transitions
  `Idle→Chase`, closes distance, enters `Attack`, and the player's `Health` drops on the
  attack cadence. Break LoS → assert it stops chasing.
