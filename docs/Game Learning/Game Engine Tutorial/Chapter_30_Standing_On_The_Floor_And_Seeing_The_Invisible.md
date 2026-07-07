# Chapter 30: Standing on the Floor and Seeing the Invisible — Feet-Origin Placement and a Trigger Debug View

## What You'll Learn
- Why a `monster_grunt` you rest **on the floor** in TrenchBroom spawns **half-buried** in the running
  game — the mismatch between a mapper's *feet-origin* and the factory's *centre-origin*, and how the
  coordinate transform compounds it
- The **loader-side fix**: a `groundHalfHeight(cls)` helper in `map_to_descriptors.cpp` that lifts a
  floor-standing actor by its collider half-height on import — and the careful reasoning for why the
  correction lives in the **loader** and not in the factory
- Writing a **regression scenario** (`scenarioMapGroundPlacement`, the `map_ground` test) that loads a
  real `.map`, runs the descriptor conversion, and asserts every grunt's feet land exactly on its
  authored floor
- Why **trigger volumes are invisible** — `trigger_multiple`/`trigger_hurt`/`trigger_teleport` are brush
  entities with no drawn geometry — and a **debug view** that makes them visible on demand
- Reusing the engine's **existing wireframe machinery** (the `TagDebugWireframe` factory and the
  green-`colorOverride` + `glPolygonMode(GL_LINE)` render path) to draw a wireframe **twin** of each
  trigger's AABB, auto-spawned on `.map` load
- The **`DebugRenderConfig` context singleton**, gating those wireframes in the render system, and the
  **F1 edge-detected toggle** in `main.cpp` — including why edge detection is what makes one press flip
  the view exactly once
- The connecting insight: a trigger's **height** is gameplay — the lava `trigger_hurt` launches the
  player upward every tick they're inside it — so *seeing* whether a volume is too thin is a real
  authoring tool

---

## Where We Are

Chapters 26–28 built the TrenchBroom `.map` pipeline: an editor, a parser, and a loader that turns
brushes into a walkable room and entities into spawns through the same `factories::spawnScene` the
hard-coded showcase uses. Chapter 29 then rebuilt the *entire* showcase arena as an authored
`assets/maps/showcase.map` — doors, lifts, teleporters, lava, the arsenal, and two grunts — proving the
pipeline can carry a real level.

Authoring that map surfaced two rough edges, and this chapter is engine code that files them both down.
Neither is a new feature so much as making hand-authored levels *behave correctly*:

- **Part A** — a grunt you rest on the floor in the editor spawned sunk into it. The fix is a small,
  surgical lift on the loader's import path, plus a headless test that guards it.
- **Part B** — a trigger volume is invisible, so you can't tell, from inside the game, where an
  activate/hurt/teleport zone actually is or how tall it is. The fix is a debug view: a green wireframe
  twin of every trigger, toggled with **F1**.

Everything below is grounded in the real files changed this session:
`src/engine/level/map_to_descriptors.cpp`, `src/engine/level/spawn_monster.cpp`,
`src/harness/map_scenarios.{h,cpp}`, `src/harness/headless_main.cpp`,
`src/engine/ecs/components/core.h`, `src/engine/level/factories.cpp`,
`src/engine/app/scene_setup_map.cpp`, `src/engine/ecs/systems/render/render_system.cpp`,
`src/engine/ecs/systems/trigger/trigger_system.cpp`, and `src/main.cpp`.

---

## Part A — Actors Stand on the Floor

## Step 1: The Symptom — a Floor-Placed Grunt Spawns Half-Buried

Chapter 29 authored the two grunts with their origin **on the floor** (`z = -80`, the floor surface):

```
// entity 14
{
"classname" "monster_grunt"
"origin" "-176 368 -80"
}
```

That's the natural thing to do in a level editor: you drop the entity, drag it down until its box rests
on the floor, and move on. But launch the map and the grunt spawns **sunk half-way into the floor** — its
waist at floor level, its feet below it. Nothing else looks wrong; the room, the lights, the pickups are
all fine. Only the actor is buried.

The cause is a disagreement about what an "origin" *means*. Look at how the factory spawns a grunt —
`src/engine/level/spawn_monster.cpp`:

```cpp
    entt::entity spawnMonsterGrunt(entt::registry& reg, const MeshAssets& a, glm::vec3 pos)
    {
        auto e = reg.create();
        reg.emplace<Position>(e, pos);
        reg.emplace<Rotation>(e, glm::vec3(0.0f));
        reg.emplace<Scale>(e, glm::vec3(0.8f, 1.8f, 0.8f));          // humanoid-ish box
        reg.emplace<AABBCollider>(e, glm::vec3(0.4f, 0.9f, 0.4f), false); // solid: shootable + blocks
        reg.emplace<MeshRenderer>(e, cubeRenderer(a, 0u));           // untextured — coloured below
        reg.emplace<Colour>(e, glm::vec4(0.75f, 0.15f, 0.15f, 1.0f)); // red, via renderSystem albedo
        reg.emplace<Health>(e, 50.0f, 50.0f, 0.0f);
        reg.emplace<DamageFlash>(e, 0.0f, 0.12f);   // brief white blink when shot (renderSystem)
        reg.emplace<AIState>(e);
        reg.emplace<AIPath>(e);
        // No TagTriggerable: player-only volumes (lava, teleporters) must not
        // affect the grunt. It is NOT given PendingKnockback either (kinematic
        // bodies ignore impulses — knockback is deferred to the behaviour plan).
        return e;
    }
```

Two lines are the whole story. `Position = pos` puts the entity's **centre** at `pos`, and the
`AABBCollider` has **half-extents `(0.4, 0.9, 0.4)`** — so the box reaches `0.9` engine units *below* the
centre. In other words, the factory treats the spawn point as the body **centre**, and the feet sit at
`centre.y − 0.9`.

But a mapper doesn't place a centre. A mapper rests the *box on the floor*, which puts the authored
origin at **floor level — the feet**. Feed a feet-origin into a factory that reads it as a centre-origin,
and the whole actor drops by its half-height: the feet end up `0.9` below the floor and the centre ends
up *on* it. That's exactly the half-buried grunt.

The coordinate transform is what makes the number concrete. Recall `mapPointToEngine` from Chapter 28
(`src/engine/level/map_transform.h`):

```cpp
    // A world POSITION: axis-swap + scale.
    inline glm::vec3 mapPointToEngine(glm::vec3 m)
    {
        return glm::vec3(m.x, m.z, -m.y) / kMapUnitsPerEngineUnit;
    }
```

The grunt's authored `z = -80` (up, in map space) becomes the engine-space *Y* (up), divided by 32 — a
floor at roughly `y = -2.5` engine units. The loader drops the grunt's centre onto that floor, and its
feet fall `0.9` beneath it. The player sees a grunt buried to the waist.

> **Why is the factory's centre-origin the "right" convention, given the mapper wants a feet-origin?**
> Because the factory serves *both* the map loader and the in-code showcase, and the showcase authors
> centre origins directly — a hand-written `SpawnParams{ .origin = glm::vec3(...) }` for a grunt is its
> centre, and every existing headless AI scenario places grunts by centre too. The factory is also
> physics-facing: a body's `Position` is its centre, its `AABBCollider` is symmetric about that centre,
> and the character controller, raycasts, and knockback all reason about the centre. Changing the factory
> to treat its argument as feet would silently move *every* existing grunt in the engine up by `0.9` and
> break all of that. The centre convention is correct for the engine; it's only the *map import path*
> that speaks a different dialect (feet), and a dialect mismatch is fixed at the boundary — not by
> rewriting the language on one side.

---

## Step 2: The Fix — Lift a Floor-Standing Actor on Import

The mismatch is a boundary problem, so it gets a boundary fix: in the loader, as it converts a map
entity into a `SpawnParams`, raise a floor-standing actor's origin by its collider half-height, so the
feet-origin the mapper drew becomes the centre-origin the factory expects. The whole correction is a tiny
lookup helper and a single line, both in `src/engine/level/map_to_descriptors.cpp`.

The helper — a per-class table of "how far up is this actor's centre above its feet?":

```cpp
    // Lift a floor-standing point actor from its authored feet-origin up to the
    // centre-origin the factories expect (value = the actor's collider half-Y).
    float groundHalfHeight(const std::string& cls)
    {
        if (cls == "monster_grunt") return 0.9f;  // mirrors spawnMonsterGrunt
        return 0.0f;
    }
```

It returns `0.9` for a `monster_grunt` — deliberately the *same* number as the factory's `AABBCollider`
half-Y — and `0.0` for everything else, which means the lift is a no-op for any class that isn't a
floor-standing actor. Then, inside the entity loop, right after the origin is resolved (from a brush AABB
for brush entities, or from the `origin` key for point entities), one line applies it:

```cpp
        // Brush entities derive origin+size from their AABB; points read `origin`.
        glm::vec3 mn, mx;
        if (entityBrushAABB(e, mn, mx))
        {
            p.origin = (mn + mx) * 0.5f;
            p.size   = mx - mn;
        }
        else if (e.has("origin"))
        {
            std::istringstream ss(e.getString("origin"));
            glm::vec3 m(0.0f);
            if (ss >> m.x >> m.y >> m.z)
                p.origin = qmap::mapPointToEngine(m);
        }

        // Authored feet-origin → the body-centre origin the factories expect, so
        // a floor-placed grunt stands on the floor instead of sinking half-in.
        p.origin.y += groundHalfHeight(cls);
```

That's it. `p.origin` is already in engine space (`mapPointToEngine` ran on the way in), so the lift is a
straight `+= 0.9` on the engine-space Y. A grunt authored with feet at floor level now has its *centre*
`0.9` above the floor, and the factory drops its box so the feet land exactly on the floor. For every
other class, `groundHalfHeight` returns `0` and nothing moves — the pickups at `z = -64` (authored to
float just off the floor) and the door/lift/trigger brushes are all untouched.

> **Why does the lift live in `map_to_descriptors.cpp` — the loader — rather than in `spawnMonsterGrunt`
> — the factory?** Because the correction must apply to *exactly one* path: the `.map` import. The
> factory is shared by three callers, and only one of them (the map loader) delivers a feet-origin. The
> in-code showcase (`showcase_descriptor.cpp`) authors grunt *centres* directly and already spawns them
> correctly; the headless AI scenarios place grunts by centre too. Put the `+0.9` in the factory and you
> lift *those* grunts as well — every showcase grunt and every test grunt jumps up half a body, breaking
> the scenarios that pin their positions. Putting it in the loader confines the fix to the one dialect
> that needs translating, and leaves the factory's centre-origin contract — which everything else relies
> on — completely intact.
>
> There's a second reason it belongs here and not, say, in a hand-edited offset baked into the `.map`
> file: **it survives re-saving.** If you "fixed" the placement by nudging the grunt's authored origin up
> by hand in the text, the next time a designer opens the map in TrenchBroom, drags that grunt, and saves,
> the editor rewrites the origin back to wherever the box rests — on the floor — and the bug returns. The
> loader-side lift is applied fresh on every import, so it holds no matter how many times the map is
> re-authored. The rule the mapper learns (Chapter 29) — *rest a grunt on the floor and it stands on the
> floor* — is true precisely because this line makes it true, every load.

---

## Step 3: A Regression Test That Guards the Feet

A one-line fix is exactly the kind of thing a later refactor silently undoes — someone reorders the loop,
or "cleans up" the helper, and grunts quietly sink again with no compile error. So the fix ships with a
headless scenario that *asserts* the feet land on the floor. It's pure data — no GL, no registry, no
physics — living in `src/harness/map_scenarios.cpp` as `scenarioMapGroundPlacement`:

```cpp
    bool scenarioMapGroundPlacement(const std::string& path)
    {
        std::string err;
        qmap::MapData map = qmap::loadMapFile(path, &err);
        auto descriptors = mapEntitiesToDescriptors(map);

        // Mirror of spawnMonsterGrunt's AABBCollider half-Y and the loader's lift.
        constexpr float kGruntHalfY = 0.9f;
        constexpr float kEps        = 0.01f;

        int grunts = 0, sunk = 0;
        float worstFeetGap = 0.0f;   // signed: negative = feet below authored floor

        for (const auto& e : map.entities)
        {
            if (e.classname() != "monster_grunt" || !e.has("origin")) continue;
            ++grunts;

            // Authored ground point (feet target) in engine space.
            std::istringstream ss(e.getString("origin"));
            glm::vec3 m(0.0f);
            ss >> m.x >> m.y >> m.z;
            const float floorY = qmap::mapPointToEngine(m).y;

            // The matching descriptor must place the body CENTRE a half-height above
            // that ground, so the feet (centre − halfY) land exactly on the floor.
            const glm::vec3 wantCentre = qmap::mapPointToEngine(m) + glm::vec3(0.0f, kGruntHalfY, 0.0f);
            bool matched = false;
            for (const auto& d : descriptors)
            {
                if (d.classname != "monster_grunt") continue;
                if (glm::length(d.origin - wantCentre) < kEps) { matched = true; break; }
            }

            const float feetY = matched ? (wantCentre.y - kGruntHalfY) : (floorY - kGruntHalfY);
            const float gap   = feetY - floorY;
            if (std::fabs(gap) > kEps) { ++sunk; }
            if (std::fabs(gap) > std::fabs(worstFeetGap)) worstFeetGap = gap;
        }

        bool ok = err.empty() && grunts > 0 && sunk == 0;

        char buf[220];
        std::snprintf(buf, sizeof(buf),
            "grunts=%d, sunk/misplaced=%d, worst feet gap=%.3f (want ~0), err=\"%s\"",
            grunts, sunk, worstFeetGap, err.c_str());
        return report("map_ground", ok, buf);
    }
```

Walk the logic per grunt:

1. **Read the authored floor.** The grunt's `origin` key is its feet, in map space; `mapPointToEngine`
   gives `floorY`, the engine-space height its feet should end up at.
2. **Compute the expected centre.** A correctly-lifted grunt has its *centre* `kGruntHalfY` above that
   floor — `wantCentre`.
3. **Find the matching descriptor.** Run the *real* `mapEntitiesToDescriptors` (the code under test), and
   look for a grunt descriptor whose `origin` sits within `kEps` of `wantCentre`. If the loader's lift is
   working, there is one; if the lift is missing or wrong, `matched` stays false.
4. **Measure the feet gap.** From the matched centre, the feet are `centre.y − kGruntHalfY`, which should
   equal `floorY` exactly — a `gap` of zero. Any grunt off by more than `kEps` counts as `sunk`.

The scenario passes only when the map parsed, it found at least one grunt, and **none** are sunk. It's
registered like the other map scenarios — declared in `src/harness/map_scenarios.h`:

```cpp
    // Ground-placement check: load a `.map` and assert every floor-standing actor
    // (monster_grunt) is lifted from its authored feet-origin to a centre-origin,
    // so it stands ON the floor rather than sinking in by half its height.
    bool scenarioMapGroundPlacement(const std::string& path);
```

and wired into the dispatch in `src/harness/headless_main.cpp` alongside the Chapter 27–28 map
scenarios:

```cpp
    else if (scenario == "map_scene")        pass = mapscenarios::scenarioMapScene();
    else if (scenario == "map_ground")       pass = mapscenarios::scenarioMapGroundPlacement(mapArg);
```

The `mapArg` is the harness's second argument (defaulting to `assets/maps/smoke.map`), so pointing the
test at the showcase — which has the two grunts — runs it against real content. The actual output:

```
[PASS] map_ground — grunts=2, sunk/misplaced=0, worst feet gap=0.000
```

Two grunts found, zero sunk, worst feet gap `0.000` — the feet land dead on the authored floor.

> **Why test the descriptor's origin instead of just spawning a grunt and reading its physics body's
> height?** Because the bug lives entirely in the *conversion*, and the conversion is pure data — a
> `MapData` in, a `vector<SpawnParams>` out — with no need for a GL context, a Jolt world, or a running
> tick. Testing at that layer makes the scenario fast, deterministic, and precise: it asserts the exact
> arithmetic that was wrong (feet-origin → centre-origin), with no physics settling, no frame timing, and
> no window to stand up. The test *mirrors* the two magic numbers (`kGruntHalfY = 0.9` and the lift) on
> purpose, so if either the factory's half-height or the loader's lift changes and they fall out of step,
> the `worst feet gap` moves off zero and the scenario fails immediately — pinning the invariant "a
> floor-authored grunt stands on the floor" as a single, checkable number.

---

## Part B — Seeing the Invisible: a Trigger Debug View

## Step 4: The Symptom — Triggers Have No Geometry

A trigger is a brush entity with **behaviour but no visible geometry**. `trigger_multiple` (opens a
door), `trigger_hurt` (the lava), and `trigger_teleport` (moves you across the room) are all authored as
brushes textured `__TB_empty` — the loader never renders them, because a trigger is a sensor, not a wall
(Chapter 29, Step 4). That's correct: you don't want to *see* a lava-damage volume as a solid box.

But it means that from inside the running game you have **no idea where a trigger is** or, crucially, how
*tall* it is. The hard-coded showcase papered over this by hand-placing `_wireframe` boxes over its
trigger zones — little visible outlines a programmer added in C++. An *authored* `.map` (Chapter 29) has
none: a designer draws a `trigger_hurt` brush, saves, runs, and the lava's hurt volume is completely
invisible. Did the trigger end up where you meant? Is it tall enough to catch a jumping player? You can't
tell.

So we add a debug view that draws a wireframe **twin** of every trigger, toggled on and off with **F1**.
It reuses machinery the engine already has, and the switch that governs it is one tiny context struct in
`src/engine/ecs/components/core.h`:

```cpp
// Debug-visualisation toggles (stored in registry context). renderSystem reads
// these; the game loop flips them from key input. `showTriggerVolumes` draws the
// otherwise-invisible activate/hurt/teleport zones as green wireframe boxes.
struct DebugRenderConfig
{
	bool showTriggerVolumes = true;
};
```

A single `bool`, stored in the registry's **context** (the engine's home for scene-wide singletons — one
per registry, not per entity), defaulting to `true` so the wireframes are visible the moment a map loads.
The render system reads it; the game loop flips it. Everything else in Part B hangs off this flag.

> **Why a dedicated `DebugRenderConfig` struct in the registry context instead of a global `bool` or a
> render-system member?** Because the flag has two owners on opposite sides of the frame — the game loop
> *writes* it (from a keypress) and the render system *reads* it — and the registry context is exactly
> the engine's channel for that kind of shared, scene-lifetime state (it's where `HudConfig`,
> `CameraDirection`, and `CombatResources` already live). A free-floating global would work but sits
> outside the ECS's ownership model, can't be reset per-scene, and couples the two systems through a
> back-channel. Wrapping it in a named struct also leaves room to grow: the next debug toggle
> (nav-grid overlay, collision boxes) is another `bool` in the same struct, read the same way, with no
> new plumbing.

---

## Step 5: Auto-Spawning a Wireframe Twin per Trigger

The view is built at scene-setup time: as `setupSceneFromMap` finishes wiring the `.map`, it walks every
trigger and spawns a green wireframe box the same size as the trigger's collider. The key move is that it
**reuses the exact machinery the trigger system itself uses** — the same `<Position, AABBCollider,
TriggerVolume>` view — so a wireframe appears over precisely the entities that are triggers, no more, no
less. From `src/engine/app/scene_setup_map.cpp`, right after `spawnScene`:

```cpp
    // ─── Debug: wireframe twin per trigger volume ────────────────
    // The showcase hand-placed `_wireframe` boxes over its trigger zones; a
    // `.map` has none, so its activate/hurt/teleport volumes are invisible.
    // Spawn a green wireframe box matching each trigger's AABB (shown/hidden by
    // the DebugRenderConfig toggle). Collect first, then spawn — creating
    // entities mid-view would touch the Position pool we're iterating.
    if (!headless)
    {
        unsigned int wireTex = resources.getTexture("grid_green")->getId();
        std::vector<std::pair<glm::vec3, glm::vec3>> zones;  // centre, full-size
        for (auto [e, pos, col, trig] : registry.view<Position, AABBCollider, TriggerVolume>().each())
            zones.emplace_back(pos.value, col.halfExtents * 2.0f);
        for (const auto& [centre, size] : zones)
            factories::spawnDebugWireframe(registry, ctx.assets, centre, size, wireTex);
    }
```

Three details earn their place:

- **It iterates the trigger view.** `registry.view<Position, AABBCollider, TriggerVolume>()` is the same
  set of components the trigger *system* iterates every tick — anything with a `TriggerVolume` is, by
  definition, a trigger. So the debug pass can't drift out of sync with what's actually a trigger: it
  reads the identical view.
- **It sizes each box to the collider's AABB.** The trigger's `AABBCollider` stores half-extents;
  `col.halfExtents * 2.0f` is the full box size, and `pos.value` its centre. The wireframe is an exact
  outline of the sensor volume — same position, same dimensions — so what you see *is* the trigger, at
  true size and true height.
- **It collects into a vector first, then spawns.** The loop reads `(centre, size)` pairs into `zones`
  while iterating the view, and only *after* the view loop ends does it call `spawnDebugWireframe`.
  That ordering is deliberate, and the comment says why: creating entities inside the loop would add to
  the `Position` pool that the view is *currently iterating*, which can invalidate the iteration
  mid-flight. Collect, close the view, then spawn — a standard EnTT safety pattern.

The wireframe itself is not new code — it's an existing factory, `spawnDebugWireframe` in
`src/engine/level/factories.cpp`:

```cpp
    entt::entity spawnDebugWireframe(entt::registry& reg, const MeshAssets& a, glm::vec3 pos,
                                     glm::vec3 scale, unsigned int textureId)
    {
        auto e = reg.create();
        reg.emplace<Position>(e, pos);
        reg.emplace<Scale>(e, scale);
        reg.emplace<MeshRenderer>(e, cubeRenderer(a, textureId));
        reg.emplace<TagDebugWireframe>(e);
        return e;
    }
```

It's an ordinary cube-mesh entity carrying one extra marker: **`TagDebugWireframe`**. That tag is the
handle the render system keys off — both to draw the entity as an outline instead of a solid, and to
hide it when the toggle is off. The whole "wireframe twin" is a normal render entity plus a tag; all the
special behaviour lives in the render system reading that tag.

> **Why build a *separate* wireframe entity per trigger rather than teaching the trigger entity itself to
> render as an outline?** Because a trigger entity deliberately has no renderable geometry — that's the
> point of it — and giving it a `MeshRenderer` would entangle "is this a sensor?" with "how is this
> drawn?", two concerns the engine keeps apart. A separate twin keeps the trigger pure (sensor only) and
> the wireframe pure (a tagged debug mesh the render system already knows how to draw). It also means the
> debug view is *additive and disposable*: the twins are extra entities you can show, hide, or (in
> principle) tear down without touching the triggers that actually drive gameplay. Reusing the existing
> `spawnDebugWireframe`/`TagDebugWireframe` path — the same one the hard-coded showcase used for its
> hand-placed boxes — means we added a *policy* ("one twin per trigger, auto-derived from the map"), not
> a new rendering mechanism.

---

## Step 6: Gating the Wireframes in the Render System

The twins exist as entities the moment the map loads; whether they *draw* is decided each frame by the
render system, reading `DebugRenderConfig`. From `src/engine/ecs/systems/render/render_system.cpp`, just
before the mesh loop:

```cpp
	// ─── Draw meshes ─────────────────────────────────────────────
	// Trigger wireframes show only when the debug toggle is on (no config → on).
	bool showTriggers = !registry.ctx().contains<DebugRenderConfig>()
		|| registry.ctx().get<DebugRenderConfig>().showTriggerVolumes;

	auto meshView = registry.view<Position, MeshRenderer>();

	for (auto [entity, pos, mesh] : meshView.each())
	{
		const bool isWireframe = registry.all_of<TagDebugWireframe>(entity);
		if (isWireframe && !showTriggers) continue;
```

`showTriggers` is `true` when there's no `DebugRenderConfig` in the context **or** when there is one and
its `showTriggerVolumes` is set — so a scene that never installed the config (a headless run, or the
in-code showcase) still shows its wireframes, and only an *explicit* `false` hides them. Then, per entity,
`isWireframe` tests for the `TagDebugWireframe` marker, and `if (isWireframe && !showTriggers) continue;`
skips drawing a wireframe twin while the toggle is off. A non-wireframe entity is never affected by the
flag — the gate only touches tagged debug meshes.

The *look* of a wireframe — how the same cube entity draws as a green outline instead of a solid box — is
the existing debug-render path, further down the same loop:

```cpp
		// Flat colour override for debug wireframes (+ enemy hit flash)
		loc = glGetUniformLocation(mesh.shaderId, "colorOverride");
		if (isWireframe)
			glUniform4f(loc, 0.0f, 1.0f, 0.0f, 1.0f);  // bright green
```

and, around the draw call itself:

```cpp
		if (isWireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

		glBindVertexArray(mesh.vao);
		// … glDrawElements / glDrawArrays …

		if (isWireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);  // restore fill
```

Two OpenGL calls do the work. `colorOverride` set to bright green makes the shader paint the box a flat
colour instead of sampling its texture, and `glPolygonMode(GL_FRONT_AND_BACK, GL_LINE)` tells the
rasteriser to draw only the *edges* of each triangle — an outline — for the duration of that one draw,
then `GL_FILL` restores solid rendering for everything after. The result is a green wireframe cube exactly
outlining the trigger's volume.

> **Why gate the wireframes with an early `continue` in the draw loop rather than not spawning them, or
> destroying them when the toggle turns off?** Because a toggle wants the entities to *persist* — flip F1
> off and on and the same twins reappear instantly, with no rebuild, no re-walking the trigger view, no
> churn in the registry. Spawning them once at load and skipping their draw when hidden makes the toggle
> effectively free: the only cost of "off" is one `all_of<TagDebugWireframe>` check and a `continue` per
> tagged entity per frame, which is nothing. Destroying and respawning on every toggle would be more
> code, more allocation, and would throw away the twins' derivation from the trigger view each time. The
> `no config → shown` default in the same expression is what keeps *other* code paths (headless, the
> in-code showcase) rendering their wireframes unchanged — the gate adds a switch without changing the
> default behaviour of anything that predates it.

---

## Step 7: The F1 Toggle — Edge-Detected in the Game Loop

The last piece is the input that flips the flag. `main.cpp` installs a default `DebugRenderConfig` at
startup and, each frame, checks **F1** — but with a twist that matters. First the setup, from
`src/main.cpp`:

```cpp
	// F1 toggles trigger-volume wireframes; default on (visible on load).
	registry.ctx().emplace<DebugRenderConfig>();
	bool prevToggleKey = false;
```

`emplace<DebugRenderConfig>()` constructs it with its default (`showTriggerVolumes = true`), so the
wireframes are on when the game starts. `prevToggleKey` remembers whether F1 was held *last* frame — the
state that makes edge detection possible. Then, in the input section of the frame loop:

```cpp
		// F1 (edge-detected): toggle trigger-volume debug wireframes.
		bool toggleKey = input.isKeyPressed(GLFW_KEY_F1);
		if (toggleKey && !prevToggleKey)
			registry.ctx().get<DebugRenderConfig>().showTriggerVolumes ^= true;
		prevToggleKey = toggleKey;
```

Read it carefully. `toggleKey` is whether F1 is down *this* frame. The flip happens only when
`toggleKey && !prevToggleKey` — F1 is down **now** and was **up last frame**, i.e. the exact frame the
key transitions from released to pressed (its *rising edge*). The flip itself is `showTriggerVolumes ^=
true`, an XOR-assign that inverts the bool: `true` becomes `false`, `false` becomes `true`. Finally
`prevToggleKey = toggleKey` records this frame's state for the next one.

The edge detection is the point. `isKeyPressed` returns true for *every frame the key is held* — and a
key is physically held for many frames even on a quick tap (at 60fps, a 100ms press is six frames). Flip
the flag on the raw `isKeyPressed` and a single tap would toggle the view six times, landing on whichever
state the release frame happens to hit — a coin-flip. Requiring the *rising edge* (`down now && up last
frame`) means the flag flips exactly **once per physical press**, no matter how long you hold F1. Press
once, the wireframes appear; press again, they vanish. Deterministic, one press per toggle.

> **Why hand-roll edge detection with a `prevToggleKey` bool instead of asking the input layer for a
> "key just pressed this frame" query?** Because the engine's `Input` exposes level state — "is this key
> down?" — not edge events, and for a one-off debug toggle the cheapest correct thing is to remember one
> bool across frames and compare. It's the same pattern every debounced toggle in a game loop uses:
> `pressed-this-frame && !pressed-last-frame` is the definition of a rising edge, and two lines of
> bookkeeping express it exactly. Reaching for a heavier event system would be over-engineering a single
> F1 key; the local `prevToggleKey` is self-contained, obviously correct, and lives right next to the
> code that uses it.

---

## Step 8: Why This Matters — Trigger Height Is Gameplay

The debug view isn't just tidiness; it exposes a property of triggers that's easy to get wrong when you
can't see it: **height**. A trigger's vertical extent is *gameplay*, and the lava is the clearest case.
Look at the `Damage` branch of the trigger system, `src/engine/ecs/systems/trigger/trigger_system.cpp`:

```cpp
				case TriggerAction::Damage:
				{
					// Armour absorbs first, then health (see applyDamage).
					if (applyDamage(registry, entity, trigger.value * dt))
					{
						// Knockback: push player upward out of lava
						if (registry.all_of<PendingKnockback>(entity))
							registry.get<PendingKnockback>(entity).impulse += glm::vec3(0.0f, 1.0f, 0.0f);
					}
					break;
				}
```

Every tick the player stands in a `trigger_hurt`, `applyDamage` bites, and — while they're inside — a
`+1` upward impulse accumulates into their `PendingKnockback`. The effect is that lava *launches* the
player up and out. But how big the launch is depends on **how many ticks the player spends inside the
volume**, and that is a function of the volume's **height**: a lava trigger that's a thin sliver gives the
player one or two ticks of upward push — a feeble pop — before they've cleared the top. A taller lava
volume keeps them inside for more ticks, accumulating a bigger impulse, and throws them properly clear.

That is exactly the property you cannot judge from a `.map` without seeing the box. A designer draws a
`trigger_hurt` brush, and whether it's a satisfying launch or a limp nudge comes down to a dimension
that's invisible in-game — until now. Toggle F1 and the lava's hurt volume is a green wireframe box you
can *look at*: if it's a paper-thin slab hugging the floor, that's why the knockback feels weak, and the
fix is to make the brush taller. The debug view turns an invisible tuning parameter into something you
can see and iterate on, which is the whole reason it's worth building.

> **Why is the upward knockback tied to *dwell time inside the volume* rather than a single fixed impulse
> on entry?** Because it makes the hazard's feel *emerge from its geometry* instead of from a magic
> number. A per-tick push means a bigger, taller lava pit is inherently more dangerous-feeling — you're
> in it longer, so it flings you further — and a shallow puddle is a mild deterrent, all without the
> designer touching a single tuning value: they just draw the brush bigger or smaller. It's the same
> data-driven philosophy the whole map pipeline runs on (geometry *is* the gameplay), and it's precisely
> why a debug view of trigger *height* pays off — the shape you author is the behaviour you get, so being
> able to see the shape is being able to see the behaviour.

---

## What Changed — Summary

| File | Change |
|------|--------|
| `engine/level/map_to_descriptors.cpp` | **Part A** — new `groundHalfHeight(cls)` helper (`0.9` for `monster_grunt`, else `0`) and one line `p.origin.y += groundHalfHeight(cls)` after origin resolution: lifts a floor-authored feet-origin to the body-centre origin the factories expect, on the `.map` import path only. |
| `harness/map_scenarios.{h,cpp}` | **Part A** — new `scenarioMapGroundPlacement(path)` (`map_ground`): loads a `.map`, runs `mapEntitiesToDescriptors`, and asserts each grunt's feet land on its authored floor (worst feet gap ~0, zero sunk). |
| `harness/headless_main.cpp` | **Part A** — registers the `map_ground` scenario in the dispatch (`… pass = mapscenarios::scenarioMapGroundPlacement(mapArg);`). |
| `engine/ecs/components/core.h` | **Part B** — new `DebugRenderConfig { bool showTriggerVolumes = true; }` context singleton: the render/input toggle for trigger-volume wireframes. |
| `engine/app/scene_setup_map.cpp` | **Part B** — after `spawnScene`, walks the `<Position, AABBCollider, TriggerVolume>` view, collects each trigger's centre + full size into a vector, then spawns a green `spawnDebugWireframe` twin per trigger (collect-then-spawn to avoid mutating the pool mid-view; non-headless only). |
| `engine/ecs/systems/render/render_system.cpp` | **Part B** — computes `showTriggers` from `DebugRenderConfig` (absent config → shown) and skips drawing `TagDebugWireframe` entities when the toggle is off; the existing green `colorOverride` + `glPolygonMode(GL_LINE)` path draws the outline. |
| `src/main.cpp` | **Part B** — `emplace<DebugRenderConfig>()` at startup (default on) and an F1 rising-edge toggle (`toggleKey && !prevToggleKey` → `showTriggerVolumes ^= true`), so one press flips the view once. |
| `engine/ecs/systems/trigger/trigger_system.cpp` | (Context for Step 8) the `TriggerAction::Damage` case adds a `+1` upward `PendingKnockback` impulse per tick a player is inside a `trigger_hurt` — the reason a lava volume's height matters. |

Both changes are surgical and additive: Part A is one helper plus one line on the loader's import path
(and a test), and Part B reuses the existing `TagDebugWireframe` render path — no new rendering
mechanism, just a policy that spawns a twin per trigger and a flag that gates them.

---

## What You Should See

Run `build/QEngine.exe assets/maps/showcase.map`:

1. **The grunts stand on the floor.** The two `monster_grunt`s authored with their origin on the floor
   (`z = -80`) now stand *on* the floor instead of sunk to the waist — because the loader lifted their
   feet-origin to the centre-origin the factory expects.
2. **Trigger volumes are visible.** On load, every `trigger_multiple`, `trigger_hurt`, and
   `trigger_teleport` shows a green wireframe box outlining its exact volume — the door/lift activate
   zones, the lava hurt volume, the teleport pad — at true position and true height.
3. **F1 toggles them.** Press F1 and the green wireframes vanish; press it again and they return. One
   flip per press, however long you hold the key.
4. **The lava's height reads as gameplay.** Stand in the `trigger_hurt` and it launches you upward; the
   wireframe shows you how tall the volume is, which is why the launch is as strong (or weak) as it is —
   a thin volume, a small pop; a taller one, a bigger launch.

Headless:

5. **`QEngineHeadless map_ground assets/maps/showcase.map` passes** — printing
   `[PASS] map_ground — grunts=2, sunk/misplaced=0, worst feet gap=0.000`, pinning the feet-on-floor
   invariant.
6. **`map_parse`, `map_file`, and `map_scene` still pass** (Chapters 27–28) — the ground fix and the
   debug view touched neither the parser nor the geometry conversion.

---

## What's Next

Hand-authored levels now *behave* the way a designer intends: rest an actor on the floor and it stands on
the floor, and the invisible logic of a level — its trigger volumes — can be made visible on demand to
check and tune. Both fixes were confined to the `.map` import and debug paths, so the factory's
centre-origin contract and the trigger sensors themselves are untouched.

The natural follow-ups are generalisations of each. Part A's `groundHalfHeight` is a two-entry table
(`monster_grunt` and nothing else); the moment a second floor-standing actor class exists — a bigger
enemy, an NPC — it wants an entry too, and eventually a cleaner source for that half-height than a
hard-coded literal that mirrors the factory (reading it from the collider the factory would build). Part
B's debug view is trigger-only and one flag; the same `DebugRenderConfig` + `TagDebugWireframe` pattern
extends straight to the other things you currently can't see — collision AABBs, the nav grid, spawn
points — each another `bool` in the struct and another key in the toggle. But the spine of both is done:
a level you author looks and plays the way you drew it, and when it doesn't, you can now see why.
