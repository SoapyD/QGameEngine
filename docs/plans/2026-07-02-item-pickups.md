# 2026-07-02 — Item Pickups

**Status:** Phase 1 implemented — pickup on touch (health/ammo/armour/weapons), 5 demo items
in the showcase, headless green (8/8 inc. `pickup_health`), 2026-07-02. Phase 2 (respawn,
armour absorption, bob/spin, FGD) not started.
**Graduated from:** Plan 02 #2 (item pickups)
**Prereq for:** nothing hard; makes the showcase a playable loop and adds `item_*`/`weapon_*`
classnames the TrenchBroom FGD (Plan 03 §3.1) will need.
**Blocked by:** nothing — the entity-factory dispatch it builds on shipped 2026-07-02.

---

## Why this plan exists

You can already *lose* health (lava) and *spend* ammo (firing), but you can't get either back
by exploring — there are no item entities. That's the one missing half of the core FPS loop.
Pickups close it, and they're cheap: they reuse the trigger-overlap code and the `Health`/`Ammo`
components that already exist, and they slot into the `classname`→factory dispatch we just
shipped. Each new pickup type ends up being roughly one factory + one table entry.

They also double as Phase-5 prep: `item_health`, `item_shells`, `weapon_*` are exactly the
point-entity classnames the TrenchBroom FGD needs (Plan 03 §3.1), so building them now means
the map editor has something to place later.

---

## Goal / definition of done

1. Walking a `TagTriggerable` entity (the player) over a pickup grants its effect and removes
   the pickup from the world.
2. Pickup types: **health, shells, nails, rockets, cells, armour, and weapons**.
3. Grants respect limits — health/armour cap at their max; a weapon pickup adds the weapon to
   the inventory only if not already held (and tops up its ammo).
4. Pickups are spawned by `classname` (`item_health`, `item_shells`, …, `weapon_shotgun`)
   through the existing dispatch, and a few appear in the showcase.
5. The existing HUD reflects grants automatically (health bar / ammo counter already read the
   components — **no HUD changes**).
6. A headless scenario proves "walk onto a health pickup → health rises, pickup gone."

Out of scope for v1 (see Phase 2): respawning pickups, armour *damage absorption*, and bob/spin
visual polish. v1 grants on touch and removes the entity.

---

## Current state (what pickups reuse — don't rebuild)

| Piece | Where | Role for pickups |
|-------|-------|------------------|
| `Health{ current, max, invulnerableTimer }` | `components/gameplay.h` | health pickup target (cap at `max`) |
| `Ammo{ shells, nails, rockets, cells }` | `components/combat.h` | ammo pickup targets |
| `WeaponInventory{ weapons, currentWeapon }` + `createWeapon(WeaponType)` | `components/combat.h`, `weapon_definitions.h` | weapon pickup grants |
| `TagTriggerable` | `components/tags.h` | who can pick things up (player today; enemies could opt in) |
| AABB overlap: `AABB::fromCentreSize` + `intersects`, `view<Position, AABBCollider, TagTriggerable>` | `trigger_system.cpp:24-35` | the exact overlap loop `pickupSystem` copies |
| `AABBCollider(halfExtents, isSensor=true)` (ECS-only, no Jolt body) | `factories::spawnTrigger` | pickups are sensor volumes — overlap only, no physics body |
| classname→factory dispatch, `SpawnParams`, `spawnScene`, `showcaseDescriptors()` | `level/classname_factory.*`, `level/spawn_scene.*`, `level/showcase_descriptor.*` | where each `item_*` registers |
| Existing HUD health bar + ammo counter | `debug_hud/*` | already reads `Health`/`Ammo` — reflects grants for free |

The overlap pattern is worth copying verbatim from `triggerSystem`: build a box from the pickup,
build a box per `TagTriggerable` entity, `intersects()` → grant.

## The gap — what's missing

1. No **`Pickup` component** describing what an item gives.
2. No **`pickupSystem`** to detect overlap and apply the grant.
3. No **`Armor` component** (armour is a brand-new stat).
4. No **`spawnPickup` factory** and no **`item_*`/`weapon_*` classnames** in the dispatch.
5. No **pickups in the showcase** and no **regression scenario**.

---

## Design

### 1. `Pickup` component (`components/gameplay.h`)
```cpp
enum class PickupType { Health, Shells, Nails, Rockets, Cells, Armor, Weapon };

struct Pickup
{
    PickupType type = PickupType::Health;
    int        amount = 0;                       // shells/health/armour granted
    WeaponType weaponType = WeaponType::Shotgun; // used only when type == Weapon
    // Phase 2: float respawnDelay = 0.0f; (0 = permanent removal)
};
```
`PickupType` is part of the `Pickup` contract, so per the coding standard (§2 enum rule) it
lives with the component, next to `MoverState`/`TriggerAction`.

### 2. `Armor` component (`components/combat.h`, near `Health`/`Ammo`)
```cpp
struct Armor { float current = 0.0f; float max = 100.0f; };
```
Add `Armor` to the player in `spawnPlayer` (starts at 0). In v1 armour is just a stat a pickup
fills; **damage absorption is Phase 2** (it touches every damage path, like the Chapter-16
invuln guard did). Ship the number first, wire absorption second.

### 3. `pickupSystem` (`ecs/systems/pickup/pickup_system.{h,cpp}`)
Mirror `triggerSystem`'s overlap loop:
```
view<Position, AABBCollider, Pickup> pickups
view<Position, AABBCollider, TagTriggerable> receivers
for each pickup:
    for each receiver that intersects:
        applyPickup(registry, receiver, pickup)   // grant by type
        registry.destroy(pickupEntity)            // v1: permanent
        break
```
`applyPickup` switches on `PickupType`:
- **Health** → `health.current = min(current + amount, max)`
- **Shells/Nails/Rockets/Cells** → `ammo.<field> += amount`
- **Armor** → `armor.current = min(current + amount, max)`
- **Weapon** → if `WeaponInventory` has no weapon of `weaponType`, `weapons.push_back(createWeapon(weaponType))`; top up the matching ammo pool either way.

One free helper per concern; the system stays under the 120-line cap.

> **Why not fold pickups into `TriggerVolume` (add a `Pickup` action)?** A trigger *acts on the
> toucher and stays put*; a pickup *is consumed*. They share only the overlap test, not the
> lifecycle. A separate component keeps "grant then destroy" out of the trigger switch and lets
> pickups carry their own data (amount, weapon type) cleanly.

### 4. `spawnPickup` factory (`level/factories.{h,cpp}`)
```cpp
entt::entity spawnPickup(entt::registry& reg, const MeshAssets& a, glm::vec3 pos,
                         const Pickup& pickup, unsigned int textureId);
```
Builds: `Position`, small `Scale` (~0.4), `MeshRenderer` (cube, distinctive texture),
`AABBCollider(halfExtents, /*sensor*/ true)` (ECS overlap only — no Jolt body), and the
`Pickup`. Same shape as `spawnDecorBox` + a collider + the `Pickup`.

### 5. Classname dispatch (`level/classname_factory.cpp`)
Add thin `make_*` factories + table entries, each translating `SpawnParams` → `spawnPickup`:

| classname | PickupType | default amount | texture |
|-----------|-----------|----------------|---------|
| `item_health` | Health | 25 | `grid_green` |
| `item_shells` | Shells | 10 | `grid_orange` |
| `item_nails` | Nails | 25 | `grid_orange` |
| `item_rockets` | Rockets | 5 | `grid_red` |
| `item_cells` | Cells | 25 | `grid_blue` |
| `item_armor` | Armor | 50 | `grid_blue` |
| `weapon_shotgun` … `weapon_railgun` | Weapon | (ammo top-up) | `grid_grey` |

`amount` comes from `p.getInt("amount", <default>)`; `weapon_*` maps the classname suffix to a
`WeaponType`. This is the payoff from the dispatch refactor — adding a pickup type is one table
row + one 3-line factory.

### 6. Showcase + descriptors
Add a handful to `showcaseDescriptors()` so they're visible/testable — e.g. an `item_health`
near the lava, `item_shells` on the shelf, a `weapon_nailgun` in an alcove. (These are *extra*
demo content; they don't change existing entities.)

---

## Work breakdown (each step builds + harness green)

**Phase 1 — core loop (the deliverable)**
1. `Pickup` component + `PickupType` enum (`gameplay.h`); `Armor` component (`combat.h`);
   grant `Armor` in `spawnPlayer`.
2. `spawnPickup` factory.
3. `pickupSystem` (overlap → grant → destroy) + `applyPickup` helper.
4. Wire `pickupSystem` into the tick order (after `triggerSystem`, before `playerDeathSystem`);
   add its `.cpp` to CMake; call it in `stepSimulation`.
5. `item_*` / `weapon_*` classnames in the dispatch table.
6. Add a few pickups to `showcaseDescriptors()`.
7. Harness scenario `pickup_health` (walk onto a health pickup → health rises + entity gone);
   run the full suite.

**Phase 2 — polish (optional, follow-on)**
8. Respawn: `Pickup.respawnDelay`; on collect, hide (stash + remove `MeshRenderer`/collider,
   start a timer) and restore when it elapses, instead of `destroy`.
9. Armour *absorption*: armour soaks a fraction of incoming damage before health, in every
   damage path (`trigger_system` Damage, `combat_system` hitscan/projectile/splash) — same
   surgical pattern as the Chapter-16 invulnerability guard.
10. Visual flair: slow bob/spin on pickups (a tiny `pickup_bob_system` or reuse a rotation
    field) so they read as items, not props.
11. FGD entries for the `item_*`/`weapon_*` point entities (feeds Plan 03 §3.1).

Phase 1 is the whole gameplay win and is self-contained; Phase 2 items are independent and can
land any order.

## File plan (follows the coding standard)

| File | New/changed | Notes |
|------|-------------|-------|
| `ecs/components/gameplay.h` | changed | `Pickup` + `PickupType` |
| `ecs/components/combat.h` | changed | `Armor` |
| `ecs/systems/pickup/pickup_system.{h,cpp}` | new | overlap → grant → destroy |
| `level/factories.{h,cpp}` | changed | `spawnPickup` |
| `level/classname_factory.cpp` | changed | `item_*` / `weapon_*` factories + table rows |
| `level/showcase_descriptor.cpp` | changed | a few demo pickups |
| `app/simulation.cpp` | changed | `pickupSystem` in tick order |
| `harness/headless_main.cpp` | changed | `pickup_health` scenario |
| `CMakeLists.txt` | changed | `pickup_system.cpp` |
| `factories::spawnPlayer` (`factories.cpp`) | changed | emplace `Armor` |

## Behaviour / tick-order notes
- **Placement:** `pickupSystem` runs after `triggerSystem` and before `playerDeathSystem` —
  the player's `Position` is already current (post `playerCharacterSystem`), and a health grant
  lands before the death check.
- **Sensor collider:** pickups use `AABBCollider(halfExtents, /*sensor*/ true)` and get **no
  Jolt body** — they're pure ECS overlap, like triggers. The player walks through them.
- **Destroy-during-iteration:** collect `registry.destroy` calls into a small vector and destroy
  after the pickup loop (don't invalidate the view mid-iteration).
- **HUD:** no change — the Chapter-16 health bar and ammo counter read `Health`/`Ammo` live.

## Validation
- New scenario `pickup_health`: teleport the player onto an `item_health` placed at a known
  spot, step a few ticks, assert `Health.current` increased and no `Pickup` entity remains at
  that position.
- `spawn_counts` stays green (new demo pickups will change tallies — update its expected counts,
  or exclude `Pickup` entities from the assertion).
- All existing scenarios stay green (pickups are additive; existing entities untouched).

## Relationship to the other plans
- **Plan 02 #2** — this is that item. After it, #3 (graphical HUD polish) and #5 (enemies) are
  the remaining Track-1 gameplay work.
- **Plan 03 §3.1** — the `item_*` / `weapon_*` classnames become FGD point entities; the map
  loader spawns them through the same dispatch, no new engine work.
