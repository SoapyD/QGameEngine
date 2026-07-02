# Plan 02 — What to Build Next

**Scope:** Inventory the "objects" (entity archetypes) the engine can build *today*,
compare against the roadmap, and recommend an ordered set of next features.

---

## A. What we can build today (the current toolbox)

Everything below already works in code (verified against `components.h`, the systems,
and `scene_setup.cpp`):

| Object / archetype | Built from | Notes |
|--------------------|------------|-------|
| **Static geometry** | `Surface`s → `createLevelBodies`, or `createStaticBody` | Walls, floors, ceilings, shelves. |
| **Dynamic physics props** | `createDynamicBody` + `DemoReset` | Falling/sliding cubes; auto-respawn on a timer. |
| **Doors** (kinematic) | `Mover` (vertical/any-axis slide) + `createKinematicBody` | State machine: Idle→StartDelay→Moving→Waiting→Returning. Pushes the player. |
| **Lifts/platforms** | same `Mover` + kinematic body | Player rides correctly (ground-velocity inheritance). |
| **Trigger volumes** | `TriggerVolume` + ECS AABB overlap | Actions: **ActivateMover, Teleport, Damage, Heal, ChangeLevel, Message**. Player-only (`TagPlayer`). |
| **Teleporters / damage zones (lava)** | `TriggerVolume` (Teleport / Damage) | Showcase uses both. |
| **Player** | `JoltCharacter` + `CharacterPhysics` + `Health` + `SpawnPoint` | Quake movement, jump, bunny-hop, stair-step, **death/respawn**, invuln timer, `DamageFlash` + `PendingKnockback` (data present). |
| **Weapons** | `Weapon` + `createWeapon()` | **All 7 are already defined** (Shotgun, SuperShotgun, Nailgun, RocketLauncher, GrenadeLauncher, LightningGun, Railgun) — but only Shotgun + RocketLauncher are *handed to the player*. |
| **Projectiles & hitscan** | `combatSystem` + `Projectile` + `Lifetime` | Pellets, spread, splash damage, knockback, tracer visuals. |
| **Lights** | `DirectionalLight`, `PointLight` (+ debug cube) | Phong, up to 8 point lights. |
| **Debug HUD** | `debugHudSystem` (stb_easy_font) | FPS/pos/vel/health/ammo/weapon as text. |

### The notable *gaps* (things we conspicuously can't make yet)
- **Pickups** — no item entities. Weapons/ammo/health can't be collected in-world.
- **Enemies / AI** — no NPC archetype at all.
- **Audio** — silent engine.
- **Graphical HUD** — crosshair, health/ammo bars, damage-flash *rendering* (the data
  exists, nothing draws it).
- **Game states** — boots straight into gameplay; no menu/pause/win-lose.
- **Authored levels** — geometry is hard-coded (see Plan 03 for TrenchBroom).

---

## B. Roadmap context

- **Phase 5 (Ch 16–20)** is "Written" as tutorials but **not in code**. Ch 16 (gameplay
  polish) is *partly* coded already (death/respawn, knockback, damage-flash data).
  Ch 17–20 are the TrenchBroom pipeline → **Plan 03**.
- **Nice-to-haves (Ch 21+):** game-state machine/menus, save/load, top-down adaptation,
  multiplayer. All optional, none blocking.
- **CPP_CONCEPTS / Showcase roadmaps** assume an *Enemy AI* and *Audio* chapter exist
  conceptually but they were never given chapter slots in the current ROADMAP (the old
  Ch 14 "Enemy AI" got reassigned to Jolt). So enemies/audio are **un-slotted** — a real
  gap, not just "not done yet."

---

## C. Recommended build order

There are two reasonable tracks depending on the goal. I recommend **Track 1**.

### Track 1 (recommended): "Make the showcase a game, then open it up to TrenchBroom"
Small, high-leverage gameplay content first — each item is cheap, reuses existing systems,
**and produces an entity type TrenchBroom will need an FGD entry for** (so it doubles as
Phase-5 prep). Then jump to TrenchBroom.

| # | Feature | Why now | Reuses | Size |
|---|---------|---------|--------|------|
| 1 ✅ | **Entity-factory refactor** → **shipped** (2026-07-02), see [archive/2026-07-02-entity-factory-classname-dispatch.md](archive/2026-07-02-entity-factory-classname-dispatch.md). The literal "inline spawns → `spawn*` functions" part shipped earlier (2026-06-14, `factories::`); the dated plan delivered the rest: a `classname`→factory dispatch layer so map data / pickups / enemies can hang off it. Prereq for everything below *and* for TrenchBroom. | — | S |
| 2 | **Item pickups** (health, ammo, armour, weapon) → **graduated** to [2026-07-02-item-pickups.md](2026-07-02-item-pickups.md). Closes the gameplay loop (you can already lose health/ammo, but not regain them by exploring). Pure reuse of trigger-overlap + `Health`/`Ammo`. New `Pickup` component + `pickupSystem`. | `triggerSystem` overlap, `Ammo`, `Health`, `WeaponInventory` | S |
| 3 | **Graphical HUD + crosshair (finish Ch 16)** | The "polish" milestone. Crosshair, health/ammo bars, and actually *render* the existing `DamageFlash`. Makes it feel like a game. | `debugHudSystem` plumbing, HUD shader, `DamageFlash` data | S–M |
| 4 | **Flesh out weapons & ammo wiring** | All 7 weapons are defined; grant them, give weapon-switch keys 1–7, stock the 4 ammo types, and confirm each weapon decrements the *right* ammo pool. Fix the **Nailgun** (flagged `Hitscan` but has `projectileSpeed` — likely meant `Projectile`). | `createWeapon`, `combatSystem`, `weaponSwitchSystem` | S |
| 5 | **Basic enemy / AI** | The biggest "this is a game now" jump and a long-standing gap. A `monster_grunt`: `Health` (already takes damage), a small state machine (Idle→Chase→Attack→Dead), Jolt body, line-of-sight check, simple seek movement. Generalise triggers off `TagPlayer` so enemies can use doors. | `Health`, combat damage, Jolt bodies, `Mover`/trigger generalisation | L |
| 6 | **Audio** (miniaudio) | Independent; slot anytime after #3. Weapon fire, footsteps, pickup blips, ambient hum, positional 3D. | new `AudioSource` component + system | M |

After #1–#5 you have a self-contained mini-FPS; **then move to Plan 03 (TrenchBroom)** so
all this content can be placed by hand instead of hard-coded.

### Track 2 (alternative): "TrenchBroom first"
If the priority is *authoring levels* over *more gameplay*, do only **#1 (factories)** and a
**minimal #3 (crosshair)** from Track 1, then jump straight to Plan 03. Enemies/audio/pickups
come later and are easier to place once the editor exists. Downside: you'll be building the
`.map` pipeline against a still-bare gameplay set.

**Recommendation:** Track 1 through item #3 at minimum (factories + pickups + HUD) before
TrenchBroom — that's ~the Ch 16 milestone — then decide whether to detour through enemies
(#5) or go to Plan 03. Item #1 is mandatory either way.

---

## D. Quick wins worth doing opportunistically
- **Grant all 7 weapons + bind keys 1–7** (≈30 min; the weapons already exist).
- **Fix the Nailgun fire-mode inconsistency.**
- **Stock all four ammo types** in the player loadout so the extra weapons are testable.
- **Render the `DamageFlash`** that's already tracked but invisible.

---

## E. Decision needed
The one strategic fork is **#5 Enemies vs going to TrenchBroom (Plan 03) sooner.**
Everything before #5 is cheap and uncontroversial; #5 is a multi-day effort that the
roadmap never formally slotted. Pick based on whether the next milestone you want is
"a level full of things to shoot" (do #5) or "levels I can build myself" (do Plan 03).
