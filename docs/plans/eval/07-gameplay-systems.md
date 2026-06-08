# 07 — Gameplay Systems

**Status:** Evaluated.
**Scope:** `ecs/systems/{combat_system,weapon_switch_system,lifetime_system,trigger_system,player_death_system,demo_reset_system}`, `physics/raycast.*`, `weapon_definitions.h`.
**Part of:** [README.md](README.md).

## Findings

| # | Issue | Sev | Detail | Action | Location |
|---|-------|-----|--------|--------|----------|
| 7.1 | **Teleport trigger doesn't move the player** | P1 (functionally P0) | `TriggerAction::Teleport` sets `entPos.value` and zeroes the `Velocity` component. But the player is a `CharacterVirtual` whose position is written from `character->GetPosition()` every tick — so next tick **overwrites** the teleport, and the character was never actually moved. The teleporter in the showcase is dead. `playerDeathSystem` does it correctly (`character->SetPosition`) — mirror that. | When the target is the player, call `character->SetPosition` + `SetLinearVelocity`. | [trigger_system.cpp:63-72](../../../src/engine/ecs/systems/trigger_system.cpp#L63-L72) vs [player_death_system.cpp:36-39](../../../src/engine/ecs/systems/player_death_system.cpp#L36-L39) |
| 7.2 | **Projectiles pass through level geometry** | P1 | Projectile collision iterates `view<Position, AABBCollider>` — level surfaces aren't entities (see [06 §6.1](06-level-and-scene.md)), so rockets fly through walls/floors and only detonate on entities. Hitscan handles level surfaces explicitly; projectiles don't. | Test projectiles against level geometry (Jolt cast, or the same surface loop hitscan uses). | [combat_system.cpp:402-481](../../../src/engine/ecs/systems/combat_system.cpp#L402-L481) |
| 7.3 | Two collision truths | P2 | Combat raycasts use the **legacy AABB** system (`raycast.h`) against ECS colliders + a manual surface loop — not Jolt. Rotated/again-synced bodies are approximated by axis-aligned boxes. Divergence risk as the engine leans on Jolt. | Decide long-term: migrate combat to Jolt queries, or document the split. | [combat_system.cpp:34-67](../../../src/engine/ecs/systems/combat_system.cpp#L34-L67) |
| 7.4 | Unfinished trigger actions | P2 | `ChangeLevel` and `Message` are no-ops (TODO). | Implement or remove from the enum until supported. | [trigger_system.cpp:116-127](../../../src/engine/ecs/systems/trigger_system.cpp#L116-L127) |
| 7.5 | Combat camera-dir coupling | P2 | Firing direction comes from `registry.ctx().get<glm::vec3>()` — a bare, unkeyed `glm::vec3` in context set by `main`. Fragile; any other `glm::vec3` ctx value would collide, and a missing value throws. | Wrap in a named struct (e.g. `CameraDirection{glm::vec3}`). Cross [08](08-integration.md). | [combat_system.cpp:360](../../../src/engine/ecs/systems/combat_system.cpp#L360) |
| 7.6 | Triggers only test the player | P2 | `triggerSystem` iterates `TagPlayer` only; non-player entities can't trip triggers. Fine for the showcase; a limitation to note. | Document; generalise if needed. | [trigger_system.cpp:27](../../../src/engine/ecs/systems/trigger_system.cpp#L27) |
| 7.7 | `rayIntersectsTriangle` dead | P2 | Defined, never called. | Remove (tracked in [01 §1.4](01-build-and-dead-code.md)). | [raycast.cpp:54](../../../src/engine/physics/raycast.cpp#L54) |
| 7.8 | Magic knockback/damage constants | P3 | Knockback `*1.0/1.6/2.0`, eye offset `0.7` scattered. | Hoist to named config. | combat_system.cpp |

**Correct as-is:** `playerDeathSystem` (proper `SetPosition`), `demoResetSystem` (proper `SetPosition`+velocity on the Jolt body), `weaponSwitchSystem` (bounds-checked), splash falloff, cooldown-gated DPS via `value*dt`.

## Graduates to a fix plan
- 7.1 → `docs/plans/teleport-fix.md` (small, high value — restores a visibly broken feature).
- 7.2 (+ [06 §6.1]) → `docs/plans/projectile-collision-fix.md`.
- 7.3 → a design note, not an immediate fix; revisit when combat needs rotated-shape accuracy.
