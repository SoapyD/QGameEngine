# Plan — Weapons & Ammo Wiring

**Graduated from** [archive/2026-06-08_next-features.md](2026-06-08_next-features.md) §C #4.
**State verified:** 2026-07-03 against source.
**Status:** ✅ Shipped 2026-07-03. Decision taken: **fixed 7-slot inventory** (`std::array<Weapon,7>` + `owned[7]`, indexed by `WeaponType`) so keys 1-7 always select the same weapon type. Player starts owning Shotgun + Rocket Launcher; the other 5 are collected via the existing showcase pickups. Per-pool ammo decrement + HUD were already correct; item #6 (showcase pickups) was already done. Updated `combat.h`, `factories.cpp`, `weapon_switch_system.h`, `combat_system.cpp`, `draw_ammo.cpp`, `pickup_system.cpp`, and two harness scenarios (`weapon_pickup`, `rocket_vs_floor`). All 11 headless scenarios green.

**Goal:** turn "all 7 weapons are *defined*" into "all 7 are *usable*" — a coherent
inventory, a sensible starting kit, and each weapon drawing from the correct ammo pool.

---

## Already done (don't redo)
- **All 7 weapon stat blocks** exist — [`weapon_definitions.h`](../../src/engine/ecs/weapon_definitions.h)
  (Shotgun, SuperShotgun, Nailgun, RocketLauncher, GrenadeLauncher, LightningGun, Railgun).
- **Nailgun fire-mode bug fixed** — now `FireMode::Projectile` (was mislabelled `Hitscan`).
- **Keys 1–7 bound** — [`player_input_system.cpp`](../../src/engine/ecs/systems/player/player_input_system.cpp)
  writes `weaponSwitch = 0..6`.
- **`weaponSwitchSystem`** switches the active weapon and plays `weapon.switch`.
- **All 7 `weapon_*` + 4 `item_*` (shells/nails/rockets/cells) + health/armor pickup
  factories** exist and are registered — [`classname_factory_items.cpp`](../../src/engine/level/classname_factory_items.cpp).
- **Ammo pools** are `{ shells, nails, rockets, cells }` — [`components/combat.h`](../../src/engine/ecs/components/combat.h).

## The actual gap
The player is handed only **2 weapons + 2 ammo pools** —
[`factories.cpp` `spawnPlayer`](../../src/engine/level/factories.cpp): Shotgun + RocketLauncher,
`Ammo(25, 0, 5, 0)`. `WeaponInventory.weapons` is a **dynamic vector**, but keys 1–7 index it
directly — so keys 3–7 select nothing, and if weapons are collected in-world the key→weapon
mapping depends on pickup order.

## Work items
| # | Task | Notes |
|---|------|-------|
| 1 | **Decide the inventory model** | Fixed 7-slot (key *N* always selects weapon type *N*, empty until owned) vs. the current dynamic vector. Fixed-slot is the standard Quake feel and makes keys 1–7 stable. **Recommended: fixed 7-slot.** |
| 2 | **Starting loadout** | Keep Shotgun only (collect the rest) *or* grant all 7 for testing behind a debug flag. Pickups already exist, so "start minimal, collect the rest" is viable now. |
| 3 | **Per-pool ammo decrement** | Confirm `combatSystem` decrements the *correct* pool per weapon (shotguns→shells, nailgun→nails, RL/GL→rockets, LG/railgun→cells). Add a `WeaponType → AmmoType` mapping if missing. This is the highest-risk item — verify by firing each. |
| 4 | **Block firing on empty pool** | + optional dry-fire click (audio deferred in the audio plan). |
| 5 | **HUD ammo shows the active weapon's pool** | [`draw_ammo.cpp`](../../src/engine/ecs/systems/debug_hud/draw_ammo.cpp) — confirm it reflects the current weapon, not a fixed pool. |
| 6 | **Place weapon/ammo pickups in the showcase** | So the extra 5 weapons are reachable in-game (descriptor list). |

## Verification
- Fire each of the 7 weapons; assert the right ammo pool decrements and hits/splash behave.
- Add a headless scenario: grant all weapons + ammo, cycle 1–7, fire once each, assert pools.

## Doubles as
FGD prep for [2026-07-03_trenchbroom_engine-loader.md](2026-07-03_trenchbroom_engine-loader.md)
— the `weapon_*` / `item_*` classnames are the entity-mapping targets.
