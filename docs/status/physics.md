# Status: Physics (Jolt Integration & Sync)

**State:** ✅ working · _verified 2026-06-14_

## Works
- Jolt body types in use: static, dynamic, kinematic, `CharacterVirtual`, sensor.
- Pre-step `moverSyncSystem` (ECS→Jolt) and post-step `joltSyncSystem` (Jolt→ECS)
  ordering is correct.
- `demoResetSystem` respawns demo cubes (resets ECS + Jolt body state).
- Body creation centralised in `jolt_body_helpers` (5 factory functions).

## Known gaps / risks
- Sensor bodies are created for triggers but not queried (triggers use ECS AABB).
- `OnGround` for dynamic bodies is a velocity heuristic (cubes only); the player
  uses the authoritative `CharacterVirtual` ground state.
- Legacy physics (`spatial_hash`, `collision`, `aabb`, `collision_layers`) only
  feed `archived/` systems — candidates for removal.

## Next
- Decide whether to delete legacy physics helpers + archived systems.

Process: [`../processes/physics.md`](../processes/physics.md)
