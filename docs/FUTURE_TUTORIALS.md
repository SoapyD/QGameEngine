# Future Tutorials

Tutorials that need to be written but don't yet exist. These cover concepts introduced illustratively in existing chapters that were deferred for later implementation.

---

## Advanced Movement

**Source**: Chapter 10 (Physics & Movement) — stair stepping section marked as "Concept — Future Chapter"

**What it covers**:
- Stair stepping — gliding up small height changes without jumping
- Uses swept AABB tests to try "move up, move forward, move back down"
- Requires a walking player entity with collision

**Prerequisites**:
- Player entity with Position, Velocity, AABBCollider, OnGround
- Swept AABB collision (Chapter 9)
- Ground detection system (Chapter 10)

**Why it was deferred**: The implementation requires multiple swept AABB checks in sequence and a player entity that walks around. Chapter 10 only has a fly camera, so there's nothing to walk up stairs with yet.

---

## Cached Uniform Locations

**Source**: Chapter 10a (Game Loop & Physics Cleanup) — performance note in the Multiple Point Lights section

**What it covers**:
- Cache `glGetUniformLocation` results once after shader compilation instead of calling per-frame
- A uniform cache struct or map keyed by uniform name, populated on shader load
- Eliminates string concatenation and hash lookups inside the draw loop

**Prerequisites**:
- Multi-point-light rendering (Chapter 10a)
- Shader class that knows its own program ID

**Why it was deferred**: The per-frame cost of `glGetUniformLocation` with string concatenation is negligible for a small number of lights and draw calls. It becomes a real concern when the scene has many objects and many lights. Best addressed as part of a broader rendering cleanup.

**Likely home**: Cleanup 30a (Rendering Pipeline Cleanup) — already lists "shader cache" and "draw call batching audit" as targets.

---

## NPC Trigger Interaction

**Source**: Chapter 11 (Doors, Lifts & Triggers) — trigger system currently filters by `TagPlayer` only

**What it covers**:
- Generalise the trigger system so NPCs (and potentially projectiles) can activate triggers
- Options: collision layers on `AABBCollider` (layer/mask filtering already stubbed), a `TagTriggerable` component, or per-trigger allowlists
- Decide which trigger actions NPCs should be able to activate (doors yes, teleporters maybe, damage zones yes, level changes no)

**Prerequisites**:
- Trigger system with `TagPlayer` filter (Chapter 11)
- NPC entities with Position, AABBCollider, and AI behaviour
- Collision layer system (already has `layer` and `mask` fields on `AABBCollider` but trigger system doesn't use them yet)

**Why it was deferred**: Chapter 11 has no NPCs — the only non-player collider entities are physics demo cubes, which were incorrectly triggering doors. Filtering by `TagPlayer` was the simplest correct fix. Generalising requires NPC entities to exist first.

---
