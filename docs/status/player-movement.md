# Status: Player Movement

**State:** ✅ working · _verified 2026-06-14_

## Works
- Quake-style ground acceleration + friction, air control, bunny-hopping.
- Jump (only when `OnGround`), manual gravity while airborne.
- `CharacterVirtual.ExtendedUpdate` stair-stepping + floor-sticking via `stepHeight`.
- Ground state from `CharacterVirtual.GetGroundState()` (authoritative for player).

## Known gaps / risks
- Movement tuning constants (e.g. air speed 1.0, gravity −20) are literals in the
  system, not centralised config.
- No crouch / no death-stops-input handling (ties into death/respawn 🔴).

## Next
- Surface tuning constants into `CharacterPhysics` / config if movement feel needs
  iteration.

Process: [`../processes/player-movement.md`](../processes/player-movement.md)
