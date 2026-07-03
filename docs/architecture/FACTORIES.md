# QEngine — Entity Factories

How entities get built. There are **two layers**: typed `spawn*` factories (one per entity
kind) and a `classname`→factory **dispatch** layer that lets data (a descriptor list today, a
TrenchBroom `.map` tomorrow) name entities by string. This is the contract the Phase-5 `.map`
loader targets (Plan 03 step 2.3).

> Shipped 2026-07-02 (entity-factory + classname dispatch). Keeps `scene_setup.cpp` *declarative*
> (what is in the scene) rather than *mechanical* (how each entity is assembled).

---

## Layer 1 — typed factories (`level/factories.h`)

One free function per spawnable entity type, in `namespace factories`. Each builds the entity's
full component set and returns its handle:

| Factory | Builds |
|---------|--------|
| `spawnPlayer` | collider, movement, `Health`, `SpawnPoint`, weapons + ammo, `Armor`, damage-feedback, `TagPlayer`/`TagTriggerable` |
| `spawnDirectionalLight` | the sun (`DirectionalLight`) |
| `spawnPointLight` | a `PointLight` + its small debug-cube marker |
| `spawnStaticBox` | rendered box + Jolt **static** body |
| `spawnDemoCube` | rendered cube + Jolt **dynamic** body + `DemoReset` |
| `spawnMover` | rendered box + `Mover` (kinematic body created later in `buildWorld`) |
| `spawnTrigger` | a `TriggerVolume` (activate / teleport / damage / heal) |
| `spawnDebugWireframe` | green `GL_LINE` box visualising a trigger |
| `spawnDecorBox` | decorative box, no physics |
| `spawnPickup` | rendered cube + sensor AABB carrying a `Pickup` (ECS overlap only, no Jolt body) |

**Physics note:** factories needing a static or dynamic body create it immediately. Kinematic
bodies for movers are created later in `buildWorld()` (after the broad-phase is first optimised),
so `spawnMover` only attaches the `Mover`/render components.

---

## Layer 2 — classname dispatch (`level/classname_factory.h`)

```cpp
entt::entity spawnByClassname(entt::registry& reg, const SpawnContext& ctx,
                              const SpawnParams& p);
```

Looks up `p.classname` and delegates to the typed factory, translating `SpawnParams` (origin,
angle, keys) into the factory's arguments. Returns `entt::null` for an unknown classname (logged
to stderr) so the caller keeps spawning the rest.

Item and weapon classnames are registered in a dispatch table
(`classname_factory_items.cpp` → `registerItemClassnames`):

- `item_health`, `item_shells`, `item_nails`, `item_rockets`, `item_cells`, `item_armor`
- `weapon_shotgun`, `weapon_supershotgun`, `weapon_nailgun`, `weapon_rocketlauncher`,
  `weapon_grenadelauncher`, `weapon_lightninggun`, `weapon_railgun`

Each maps to a `make_*` that builds a `Pickup` via `spawnPickup`.

---

## Two-pass `targetname` linking

Entities that reference each other (a `trigger_multiple` → its `func_door`) can't resolve on the
first pass because the target may not exist yet. The build runs **two passes**: spawn every entity
recording its `targetname`, then resolve each `target` string to the `entt::entity` it names. This
is why triggers can activate movers declared anywhere in the descriptor/map.

---

## Where the scene comes from

`setupScene` builds the showcase from an **in-code descriptor list**
(`level/showcase_descriptor.cpp`) run through `spawnByClassname` — already `.map`-loader-shaped.
Plan 03's loader replaces the descriptor source with parsed `.map` entities; the factory and
two-pass linking layers stay unchanged.
