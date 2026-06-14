# Archived systems — DEAD CODE (not compiled)

These files are **not** in the CMake build (`qengine_lib` does not list them) and
are **not** referenced by any live system. They are the pre-Jolt implementations,
kept only as tutorial reference for the "old way vs new way" comparison.

| Archived system | Superseded by |
|-----------------|---------------|
| `collision_system` | Jolt rigid-body solver + `CharacterVirtual::ExtendedUpdate` |
| `physics_system` | Jolt gravity + `CharacterVirtual` (player) / Jolt bodies (objects) |
| `movement_system` | Jolt body simulation + `joltSyncSystem` |
| `player_movement_system` | `playerCharacterSystem` |

Do not extend these. If they become a maintenance burden, delete the folder —
nothing live depends on it. See [docs/processes/physics.md](../../../../../docs/processes/physics.md)
→ "Legacy & retained code".
