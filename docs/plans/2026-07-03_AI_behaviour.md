# Plan — Enemy AI: Behaviour (state machine & combat)

**Group:** `AI` (part 2 of 2 — **depends on** [archive/2026-07-03_AI_setup.md](archive/2026-07-03_AI_setup.md), shipped 2026-07-03).
**Graduated from** [archive/2026-06-08_next-features.md](archive/2026-06-08_next-features.md) §C #5.
**Status:** Proposed — ready to implement.

**Goal:** make the grunt *act* — see the player, chase, and attack — so it's a real enemy rather
than a standing target. This is the "it's a game now" jump.

---

## What's already in place (don't rebuild)
From **AI setup** + the **feedback polish** (both shipped 2026-07-03):
- `monster_grunt` archetype: `Health`, `AIState { state, attackCooldown, target }`, a **kinematic
  Jolt body** (stands upright, blocks the player), spawned from the `monster_grunt` classname (2 in
  the showcase).
- **Shootable + damageable** for free (`raycastEntities` + `applyDamage`).
- **Death** already handled by `enemyDeathSystem` (Health ≤ 0 → death sound + body removed + entity
  destroyed). It also fades the hit-flash.
- **Feedback**: white hit-flash (`DamageFlash` + `renderSystem` `colorOverride`), `combat.flesh_hit`
  on hit, `combat.explosion_small` on death.
- Triggers already key off `TagTriggerable` (not `TagPlayer`), so enemies can opt into pads/doors
  later without touching trigger code.

So behaviour is really just: **decide → move → attack.** Death/feedback are done.

---

## Critical decision first: locomotion body type
The setup shipped a **kinematic** body. That was right for "stand + block", but a kinematic body's
*own* motion **ignores collisions** — if we drive it toward the player with `MoveKinematic`, it will
**clip through walls**. Pick how enemies move before writing `aiSystem`:

| Option | Wall collision | Blocks player | Effort | Notes |
|--------|----------------|---------------|--------|-------|
| **A. Keep kinematic + steer** | ❌ (clips walls) | ✅ | S | Fine in the open showcase; bad in corridors. Fastest v1. |
| **B. `CharacterVirtual` (like the player)** ⭐ | ✅ | ⚠️ needs check | M | Proper collided locomotion, reuses the player's move primitives. Two `CharacterVirtual`s may not block each other by default — verify, or keep a thin kinematic "blocker" shape. |
| **C. Dynamic rigid + locked rotation** | ✅ | ✅ | M | Collides + knockback (nice with rockets), but needs angular-DOF locking so it doesn't tip/slide. |

**Recommendation: A for v1** (the showcase is open, keeps the diff small and the milestone landable),
with a note in the plan that corridors/TrenchBroom levels will want **B**. Revisit once levels have
tight spaces. Whichever is chosen, update `buildWorld`'s enemy-body creation accordingly.

---

## Scope
| # | Task | Notes |
|---|------|-------|
| 1 | **`aiSystem`** (new system, `systems/enemy/ai_system.*`) | Runs after `joltSyncSystem`/`playerCharacterSystem` (player position current) and before `combatSystem`. Add to the tick order + `TICK_ORDER.md`/`SYSTEMS.md`. |
| 2 | **State machine** on `AIState.state` | `Idle → Chase → Attack` (`Dead` is effectively "removed by `enemyDeathSystem`"). Transition on distance-to-player + line-of-sight. |
| 3 | **Line-of-sight** | Reuse `raycastEntities` (+ the level-surface ray from `fireHitscan`) from the grunt's eye to the player; gate `Idle→Chase` and keep `Chase` only while visible/known. |
| 4 | **Seek movement** | Steer toward the player each tick (per the body decision above). Face the player (`Rotation.y`). Stop at attack range. |
| 5 | **Attack** | In range + `attackCooldown ≤ 0`: `applyDamage(player, N)` (melee first — a short-range hit; ranged/projectile is a nice-to-have), reset cooldown. This already feeds the player's `DamageFlash` + `player.pain` + `PendingKnockback` via `applyDamage`/`fireHitscan` paths. Give enemies an attack sound. |

## Nice-to-have (defer)
- Ranged/projectile grunts, pain/stagger states, multiple archetypes, group aggro, navmesh pathing,
  a floating enemy health bar, a death gib/particle burst, dropping a pickup on death.

## Verification
- **Headless scenario `monster_ai`**: place the player in LoS of a grunt; step and assert the grunt
  transitions `Idle→Chase`, its distance to the player decreases, it reaches `Attack`, and the
  player's `Health` drops on the attack cadence. Then move the player behind a wall (break LoS) and
  assert the grunt stops closing / drops out of `Chase`.
- Keep the existing `monster_grunt` scenario green (still shootable, still blocks, still dies).

## Docs to update on ship (anti-drift rule)
`SYSTEMS.md` (+`aiSystem`), `TICK_ORDER.md` (new slot), `COMPONENTS.md` (`AIState` now *driven*),
`ARCHITECTURE.md`/`ENGINE_OVERVIEW.md`/`status` (enemies → behaviour done).
