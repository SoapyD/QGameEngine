# Status: Triggers (Trigger Volumes)

**State:** ✅ working (one action stubbed) · _verified 2026-06-14_

## Works
- ECS AABB overlap detection against the player.
- Actions: activate mover, teleport (+ zero velocity), damage, heal, message.
- Cooldown + `triggered` flag prevent re-fire spam.

## Known gaps / risks
- 🟡 `changeLevel` action is recorded but **not wired** — no level-swap path exists
  (ties into TrenchBroom/level-loading work, 🔴).
- Overlap uses ECS AABB, not Jolt's sensor contact listener (sensor bodies exist
  but are unused) — fine today; revisit if trigger shapes get complex.

## Next
- Implement `changeLevel` once level loading exists.
- Consider moving to the Jolt sensor contact listener for non-AABB trigger shapes.

Process: [`../processes/triggers.md`](../processes/triggers.md)
