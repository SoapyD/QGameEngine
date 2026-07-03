# 2026-07-02 — Entity-Factory Refactor: classname → factory dispatch

**Status:** ✅ Shipped & archived — steps 1–7 complete, headless harness green (7/7 inc. `spawn_counts`), 2026-07-02. Tutorial written up as Chapter 18.
**Prereq for:** Plan 02 #2 (pickups), #5 (enemies); Plan 03 step 2.0/2.3 (TrenchBroom `.map` loader)
**Blocked by:** nothing — can start now.

---

## Why this plan exists (reconciling Plan 02 #1 with reality)

Plan 02 #1 describes the entity-factory refactor as *"turn the inline spawns in
`scene_setup.cpp` into `spawnDoor/spawnLift/spawnTrigger/spawnLight/…`."* **That part is
already done.** The `factories::` namespace
([factories.h](../../src/engine/level/factories.h) /
[factories.cpp](../../src/engine/level/factories.cpp)) shipped 2026-06-14 and
[scene_setup.cpp](../../src/engine/app/scene_setup.cpp) already builds the whole showcase
through `factories::spawnPlayer / spawnPointLight / spawnMover / spawnTrigger / …`.

So the *literal* task is complete, but the *purpose* it was meant to serve is not. Those
factories are **not yet dispatchable by data.** Each has a bespoke positional signature and
takes `MeshAssets` + raw GL texture ids threaded from the call site. A `.map` loader
(Plan 03) produces entities described by a **string `classname` + a key/value property
bag + a texture *name*** — it cannot call today's factories generically.

**This plan closes that gap:** a uniform spawn interface + a `classname → factory` dispatch
table, validated by rebuilding the showcase from an in-code *descriptor list* (the same
shape the `.map` parser will emit) — proving the data-driven path works **before** the
parser exists.

---

## Goal / definition of done

1. A `classname` (string) can be dispatched to a factory that builds the full entity from a
   uniform `SpawnParams`, with no call-site knowledge of which components are involved.
2. `scene_setup.cpp` builds the showcase by iterating a **descriptor list** through that
   dispatch table — not by hand-calling each typed factory.
3. Texture names (not GL ids) and `targetname`/`target` links (not raw `entt::entity`) are
   resolved *inside* the load path, via a two-pass build (spawn all → link targets).
4. **Runtime behaviour is byte-for-byte the showcase we ship today** — verified by the
   existing headless harness scenarios (doors open, lift boards, teleport, lava damage,
   player spawns on ground).
5. Adding a new spawnable type (pickup, enemy, new light) is *register one classname +
   one factory* — no changes to `scene_setup` or the loader.

Explicitly **not** in scope: the `.map` parser, brush→mesh, brush collision, the FGD file
(all Plan 03, Part 2). This plan makes those *hookable*; it does not build them.

---

## Current state (what already exists — reuse, don't rebuild)

| Piece | Where | Keep as-is? |
|-------|-------|-------------|
| Low-level typed factories (`spawnPlayer`, `spawnPointLight`, `spawnStaticBox`, `spawnDemoCube`, `spawnMover`, `spawnTrigger`, `spawnDecorBox`, `spawnDebugWireframe`, dir light) | `level/factories.{h,cpp}` | **Yes** — these become the *implementation* each classname factory calls into. |
| `MeshAssets` (shared cube VAO / index count / shader) | `ecs/types/mesh_assets.h` | Yes — folds into the spawn context. |
| Two-phase kinematic spawn: `setupScene` attaches `Mover`; `buildWorld` creates the Jolt kinematic body *after* `OptimizeBroadPhase` by viewing all `Mover`s | `app/simulation.cpp:55-69` | **Yes — critical.** Data-driven spawns get kinematic bodies for free as long as they run before that view. Preserve the ordering. |
| Trigger→mover wiring by direct `entt::entity` handle | `scene_setup.cpp:88-94` | Replaced by `targetname` two-pass linking. |

---

## The gap — what's missing to serve as a prereq

1. **No uniform spawn interface.** Signatures are all different (`spawnMover` takes 9
   positional args; `spawnTrigger` takes 8). A loader can't call these blind.
2. **No `classname → factory` table.** Step 2.3 of Plan 03 needs exactly this.
3. **Render context is threaded by hand.** `MeshAssets` + texture ids are resolved in
   `scene_setup` and passed into every call. A data-driven factory receives a texture
   *name string* and must resolve it itself.
4. **`target` links are raw handles.** `.map` uses `target`/`targetname` string linking,
   which requires a two-pass load (spawn everything, then resolve names → entities). No
   such pass exists.
5. **Composite showcase objects have no single classname.** A showcase "door" today =
   `spawnMover` + `spawnTrigger` + `spawnDebugWireframe`, wired inline. In TrenchBroom
   terms these are *separate* entities (`func_door` + `trigger_multiple`) linked by
   `targetname` — so the refactor splits them cleanly rather than inventing a mega-factory.

---

## Design

### 1. `SpawnParams` — the uniform descriptor (`level/types/spawn_params.h`)
The shape the `.map` parser will emit, and what factories consume:

```cpp
struct SpawnParams {
    std::string classname;                              // "func_door", "light", …
    glm::vec3   origin{0.0f};                           // entity origin / position
    glm::vec3   halfExtents{0.5f};                      // collider / brush size
    std::string targetname;                             // this entity's name (may be empty)
    std::string target;                                 // name it links to (may be empty)
    std::unordered_map<std::string, std::string> props; // raw key/values (speed, wait, dmg, _color, texture, …)
    // typed getters with defaults: getFloat("speed", 3.0f), getVec3("_color", …), getString("texture", "grid_grey")
};
```

A `SpawnContext` bundles the render/asset side factories need but map data doesn't carry:
`MeshAssets` + a **texture resolver** (`std::function<unsigned int(std::string_view name)>`,
backed by `ResourceManager`) so a factory can turn `"grid_orange"` → GL id.

### 2. Classname factory signature
```cpp
using SpawnFn = entt::entity(*)(entt::registry&, const SpawnContext&, const SpawnParams&);
```
Each classname factory reads its params, resolves its texture, and delegates to the existing
`factories::spawn*` implementation. Example:
```cpp
// classname "func_door"
entt::entity spawn_func_door(registry& r, const SpawnContext& ctx, const SpawnParams& p) {
    return factories::spawnMover(r, ctx.assets, p.origin, doorEnd(p /*angle+lip/height*/),
        scaleFromExtents(p.halfExtents), p.halfExtents,
        p.getFloat("speed", 3.0f), p.getFloat("wait", 4.0f), p.getFloat("startdelay", 0.0f),
        ctx.texture(p.getString("texture", "grid_orange")));
}
```

### 3. Dispatch table (`level/classname_factory.{h,cpp}`)
A `classname → SpawnFn` registry with `spawn(registry, ctx, params)` that looks up and
invokes, and a `registerFactory(name, fn)`. Registration is a single init function so
adding a type is one line. Classnames to register for the current showcase:

| classname | Delegates to | Showcase element |
|-----------|--------------|------------------|
| `info_player_start` | `spawnPlayer` | player |
| `light` | `spawnPointLight` (+ marker) | ceiling/torch lights |
| `light_environment` | dir-light factory | the sun |
| `func_static` | `spawnStaticBox` | shelf |
| `prop_dynamic` | `spawnDemoCube` | demo cubes |
| `func_door` | `spawnMover` (horizontal/any-axis) | door |
| `func_plat` | `spawnMover` (vertical) | lift |
| `trigger_multiple` | `spawnTrigger` (ActivateMover, via `target`) | door/lift trigger |
| `trigger_teleport` | `spawnTrigger` (Teleport, dest from linked point) | teleporter |
| `trigger_hurt` | `spawnTrigger` (Damage, `dmg`/sec) | lava |
| `info_teleport_destination` | marker-only entity carrying an origin | teleport target |
| `func_decor` | `spawnDecorBox` | poles, lava surface |
| _(debug wireframes)_ | `spawnDebugWireframe` | editor-only; likely dropped from map path |

Leaves obvious seams for Plan 02: `item_health` / `item_shells` / `item_rockets` /
`weapon_*` (pickups) and `monster_grunt` (enemy) register the same way once those factories
land.

### 4. Two-pass load + `targetname` linking (`level/spawn_scene.{h,cpp}`)
```
pass 1: for each SpawnParams → entity = dispatch.spawn(...); record targetname → entity
pass 2: for each spawned entity with a `target` → resolve to entity, patch the link
        (TriggerVolume.target = named mover; trigger_teleport.destination = named point's origin)
```
This is the mechanism Plan 03 §3.2 calls for; building it now means the parser just feeds
`SpawnParams` in.

### 5. `scene_setup.cpp` becomes descriptor-driven
Add `level/showcase_descriptor.{h,cpp}` returning `std::vector<SpawnParams>` that encodes
today's exact showcase (same positions/speeds/textures). `setupScene` builds the
`SpawnContext`, calls `spawnScene(registry, ctx, descriptors)`, and returns the level. The
level *geometry* loop (sectors → MeshRenderer) and `CombatResources` context stay where
they are for now (they belong to the brush-mesh / map-wireup steps in Plan 03).

**This is the validation:** the descriptor list is a hand-written stand-in for parser
output, so a green headless run proves the full data path before any `.map` code exists.

---

## Work breakdown (each step leaves the build runnable + harness green)

1. **`SpawnParams` + `SpawnContext` types** with typed getters and the texture resolver.
   No behaviour change. Build.
2. **Classname factories** — one thin `spawn_<classname>` per row above, each delegating to
   the existing `factories::spawn*`. Unit-free; pure translation. Build.
3. **Dispatch table + registration** (`classname_factory`). Build; still unused.
4. **Two-pass `spawnScene`** with `targetname`/`target` resolution. Build; still unused.
5. **`showcase_descriptor`** encoding the current scene 1:1.
6. **Rewrite `setupScene`** to build `SpawnContext` + call `spawnScene(descriptors)`; delete
   the inline typed-factory calls. **Run the headless harness — must match today exactly.**
7. **Docs:** update `docs/status/_overview.md` (factory layer now data-driven) and add a note
   in Plan 03 that step 2.0/2.3 is satisfied — the parser feeds `SpawnParams`.

Steps 1–5 are additive (zero risk); step 6 is the only behavioural swap and is fully guarded
by the harness.

---

## File plan (follows the C++ coding standard)

| File | New/changed | Notes |
|------|-------------|-------|
| `level/types/spawn_params.h` | new | incidental types → `types/` (per standard §2) |
| `level/classname_factory.{h,cpp}` | new | dispatch table (grouped, like `factories.*`) |
| `level/spawn_scene.{h,cpp}` | new | two-pass loader |
| `level/showcase_descriptor.{h,cpp}` | new | in-code stand-in for parser output |
| `level/factories.{h,cpp}` | **unchanged** | reused as the implementation layer |
| `app/scene_setup.cpp` | changed | descriptor-driven; inline calls removed |

Keep `factories.*` as the existing grouped file (matches the cap-table entry
`engine/ecs/factories*.cpp`); the new classname wrappers are grouped the same way.

## Behaviour-preservation notes (don't regress these)
- **Mover kinematic bodies** are still created in `buildWorld` by viewing `Mover`s *after*
  `OptimizeBroadPhase` — `spawnScene` must run inside `setupScene` (before that view), which
  it does. Don't create kinematic bodies in the factory.
- **Trigger cooldowns / values** (lava 25/sec no-cooldown, door/lift cooldowns) must survive
  the props → `TriggerVolume` translation — cover them in the descriptor.
- **Player loadout** (2 weapons, 25 shells / 5 rockets, spawn at 15,1.7,15) is encoded by
  `info_player_start` + `spawnPlayer` as today.

## Validation
- Existing headless scenarios (door, lift/boarding, teleport, lava, spawn-on-ground) must
  pass unchanged — this is the acceptance gate for step 6.
- Add one scenario: **"spawn showcase from descriptors, assert entity counts per classname"**
  to lock the data path (extends the harness per Plan 03 §Regression safety).

## Relationship to the other plans
- **Plan 02 #1** — this *is* that item, corrected for what already shipped. Once done, #2
  (pickups) and #5 (enemies) each become "add a classname + factory."
- **Plan 03 steps 2.0 / 2.3** — satisfied. The `.map` parser (2.1) emits `SpawnParams`;
  entity mapping (2.3) is just registering the FGD classnames against these factories.
