# 08 — Integration (`main.cpp`)

**Status:** Evaluated.
**Scope:** [src/main.cpp](../../../src/main.cpp) — tick order, init/shutdown, ownership, the input→ECS and camera-follow glue.
**Part of:** [README.md](README.md).

## Findings

| # | Issue | Sev | Detail | Action | Location |
|---|-------|-----|--------|--------|----------|
| 8.1 | **Tick order — player before movers/physics** | P0 | `playerCharacterSystem` runs at line 187, but movers move (189-190) and the physics step runs (191) *after* it. The player's `ExtendedUpdate` therefore resolves against the lift's **previous-tick** position every tick → the lift glitch. This is where the [05 §2](05-physics.md) reorder decision is applied. | Reorder per the physics fix (movers + kinematic pre-step before the character update, or character update after `step()`). | [main.cpp:184-199](../../../src/main.cpp#L184-L199) |
| 8.2 | No render interpolation | P1 | The fixed-tick positions are rendered raw; `getAlpha()` is unused. At high FPS, movers/player render in discrete steps that **look like** physics jitter. Worth ruling out before chasing simulation jitter. Cross [02 §2.5](02-core.md), [05 §9](05-physics.md). | Implement alpha interpolation for rendered transforms, or document the limitation. | [main.cpp:214-224](../../../src/main.cpp#L214-L224) |
| 8.3 | TICK_ORDER.md divergence | P2 | The doc lists 10 systems and omits `weaponSwitchSystem` and `playerDeathSystem`; the actual loop runs 11 in a different grouping. | Update [TICK_ORDER.md](../../architecture/TICK_ORDER.md) to match. | [main.cpp:186-198](../../../src/main.cpp#L186-L198) |
| 8.4 | Camera dir as bare `glm::vec3` in ctx | P2 | `registry.ctx().insert_or_assign<glm::vec3>(camera.getFront())` (set twice, lines 181 & 212) is read by combat as `ctx().get<glm::vec3>()`. Unkeyed by type — any other `glm::vec3` ctx use collides. | Wrap in a named struct. Cross [07 §7.5](07-gameplay-systems.md). | [main.cpp:181](../../../src/main.cpp#L181) |
| 8.5 | Dead Jolt sensor bodies | P2 | `createSensorBody` is called for every trigger (93-101) but `triggerSystem` uses ECS AABB overlap, so the sensor bodies do nothing. Cross [05 §8](05-physics.md). | Remove sensor creation, or migrate triggers to Jolt sensor queries. | [main.cpp:93-101](../../../src/main.cpp#L93-L101) |
| 8.6 | Fire input bypasses InputManager | P2 | Mouse-fire is read via `glfwGetMouseButton` directly in `main`, while keyboard goes through `InputManager`. Inconsistent. | Route mouse buttons through `InputManager`. | [main.cpp:170-174](../../../src/main.cpp#L170-L174) |
| 8.7 | Duplicated eye-height magic | P2 | `halfExtents.y * 0.7f` appears in the camera-follow (208) and again in combat fire origin. | Single named constant. | [main.cpp:207](../../../src/main.cpp#L207) |
| 8.8 | Inline logic that should be systems | P2 | wishDir build, PlayerInput population, camera follow all live inline in the loop. | Optional: extract to systems for testability. | [main.cpp:146-209](../../../src/main.cpp#L146-L209) |
| 8.9 | `HudeConfig` typo | P3 | Misspelled local. | Rename. | [main.cpp:111](../../../src/main.cpp#L111) |

## Graduates to a fix plan
8.1 is the headline — it belongs in `docs/plans/physics-fixes.md` (same reorder that fixes the lift). 8.2 + render interpolation is its own task. 8.4/8.5/8.6/8.7 batch into a small `docs/plans/integration-cleanup.md`. 8.3 is a doc fix bundled with the physics-doc corrections.
