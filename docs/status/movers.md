# Status: Movers (Doors & Lifts)

**State:** ✅ working · _verified 2026-06-14_

## Works
- Full state machine: Idle → StartDelay → Moving → Waiting → Returning → Idle.
- `MoveKinematic` pushes the player and dynamic bodies (correct pre-step sync).
- Lift behaviour fixed in the eval/fix bundle (shipped 2026-06-08).

## Known gaps / risks
- Movers activate only via [triggers](triggers.md) — no other activation source.
- Single linear path (start→end); no multi-waypoint or rotation movers.

## Next
- Optional: rotation/multi-waypoint movers if level design needs them.

Process: [`../processes/movers.md`](../processes/movers.md)
