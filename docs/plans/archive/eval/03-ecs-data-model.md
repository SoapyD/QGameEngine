# 03 — ECS Data Model

**Status:** Evaluated.
**Scope:** [components.h](../../../../src/engine/ecs/components.h), `system_phase.h`, `weapon_definitions.h`.
**Part of:** [README.md](README.md).

## Findings

| # | Issue | Sev | Detail | Action | Location |
|---|-------|-----|--------|--------|----------|
| 3.1 | `stepHeight` doc divergence | P2 | Code default **0.7**; [JOLT_PHYSICS.md:156](../../../architecture/JOLT_PHYSICS.md#L156) says 1.5. | Fix doc (cross [05 §7](05-physics.md)). | [components.h:90](../../../../src/engine/ecs/components.h#L90) |
| 3.2 | `Gravity` component unused | P2 | Player gravity is hardcoded `-20` in the player system; dynamic bodies use Jolt's world gravity. The `Gravity{strength=20}` component is emplaced on player + cubes but **no system reads it**. | Remove, or wire it as the single source of gravity truth (ties to [05 §6](05-physics.md)). | [components.h:71](../../../../src/engine/ecs/components.h#L71) |
| 3.3 | `Velocity` on player vestigial | P2 | Player velocity lives in the `CharacterVirtual`. The `Velocity` component on the player (emplaced in scene_setup) is never read/written for the player (`joltSyncSystem` only touches `JoltBody`, player has `JoltCharacter`). | Drop from player, or document as unused. | [components.h:42](../../../../src/engine/ecs/components.h#L42) |
| 3.4 | `OnGround` on cubes vestigial | P2 | Set on dynamic cubes via `joltSyncSystem`'s `abs(velY)<0.5` heuristic, but nothing consumes cube ground state. | Remove from cubes, or document. | [components.h:76](../../../../src/engine/ecs/components.h#L76) |
| 3.5 | `CharacterPhysics` on cube3 misleading | P2 | scene_setup sets `groundFriction=1.5` on the sliding cube, but no system applies `CharacterPhysics` to non-player bodies — friction actually comes from Jolt. The tuning value is dead. | Remove the misleading set, or implement per-body friction. | [scene_setup.cpp:237](../../../../src/engine/ecs/scene_setup.cpp#L237) |
| 3.6 | `AABBCollider.layer/mask` possibly unused | P2 | Jolt uses its own object layers; the ECS `layer`/`mask` fields may no longer drive anything post-Jolt. | Verify no reader; remove if dead. | [components.h:67-68](../../../../src/engine/ecs/components.h#L67-L68) |
| 3.7 | `system_phase.h` is documentation-only | P2 | The `SystemPhase` enum is explicitly "not for runtime dispatch" — tick order is hardcoded in `main`. Fine, but it can drift from reality (it already omits death/weapon-switch, see [08](08-integration.md)). | Keep, but treat as doc; reconcile with actual order. | [system_phase.h:24](../../../../src/engine/ecs/systems/system_phase.h#L24) |

## Theme
The data model carries several **post-Jolt vestigial components** (`Gravity`, player `Velocity`, cube `OnGround`, cube `CharacterPhysics`, maybe `AABBCollider.layer/mask`). None are bugs, but they mislead anyone reasoning about the systems. 

## Graduates to a fix plan
Batch 3.2–3.6 into `docs/plans/component-cleanup.md` — but only **after** confirming each has no reader (some interact with the physics fixes; sequence this after [05](05-physics.md)). 3.1 folds into the physics doc fix.
