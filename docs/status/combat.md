# Status: Combat (Weapons)

**State:** ✅ working · _verified 2026-06-14_

## Works
- `weaponSwitchSystem` resolves weapon change before firing each tick.
- Shotgun hitscan: ray vs `Level` + ray vs `AABBCollider`, instant damage, tracer.
- Rocket launcher projectile: spawned entity with `Velocity` + `Lifetime`.
- Cooldowns + ammo gating; `lifetimeSystem` expires tracers/projectiles.
- Projectile behaviour fixed in the eval/fix bundle (shipped 2026-06-08).

## Known gaps / risks
- Only two weapons (shotgun, rocket); stats inline in `weapon_definitions.h`.
- No reload, no muzzle/impact effects beyond the tracer.
- Damage feeds `Health`; death/respawn is handled separately by
  `player_death_system` (player only — no enemy death, since no enemies exist yet).

## Next
- Add weapons via the `createWeapon()` factory as needed.

Process: [`../processes/combat.md`](../processes/combat.md)
