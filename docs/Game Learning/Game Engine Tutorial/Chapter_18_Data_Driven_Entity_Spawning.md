# Chapter 18: Data-Driven Entity Spawning

## What You'll Learn
- Why hard-coded factory calls block a level loader (and what "data-driven" buys us)
- Designing a uniform `SpawnParams` descriptor that mirrors a map entity
- Carrying render/asset handles through a `SpawnContext` with a texture-name resolver
- Building a `classname` → factory dispatch table
- Two-pass spawning: build every entity, then resolve `target`/`targetname` links
- Encoding the hard-coded showcase as a descriptor list (a stand-in for parsed map data)
- Rewiring `scene_setup.cpp` to build the scene from descriptors
- Adding a headless regression scenario that proves the data path is intact

---

## Where We Are

Back in Chapter 11 (doors/lifts/triggers) and the cleanup that followed, we split the
inline entity creation out of `scene_setup.cpp` into a `factories::` namespace — one typed
function per spawnable thing:

```cpp
factories::spawnPlayer(registry, glm::vec3(15.0f, 1.7f, 15.0f));
factories::spawnPointLight(registry, assets, glm::vec3(15.0f, 5.5f, 10.0f), /* ... */);
auto door = factories::spawnMover(registry, assets, /* start, end, scale, ... */);
factories::spawnTrigger(registry, glm::vec3(25.0f, 1.5f, 15.0f), /* ... */,
                        TriggerAction::ActivateMover, door, /* ... */);
```

That was a real improvement — `scene_setup.cpp` reads as *what* is in the scene rather than
*how* each entity is assembled. But it has a ceiling we're about to hit.

### The problem: these factories can't be driven by data

We want to author levels in **TrenchBroom** (a Quake-style brush editor) instead of writing
C++. A `.map` file describes entities as a **string `classname`** plus a bag of
**string key/values**:

```
{
"classname" "func_door"
"origin" "25 1.5 15"
"speed" "3"
"wait" "4"
"targetname" "door1"
}
```

A loader reading that file has a *string* `"func_door"` and a *string* `"3"` — it cannot call
`factories::spawnMover(reg, assets, start, end, scale, halfExtents, 3.0f, ...)`. Every factory
today has a different positional signature, takes a `MeshAssets` struct and raw OpenGL texture
ids threaded in from the call site, and links a door to its trigger with a raw `entt::entity`
handle. None of that survives contact with data coming from a file.

So before we can write a map parser (next chapter), we need a **factory layer that a loader
can call blind**: give it a classname and a property bag, get back a fully-built entity. That
is this chapter.

> **We build this now, with no parser.** The trick that makes it safe: we express the
> *existing* hard-coded showcase as a list of descriptors — the exact shape a parser will
> emit — and run it through the new dispatch layer. If the showcase still looks and behaves
> identically, the data path works. We've proven the pipeline before writing a single line of
> file parsing.

Here's the shape of what we're building:

```
showcaseDescriptors()  ──▶  spawnScene()  ──▶  spawnByClassname()  ──▶  factories::spawn*()
 (list of SpawnParams)      (two passes)        (dispatch table)         (the Ch.11 factories)
```

The Chapter 11 factories don't change at all — they become the *implementation* that each
classname factory calls into.

---

## Step 1: The Spawn Descriptor

We need one type that describes any spawnable entity the way a map file does. Create
`src/engine/level/types/spawn_params.h`:

```cpp
#pragma once
// Data-driven spawn descriptor + render context for the entity-factory dispatch
// layer. SpawnParams is the shape the .map loader will emit (and the in-code
// showcase descriptor mimics); SpawnContext carries the render/asset handles map
// data doesn't include.

#include <glm/glm.hpp>
#include <functional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>

#include "engine/ecs/types/mesh_assets.h"

namespace factories
{
    // One spawnable entity, described the way a TrenchBroom entity is: a string
    // classname, a geometric origin/size, optional target links, and a bag of
    // string key/values. Typed getters parse `props` on demand.
    struct SpawnParams
    {
        std::string classname;
        glm::vec3   origin{0.0f};   // entity origin / position
        glm::vec3   size{1.0f};     // full render extents; box collider half = size * 0.5
        std::string targetname;     // this entity's name (for target linking; may be empty)
        std::string target;         // name this entity links to (may be empty)
        std::unordered_map<std::string, std::string> props;

        bool has(const std::string& key) const { return props.find(key) != props.end(); }

        std::string getString(const std::string& key, std::string fallback = {}) const
        {
            auto it = props.find(key);
            return it == props.end() ? std::move(fallback) : it->second;
        }

        float getFloat(const std::string& key, float fallback = 0.0f) const
        {
            auto it = props.find(key);
            if (it == props.end()) return fallback;
            try { return std::stof(it->second); } catch (...) { return fallback; }
        }

        int getInt(const std::string& key, int fallback = 0) const
        {
            auto it = props.find(key);
            if (it == props.end()) return fallback;
            try { return std::stoi(it->second); } catch (...) { return fallback; }
        }

        // Parses "x y z" (whitespace-separated). Missing key / malformed → fallback.
        glm::vec3 getVec3(const std::string& key, glm::vec3 fallback = glm::vec3(0.0f)) const
        {
            auto it = props.find(key);
            if (it == props.end()) return fallback;
            std::istringstream ss(it->second);
            glm::vec3 out;
            if (!(ss >> out.x >> out.y >> out.z)) return fallback;
            return out;
        }
    };

    // Render/asset side that map data can't carry: the shared cube handles plus a
    // texture-name → GL id resolver (backed by ResourceManager at the call site).
    struct SpawnContext
    {
        MeshAssets assets;
        std::function<unsigned int(std::string_view)> texture;
    };
}
```

Two structs, two ideas:

**`SpawnParams`** is the entity as data. Note the split: `origin` and `size` are their own
typed `glm::vec3` fields (every entity has a position and a bounding size), while everything
else — speed, wait, colours, textures — lives in the `props` string map. The typed getters
(`getFloat`, `getVec3`, …) parse those strings on demand with a fallback, exactly the way a
map loader has to. `getVec3` parses `"25 4.5 15"` into a vector. If a key is missing or
malformed, you get the fallback rather than a crash — important when reading files a human
edited by hand.

> **Why `size` as full extents, and where does the collider half-extent come from?** Look at
> any box in the showcase: the door's render `scale` is `(0.2, 3, 4)` and its collider
> `halfExtents` is `(0.1, 1.5, 2)` — exactly half. The shelf is `(4,2,4)` / `(2,1,2)`. This
> holds for *every* box entity. So one field, `size`, drives both: the renderer uses it
> directly as the scale, and any collider takes `size * 0.5`. Carrying two numbers that are
> always 2× related would just be a chance to get them out of sync.

**`SpawnContext`** carries what a map file *can't*: the shared cube mesh handles
(`MeshAssets`, from Chapter 11) and a `texture` function that turns a texture *name*
(`"grid_orange"`) into an OpenGL texture id. A map entity says `"texture" "grid_orange"` — a
string — so the factory needs something that resolves names to GPU handles. We'll wire that
function to the `ResourceManager` in Step 5.

---

## Step 2: The Classname Dispatch

Now the layer a loader actually calls. Create the header
`src/engine/level/classname_factory.h`:

```cpp
#pragma once
// classname → factory dispatch. Looks up a SpawnParams' classname and builds the
// full entity by delegating to the typed factories in factories.h. This is the
// layer the .map loader maps FGD `classname`s onto.

#include <entt/entt.hpp>

#include "engine/level/types/spawn_params.h"

namespace factories
{
    // Build the entity for `p.classname`. Returns entt::null for an unknown
    // classname (logged to stderr) so the caller can keep spawning the rest.
    entt::entity spawnByClassname(entt::registry& reg, const SpawnContext& ctx,
                                  const SpawnParams& p);
}
```

Then the implementation, `src/engine/level/classname_factory.cpp`. This is the biggest new
file, but every function in it is trivial: read params, resolve a texture, delegate to a
Chapter 11 factory.

```cpp
#include "engine/level/classname_factory.h"

#include "engine/level/factories.h"
#include "engine/ecs/components.h"   // Position (info_teleport_destination marker)

#include <iostream>
#include <string>
#include <unordered_map>

namespace factories
{
namespace
{
    // Box entities carry a full-extent `size`; the collider wants the half-extent.
    glm::vec3 halfOf(const SpawnParams& p) { return p.size * 0.5f; }

    // ─── One thin factory per classname — each just translates params and
    //     delegates to a typed factories:: builder. ────────────────────────

    entt::entity make_info_player_start(entt::registry& reg, const SpawnContext&, const SpawnParams& p)
    {
        return spawnPlayer(reg, p.origin);
    }

    entt::entity make_light(entt::registry& reg, const SpawnContext& ctx, const SpawnParams& p)
    {
        return spawnPointLight(reg, ctx.assets, p.origin,
            p.getVec3("_color", glm::vec3(1.0f)),
            p.getFloat("ambient", 0.05f),
            p.getFloat("linear", 0.09f),
            p.getFloat("quadratic", 0.032f),
            ctx.texture(p.getString("marker", "grid_grey")));
    }

    entt::entity make_light_environment(entt::registry& reg, const SpawnContext&, const SpawnParams& p)
    {
        return spawnDirectionalLight(reg,
            p.getVec3("direction", glm::vec3(-0.2f, -1.0f, -0.3f)),
            p.getVec3("_color", glm::vec3(1.0f)),
            p.getFloat("ambient", 0.08f));
    }

    entt::entity make_func_static(entt::registry& reg, const SpawnContext& ctx, const SpawnParams& p)
    {
        return spawnStaticBox(reg, ctx.assets, p.origin, p.size, halfOf(p),
            ctx.texture(p.getString("texture", "grid_grey")));
    }

    entt::entity make_prop_dynamic(entt::registry& reg, const SpawnContext& ctx, const SpawnParams& p)
    {
        return spawnDemoCube(reg, ctx.assets, p.origin,
            p.getVec3("velocity", glm::vec3(0.0f)),
            p.getFloat("interval", 5.0f),
            ctx.texture(p.getString("texture", "grid_orange")));
    }

    // func_door and func_plat share the Mover archetype; the classname is only a
    // semantic hint (horizontal door vs vertical lift). Travel is carried as an
    // explicit endpos until the .map loader derives it from angle/lip/height.
    entt::entity make_func_mover(entt::registry& reg, const SpawnContext& ctx, const SpawnParams& p)
    {
        return spawnMover(reg, ctx.assets, p.origin,
            p.getVec3("endpos", p.origin), p.size, halfOf(p),
            p.getFloat("speed", 3.0f),
            p.getFloat("wait", 3.0f),
            p.getFloat("startdelay", 0.0f),
            ctx.texture(p.getString("texture", "grid_orange")));
    }

    entt::entity make_trigger_multiple(entt::registry& reg, const SpawnContext&, const SpawnParams& p)
    {
        // target (→ mover) resolved in spawnScene's second pass.
        return spawnTrigger(reg, p.origin, halfOf(p), TriggerAction::ActivateMover,
            entt::null, glm::vec3(0.0f), 0.0f, p.getFloat("cooldown", 1.0f));
    }

    entt::entity make_trigger_teleport(entt::registry& reg, const SpawnContext&, const SpawnParams& p)
    {
        // destination overridden from the linked info_teleport_destination in pass 2;
        // a literal "destination" prop is a fallback when no target is given.
        return spawnTrigger(reg, p.origin, halfOf(p), TriggerAction::Teleport,
            entt::null, p.getVec3("destination", p.origin), 0.0f,
            p.getFloat("cooldown", 1.0f));
    }

    entt::entity make_trigger_hurt(entt::registry& reg, const SpawnContext&, const SpawnParams& p)
    {
        return spawnTrigger(reg, p.origin, halfOf(p), TriggerAction::Damage,
            entt::null, glm::vec3(0.0f), p.getFloat("dmg", 25.0f),
            p.getFloat("cooldown", 0.0f));
    }

    entt::entity make_info_teleport_destination(entt::registry& reg, const SpawnContext&, const SpawnParams& p)
    {
        // Marker: carries only an origin so a trigger_teleport can resolve it by name.
        auto e = reg.create();
        reg.emplace<Position>(e, p.origin);
        return e;
    }

    entt::entity make_func_decor(entt::registry& reg, const SpawnContext& ctx, const SpawnParams& p)
    {
        return spawnDecorBox(reg, ctx.assets, p.origin, p.size,
            ctx.texture(p.getString("texture", "grid_grey")));
    }

    entt::entity make_wireframe(entt::registry& reg, const SpawnContext& ctx, const SpawnParams& p)
    {
        // Engine-internal (leading underscore): editor debug volume, not a map entity.
        return spawnDebugWireframe(reg, ctx.assets, p.origin, p.size,
            ctx.texture(p.getString("texture", "grid_green")));
    }

    using SpawnFn = entt::entity(*)(entt::registry&, const SpawnContext&, const SpawnParams&);

    const std::unordered_map<std::string, SpawnFn>& table()
    {
        static const std::unordered_map<std::string, SpawnFn> t = {
            { "info_player_start",         &make_info_player_start },
            { "light",                     &make_light },
            { "light_environment",         &make_light_environment },
            { "func_static",               &make_func_static },
            { "prop_dynamic",              &make_prop_dynamic },
            { "func_door",                 &make_func_mover },
            { "func_plat",                 &make_func_mover },
            { "trigger_multiple",          &make_trigger_multiple },
            { "trigger_teleport",          &make_trigger_teleport },
            { "trigger_hurt",              &make_trigger_hurt },
            { "info_teleport_destination", &make_info_teleport_destination },
            { "func_decor",                &make_func_decor },
            { "_wireframe",                &make_wireframe },
        };
        return t;
    }
} // namespace

entt::entity spawnByClassname(entt::registry& reg, const SpawnContext& ctx, const SpawnParams& p)
{
    auto it = table().find(p.classname);
    if (it == table().end())
    {
        std::cerr << "[factories] unknown classname '" << p.classname << "' — skipped\n";
        return entt::null;
    }
    return it->second(reg, ctx, p);
}

} // namespace factories
```

There's a lot here, but the shape is simple and repeats. A few things worth calling out:

- **Each `make_*` is a pure translation.** It pulls typed values out of `SpawnParams`
  (falling back to the classname's defaults), resolves any texture name via `ctx.texture`, and
  hands off to the Chapter 11 factory. No new entity-assembly logic exists in this file — it's
  an adapter, not a rewrite.

- **The classnames are Quake conventions.** `info_player_start`, `light`, `func_door`,
  `trigger_hurt` — these are the standard names TrenchBroom and its `.fgd` entity definitions
  use, so when we add the editor next chapter, the names already line up.

- **`func_door` and `func_plat` share `make_func_mover`.** A door and a lift are the same
  `Mover` archetype (Chapter 11) — one slides horizontally, one vertically. The classname is
  just a semantic label; the behaviour is identical, so they point at the same factory.

- **`info_teleport_destination` is a bare marker.** It's an entity with nothing but a
  `Position`. It exists only so a teleporter can point at it *by name* and read its location.
  We'll use it in Step 4.

- **The `_wireframe` classname has a leading underscore** to mark it engine-internal — those
  green boxes visualising trigger volumes are a debug aid, not something you'd place in a real
  map. Keeping it in the table lets our descriptor reproduce the current scene exactly.

- **Unknown classname → `entt::null` + a log line, not a crash.** When you're loading a
  hand-authored map, a typo like `triger_hurt` shouldn't take the whole level down. Skip it,
  warn, keep going.

> **Why a `static` table built once?** The map is a lookup from string to function pointer.
> Building it inside a function with a `static` local means it's constructed on first use and
> reused forever — no global-initialisation-order headaches, and adding a new spawnable type
> later is a one-line entry, nothing else in the file changes.

---

## Step 3: Two-Pass Spawning and Target Links

There's one thing a single `spawnByClassname` call can't do: **link entities to each other**.
A `trigger_multiple` opens a *specific* door. In a map file that link is by name —
`"target" "door1"` on the trigger, `"targetname" "door1"` on the door — and the two entities
can appear in the file in *either order*. You can't resolve the link while spawning, because
the thing you're linking to might not exist yet.

The fix is a **two-pass load**: spawn everything first (recording each named entity), then go
back and resolve the links now that every name is known. Create the header
`src/engine/level/spawn_scene.h`:

```cpp
#pragma once
// Two-pass scene spawn: build every entity from its descriptor, then resolve
// target/targetname links (trigger → mover, teleport → destination) once all
// names exist. This is the load path the .map parser feeds SpawnParams into.

#include <vector>

#include <entt/entt.hpp>

#include "engine/level/types/spawn_params.h"

namespace factories
{
    // Spawn all `descriptors`, then link `target`→`targetname`. Returns the
    // spawned entities in descriptor order (entt::null where a classname was
    // unknown). Must run before buildWorld()'s mover view so movers still get
    // their kinematic bodies.
    std::vector<entt::entity> spawnScene(entt::registry& reg, const SpawnContext& ctx,
                                         const std::vector<SpawnParams>& descriptors);
}
```

And `src/engine/level/spawn_scene.cpp`:

```cpp
#include "engine/level/spawn_scene.h"

#include "engine/level/classname_factory.h"
#include "engine/ecs/components.h"   // TriggerVolume, TriggerAction, Position

#include <cstddef>
#include <iostream>
#include <string>
#include <unordered_map>

namespace factories
{
    std::vector<entt::entity> spawnScene(entt::registry& reg, const SpawnContext& ctx,
                                         const std::vector<SpawnParams>& descriptors)
    {
        std::vector<entt::entity> spawned;
        spawned.reserve(descriptors.size());
        std::unordered_map<std::string, entt::entity> byName;

        // Pass 1 — spawn everything, index the named entities.
        for (const auto& d : descriptors)
        {
            entt::entity e = spawnByClassname(reg, ctx, d);
            spawned.push_back(e);
            if (e != entt::null && !d.targetname.empty())
                byName[d.targetname] = e;
        }

        // Pass 2 — resolve target links now that every name is known.
        for (std::size_t i = 0; i < descriptors.size(); ++i)
        {
            const SpawnParams& d = descriptors[i];
            entt::entity e = spawned[i];
            if (e == entt::null || d.target.empty()) continue;

            auto it = byName.find(d.target);
            if (it == byName.end())
            {
                std::cerr << "[spawnScene] '" << d.classname << "' targets unknown name '"
                          << d.target << "'\n";
                continue;
            }
            entt::entity target = it->second;

            // Only triggers carry links today.
            TriggerVolume* tv = reg.try_get<TriggerVolume>(e);
            if (!tv) continue;

            if (tv->action == TriggerAction::Teleport)
            {
                if (const Position* pos = reg.try_get<Position>(target))
                    tv->destination = pos->value;
            }
            else
            {
                tv->target = target;  // ActivateMover (door/lift), etc.
            }
        }

        return spawned;
    }
}
```

Pass 1 spawns each descriptor and, if it has a `targetname`, records it in a `name → entity`
map. Pass 2 walks the descriptors again: for any entity with a `target`, it looks up the named
entity and patches the link. There are two kinds of link, both landing on a `TriggerVolume`:

- **A `trigger_multiple` targeting a door/lift** sets `tv->target = <the mover entity>`, so
  the trigger system knows which mover to activate.
- **A `trigger_teleport` targeting an `info_teleport_destination`** copies that marker's
  `Position` into `tv->destination`, so the teleporter sends the player to the marker's spot.

`try_get` returns a pointer or `nullptr`, so a dangling `target` (name that doesn't exist)
just logs and skips — again, resilient to hand-edited map typos.

> **Why return the entity vector at all?** The caller (and later the map loader) sometimes
> needs the spawned handles — for a summary, for debugging, or for wiring that the two-pass
> pass doesn't cover. We keep them in descriptor order so `spawned[i]` matches
> `descriptors[i]`.

### A subtlety: movers and their physics bodies

Recall from Chapter 15 that kinematic bodies for movers aren't created at spawn time — they're
created in `buildWorld`, *after* the physics broad-phase is first optimised, by iterating every
entity that has a `Mover` component:

```cpp
// in buildWorld(), after OptimizeBroadPhase():
auto moverView = registry.view<Position, AABBCollider, Mover>();
for (auto [entity, pos, col, mover] : moverView.each())
    createKinematicBody(registry, entity);
```

This is why `spawnScene` **must run inside `setupScene`**, which `buildWorld` calls *before*
that mover loop. As long as our door and lift entities exist (with their `Mover` components)
by the time the loop runs, they get their kinematic bodies for free — we don't do anything
Jolt-related in the factories. Order matters; the design already respects it.

---

## Step 4: The Showcase as Descriptors

Now we express the entire hard-coded showcase as a `std::vector<SpawnParams>`. This is the
stand-in for parsed map data. Create the header `src/engine/level/showcase_descriptor.h`:

```cpp
#pragma once
// The hard-coded showcase expressed as spawn descriptors — the same shape a
// parsed .map will emit. Feeding this through factories::spawnScene must
// reproduce the C++-built showcase exactly (regression-guarded by the harness).
// It stands in for parser output until the .map loader lands.

#include <vector>

#include "engine/level/types/spawn_params.h"

std::vector<factories::SpawnParams> showcaseDescriptors();
```

Then `src/engine/level/showcase_descriptor.cpp`. We use **C++20 designated initializers**
(`.classname = ...`) so each descriptor reads like a small record and we only set the fields
that matter. The values come straight from the old `scene_setup.cpp` — same positions, same
speeds, same textures.

```cpp
#include "engine/level/showcase_descriptor.h"

// Mirrors scene_setup.cpp's factory calls 1:1. Geometric `size` is the full
// render extent (box colliders take half). Textures are names, resolved by the
// SpawnContext at spawn time. Doors/lifts/teleporters are linked by targetname.

using factories::SpawnParams;

std::vector<SpawnParams> showcaseDescriptors()
{
    std::vector<SpawnParams> d;

    // ─── Player ──────────────────────────────────────────────────
    d.push_back({
        .classname = "info_player_start",
        .origin = glm::vec3(15.0f, 1.7f, 15.0f),
    });

    // ─── Lighting ────────────────────────────────────────────────
    d.push_back({
        .classname = "light_environment",
        .props = { {"direction", "-0.2 -1.0 -0.3"}, {"_color", "1 1 1"}, {"ambient", "0.08"} },
    });

    // Bright ceiling lights (front/back halves) — wide range.
    d.push_back({
        .classname = "light", .origin = glm::vec3(15.0f, 5.5f, 10.0f),
        .props = { {"_color", "2 2 2"}, {"ambient", "0.05"}, {"linear", "0.09"},
                   {"quadratic", "0.032"}, {"marker", "grid_grey"} },
    });
    d.push_back({
        .classname = "light", .origin = glm::vec3(15.0f, 5.5f, 20.0f),
        .props = { {"_color", "2 2 2"}, {"ambient", "0.05"}, {"linear", "0.09"},
                   {"quadratic", "0.032"}, {"marker", "grid_grey"} },
    });

    // Coloured torches down the left wall — tight pools.
    d.push_back({
        .classname = "light", .origin = glm::vec3(3.0f, 2.0f, 10.0f),
        .props = { {"_color", "3.0 0.2 0.2"}, {"ambient", "0.01"}, {"linear", "0.35"},
                   {"quadratic", "0.44"}, {"marker", "grid_red"} },
    });
    d.push_back({
        .classname = "light", .origin = glm::vec3(3.0f, 2.0f, 15.0f),
        .props = { {"_color", "0.2 3.0 0.2"}, {"ambient", "0.01"}, {"linear", "0.35"},
                   {"quadratic", "0.44"}, {"marker", "grid_green"} },
    });
    d.push_back({
        .classname = "light", .origin = glm::vec3(3.0f, 2.0f, 20.0f),
        .props = { {"_color", "0.2 0.2 3.0"}, {"ambient", "0.01"}, {"linear", "0.35"},
                   {"quadratic", "0.44"}, {"marker", "grid_blue"} },
    });

    // ─── Physics demos ───────────────────────────────────────────
    // Shelf: raised static platform to slide cubes off (top surface at y=2).
    d.push_back({
        .classname = "func_static", .origin = glm::vec3(20.0f, 1.0f, 5.0f),
        .size = glm::vec3(4.0f, 2.0f, 4.0f),
        .props = { {"texture", "grid_blue"} },
    });

    // Cube 1: nudged off the shelf edge (resets every 6s).
    d.push_back({
        .classname = "prop_dynamic", .origin = glm::vec3(20.5f, 4.0f, 5.0f),
        .props = { {"velocity", "-6.0 0.0 0.0"}, {"interval", "6.0"}, {"texture", "grid_orange"} },
    });
    // Cube 2: pure gravity drop (resets every 4s).
    d.push_back({
        .classname = "prop_dynamic", .origin = glm::vec3(20.0f, 5.0f, 8.0f),
        .props = { {"velocity", "0 0 0"}, {"interval", "4.0"}, {"texture", "grid_orange"} },
    });
    // Cube 3: slides across the floor, low friction (resets every 5s).
    d.push_back({
        .classname = "prop_dynamic", .origin = glm::vec3(20.0f, 0.5f, 12.0f),
        .props = { {"velocity", "3.0 0.0 1.0"}, {"interval", "5.0"}, {"texture", "grid_orange"} },
    });

    // ─── Door (slides upward when the player approaches) ──────────
    d.push_back({
        .classname = "func_door", .origin = glm::vec3(25.0f, 1.5f, 15.0f),
        .size = glm::vec3(0.2f, 3.0f, 4.0f), .targetname = "door1",
        .props = { {"endpos", "25.0 4.5 15.0"}, {"speed", "3.0"}, {"wait", "4.0"},
                   {"startdelay", "0.0"}, {"texture", "grid_orange"} },
    });
    d.push_back({
        .classname = "trigger_multiple", .origin = glm::vec3(25.0f, 1.5f, 15.0f),
        .size = glm::vec3(4.0f, 3.0f, 5.0f), .target = "door1",
        .props = { {"cooldown", "1.0"} },
    });
    d.push_back({
        .classname = "_wireframe", .origin = glm::vec3(25.0f, 1.5f, 15.0f),
        .size = glm::vec3(4.0f, 3.0f, 5.0f),
        .props = { {"texture", "grid_green"} },
    });

    // ─── Lift (rises when the player steps on it) ─────────────────
    d.push_back({
        .classname = "func_plat", .origin = glm::vec3(10.0f, 0.2f, 25.0f),
        .size = glm::vec3(3.0f, 0.2f, 3.0f), .targetname = "lift1",
        .props = { {"endpos", "10.0 4.2 25.0"}, {"speed", "2.0"}, {"wait", "2.0"},
                   {"startdelay", "2.0"}, {"texture", "grid_green"} },
    });
    d.push_back({
        .classname = "trigger_multiple", .origin = glm::vec3(10.0f, 0.5f, 25.0f),
        .size = glm::vec3(3.0f, 0.6f, 3.0f), .target = "lift1",
        .props = { {"cooldown", "0.5"} },
    });
    d.push_back({
        .classname = "_wireframe", .origin = glm::vec3(10.0f, 0.5f, 25.0f),
        .size = glm::vec3(3.0f, 0.6f, 3.0f),
        .props = { {"texture", "grid_green"} },
    });

    // ─── Teleporter → far corner ─────────────────────────────────
    d.push_back({
        .classname = "trigger_teleport", .origin = glm::vec3(5.0f, 0.5f, 5.0f),
        .size = glm::vec3(2.0f, 3.0f, 2.0f), .target = "tp_dest1",
        .props = { {"cooldown", "1.0"} },
    });
    d.push_back({
        .classname = "info_teleport_destination", .origin = glm::vec3(25.0f, 1.0f, 25.0f),
        .targetname = "tp_dest1",
    });
    d.push_back({
        .classname = "_wireframe", .origin = glm::vec3(5.0f, 0.5f, 5.0f),
        .size = glm::vec3(2.0f, 3.0f, 2.0f),
        .props = { {"texture", "grid_green"} },
    });
    d.push_back({  // centre pole
        .classname = "func_decor", .origin = glm::vec3(5.0f, 1.5f, 5.0f),
        .size = glm::vec3(0.1f, 3.0f, 0.1f),
        .props = { {"texture", "grid_blue"} },
    });

    // ─── Lava pool (visible surface + damage zone, 25/sec, no cooldown) ──
    d.push_back({
        .classname = "func_decor", .origin = glm::vec3(20.0f, 0.1f, 25.0f),
        .size = glm::vec3(6.0f, 0.2f, 6.0f),
        .props = { {"texture", "grid_red"} },
    });
    d.push_back({
        .classname = "trigger_hurt", .origin = glm::vec3(20.0f, 0.5f, 25.0f),
        .size = glm::vec3(6.0f, 1.0f, 6.0f),
        .props = { {"dmg", "25.0"}, {"cooldown", "0.0"} },
    });
    d.push_back({
        .classname = "_wireframe", .origin = glm::vec3(20.0f, 0.5f, 25.0f),
        .size = glm::vec3(6.0f, 1.0f, 6.0f),
        .props = { {"texture", "grid_red"} },
    });

    return d;
}
```

Work through one entry to see the mapping. The old door was:

```cpp
auto door = factories::spawnMover(registry, assets,
    glm::vec3(25.0f, 1.5f, 15.0f), glm::vec3(25.0f, 4.5f, 15.0f),  // start, end
    glm::vec3(0.2f, 3.0f, 4.0f), glm::vec3(0.1f, 1.5f, 2.0f),      // scale, halfExtents
    3.0f, 4.0f, 0.0f, gridOrange->getId());                        // speed, wait, delay, tex
```

Its descriptor is `classname "func_door"`, `origin` = the start position, `size` = the scale
`(0.2, 3, 4)` (the collider half `(0.1, 1.5, 2)` is derived as `size * 0.5`), `endpos` = the
end position, and `speed`/`wait`/`startdelay`/`texture` as props. The `targetname "door1"`
replaces the raw `door` entity handle that the old trigger call captured directly.

Two things to notice about the **teleporter**, because it's the most interesting case:

1. The old code passed the teleport destination `(25, 1, 25)` straight into the trigger. Our
   descriptor instead adds a separate `info_teleport_destination` entity at `(25, 1, 25)` with
   `targetname "tp_dest1"`, and the trigger `target`s that name. The destination is resolved by
   the pass-2 linking. This is exactly how TrenchBroom models teleporters — a trigger brush
   plus a destination point entity — so we're building the real thing, not a shortcut.

2. That's one *extra* entity compared to the old scene (the marker). It has only a `Position`,
   no mesh, no collider — every system ignores it. Harmless, and it's what lets us exercise the
   linking path end-to-end.

---

## Step 5: Rewire `scene_setup.cpp`

This is the only behavioural change in the chapter: replace the ~90 lines of inline factory
calls with the descriptor-driven path. Open `src/engine/app/scene_setup.cpp` and replace the
whole entity-spawning body with:

```cpp
#include "engine/app/scene_setup.h"
#include "engine/level/showcase_level.h"
#include "engine/level/spawn_scene.h"
#include "engine/level/showcase_descriptor.h"
#include "engine/ecs/components.h"
#include "engine/level/level.h"

#include <string>

Level setupScene
(
	entt::registry& registry,
	const ResourceManager& resources,
	bool headless
)
{
    auto litShader   = resources.getShader("lit");
    auto gridGrey    = resources.getTexture("grid_grey");
    auto gridOrange  = resources.getTexture("grid_orange");
    auto gridRed     = resources.getTexture("grid_red");
    auto cubeMesh    = resources.getMesh("cube");

    // ─── Showcase level geometry ────────────────────────────────
    Level level = createShowcaseLevel(headless);
    for (const auto& sector : level.sectors)
    {
        if (!sector.mesh) continue;

        auto sectorEntity = registry.create();
        registry.emplace<Position>(sectorEntity, glm::vec3(0.0f));
        registry.emplace<MeshRenderer>
        (
            sectorEntity,
            sector.mesh->getVAO(), 0u,
            litShader->getId(), gridGrey->getId(),
            true, sector.mesh->getIndexCount()
        );
    }

    // ─── Scene entities: built from descriptors via classname dispatch ──
    // showcaseDescriptors() is the in-code stand-in for parsed .map data.
    // spawnScene runs a two-pass build (spawn all, then resolve door/lift/
    // teleporter target links) — before buildWorld's mover view, so movers
    // still get their kinematic bodies. The SpawnContext supplies the shared
    // cube handles and resolves texture names → GL ids.
    factories::SpawnContext ctx;
    ctx.assets = factories::MeshAssets{ cubeMesh->getVAO(), cubeMesh->getIndexCount(),
                                        litShader->getId() };
    ctx.texture = [&resources](std::string_view name)
    {
        return resources.getTexture(std::string(name))->getId();
    };
    factories::spawnScene(registry, ctx, showcaseDescriptors());

    // ─── Combat resources (registry context) ────────────────────
    auto& combatRes = registry.ctx().emplace<CombatResources>();
    combatRes.cubeVAO = cubeMesh->getVAO();
    combatRes.cubeIndexCount = cubeMesh->getIndexCount();
    combatRes.shaderId = litShader->getId();
    combatRes.projectileTextureId = gridRed->getId();    // red cubes for rockets
    combatRes.tracerTextureId = gridOrange->getId();      // orange lines for hitscan

    return level;
}
```

What stayed and what changed:

- **The level *geometry* loop stays.** The walls/floor/ceiling come from
  `createShowcaseLevel()` as sector meshes, not from entity factories. That's a separate
  concern (it becomes brush geometry when the map loader lands next chapter), so we leave it
  alone for now.
- **The `CombatResources` context stays** — it's engine wiring for projectile rendering, not a
  spawnable entity.
- **Everything between them** — the player, lights, demo cubes, doors, lifts, triggers,
  teleporter, lava — is now one call: `spawnScene(registry, ctx, showcaseDescriptors())`.
- **The `SpawnContext` is built here**, at the one place that has the `ResourceManager`. The
  `ctx.texture` lambda closes over `resources` and turns a name into a GL id
  (`resources.getTexture(name)->getId()`). This is the bridge between string-named map data
  and live GPU handles.

Notice we now only look up the four textures the *remaining* code needs (`grid_grey` for the
sector meshes, `grid_red`/`grid_orange` for combat resources). All the other texture lookups
that used to be here — `grid_blue`, `grid_green` — moved into the descriptor as *names* and
are resolved on demand by `ctx.texture`.

---

## Step 6: A Regression Scenario

We claimed the data path reproduces the scene exactly. Let's prove it in the headless harness
(Chapter 10a) with an assertion, not just by eyeballing the window. This scenario tallies the
descriptor classnames and checks that the built world contains exactly those archetypes — so a
dispatch regression (an unhandled classname, a mis-wired factory) shows up as a count
mismatch.

In `src/harness/headless_main.cpp`, add the include near the others:

```cpp
#include "engine/level/showcase_descriptor.h"
```

Add the scenario function alongside the others (e.g. just before `scenario_teleporter`):

```cpp
    // The showcase must build identically through the descriptor → classname
    // dispatch path. Tally the descriptor classnames and assert the world's
    // component archetypes match — a data-path regression (an unhandled
    // classname, a mis-wired factory) shows up here as a count mismatch. Pure
    // static check on the built world; runs no ticks.
    bool scenario_spawn_counts(entt::registry& reg, JoltWorld&, const Level&, float)
    {
        // Expected tallies from the source-of-truth descriptors.
        int dPlayer = 0, dMover = 0, dTrigger = 0, dPoint = 0, dDir = 0, dDemo = 0;
        for (const auto& p : showcaseDescriptors())
        {
            if      (p.classname == "info_player_start")                       dPlayer++;
            else if (p.classname == "func_door" || p.classname == "func_plat") dMover++;
            else if (p.classname.rfind("trigger_", 0) == 0)                    dTrigger++;
            else if (p.classname == "light")                                   dPoint++;
            else if (p.classname == "light_environment")                       dDir++;
            else if (p.classname == "prop_dynamic")                            dDemo++;
        }

        // What actually got built.
        auto count = [](auto view) { int n = 0; for (auto e : view) { (void)e; ++n; } return n; };
        int wPlayer  = count(reg.view<TagPlayer>());
        int wMover   = count(reg.view<Mover>());
        int wTrigger = count(reg.view<TriggerVolume>());
        int wPoint   = count(reg.view<PointLight>());
        int wDir     = count(reg.view<DirectionalLight>());
        int wDemo    = count(reg.view<DemoReset>());

        bool match = wPlayer == dPlayer && wMover == dMover && wTrigger == dTrigger
                  && wPoint == dPoint && wDir == dDir && wDemo == dDemo;
        // Sanity floor: the known showcase shape (1 player, 2 movers, 4 triggers,
        // 5 point lights, 1 sun, 3 demo cubes) — catches an empty/half-built scene.
        bool shape = wPlayer == 1 && wMover == 2 && wTrigger == 4
                  && wPoint == 5 && wDir == 1 && wDemo == 3;

        char buf[200];
        std::snprintf(buf, sizeof(buf),
            "player %d/%d mover %d/%d trigger %d/%d point %d/%d dir %d/%d demo %d/%d (world/descriptor)",
            wPlayer, dPlayer, wMover, dMover, wTrigger, dTrigger,
            wPoint, dPoint, wDir, dDir, wDemo, dDemo);
        return report("spawn_counts", match && shape, buf);
    }
```

And register it in the scenario dispatch in `main`:

```cpp
    else if (scenario == "rocket_vs_floor")  pass = scenario_rocket_vs_floor(registry, jolt, level, dt);
    else if (scenario == "teleporter")       pass = scenario_teleporter(registry, jolt, level, dt);
    else if (scenario == "spawn_counts")     pass = scenario_spawn_counts(registry, jolt, level, dt);
```

The check is a closed loop: the descriptors are the source of truth, and the world must match
them. The `shape` line is a second guard against a subtler failure — if `showcaseDescriptors()`
itself were emptied or corrupted, the two tallies could still "match" at zero; pinning the
known counts (1/2/4/5/1/3) catches that.

> **Why counts and not exact positions?** The existing scenarios (`ride_lift_up`,
> `teleporter`, …) already assert *behaviour* — the lift carries the player, the teleporter
> lands them at `(25,1,25)`. Those are the real proof the *values* came through. This new
> scenario adds the one thing they don't cover: that the dispatch *built the right set of
> things at all*. Together they pin down the whole path.

---

## Step 7: Wire Up CMake, Build, and Run

Add the three new `.cpp` files to the `qengine_lib` source list in `CMakeLists.txt` (the
headers and `spawn_params.h` are header-only — nothing to compile). Put them next to the
existing `factories.cpp`:

```cmake
	src/engine/level/factories.cpp
	src/engine/level/classname_factory.cpp
	src/engine/level/spawn_scene.cpp
	src/engine/level/showcase_descriptor.cpp
```

Build (remember the MSYS2 UCRT64 toolchain must be on `PATH`, per Chapter 0):

```bash
cmake --build build
```

Run the full headless suite — all seven scenarios should pass:

```bash
build/QEngineHeadless.exe rest_no_jitter
build/QEngineHeadless.exe ride_lift_up
build/QEngineHeadless.exe walk_onto_lift
build/QEngineHeadless.exe walk_floor_seams
build/QEngineHeadless.exe rocket_vs_floor
build/QEngineHeadless.exe teleporter
build/QEngineHeadless.exe spawn_counts
```

Expected output:

```
[PASS] rest_no_jitter — resting Y band=0.0000 (max-min over 240 ticks), maxAbsVelY=0.0000
[PASS] ride_lift_up — max separation above lift during ascent=0.0333 ...
[PASS] walk_onto_lift — onPlatform=1 footY=0.300 (liftTop=0.300) maxUpVelY=0.000 stuckSeamTicks=0
[PASS] walk_floor_seams — travelled=13.71 maxAbsVelY=0.0000 minFwdSpeed=7.000 ...
[PASS] rocket_vs_floor — projectile minY=0.378 stillAlive=0 ...
[PASS] teleporter — player ended at (25.00,0.95,25.00), dist to destination=0.05
[PASS] spawn_counts — player 1/1 mover 2/2 trigger 4/4 point 5/5 dir 1/1 demo 3/3 (world/descriptor)
```

The `teleporter` line is the one to savour: the player still lands at `(25, 0.95, 25)`, but
that destination now travelled through the `info_teleport_destination` marker and the pass-2
linking rather than a hard-coded literal. The linking path a real map depends on is live and
correct.

---

## What Changed — Summary

| File | Change |
|------|--------|
| `level/types/spawn_params.h` | **New.** `SpawnParams` (classname, origin, size, target links, string props + typed getters) and `SpawnContext` (mesh handles + texture-name resolver). |
| `level/classname_factory.h` / `.cpp` | **New.** 13 thin `make_*` factories, each delegating to a Chapter 11 `factories::spawn*`, behind a `classname → fn` table and `spawnByClassname()`. |
| `level/spawn_scene.h` / `.cpp` | **New.** Two-pass loader: spawn all, then resolve `target`/`targetname` links (trigger → mover, teleport → destination). |
| `level/showcase_descriptor.h` / `.cpp` | **New.** The showcase as 24 descriptors — the parser stand-in. |
| `level/factories.h` / `.cpp` | **Unchanged.** Reused as the implementation layer under the dispatch. |
| `app/scene_setup.cpp` | Rewired: inline factory calls replaced by building a `SpawnContext` and one `spawnScene(...)` call. Geometry loop + combat resources unchanged. |
| `harness/headless_main.cpp` | Added the `spawn_counts` regression scenario and its dispatch entry. |
| `CMakeLists.txt` | Added the three new `.cpp` files to `qengine_lib`. |

---

## What You Should See

Build and run the windowed game (`build/QEngine.exe`). **Nothing should look different** — same
room, same lights, same door that opens, same lift, same teleporter, same lava. That's the
whole point: the scene is now built by a data-driven pipeline, but the output is identical.

Under the hood, the difference is structural:

1. **Adding a new spawnable type is now one line** — register a `classname` against a factory
   in the dispatch table. Pickups (`item_health`, `item_shells`) and enemies (`monster_grunt`)
   slot in the same way, with no changes to `scene_setup` or the loader.
2. **The scene is described by data**, not code. `showcaseDescriptors()` is a plain list you
   could serialise, diff, or generate.
3. **Entities link by name**, not by C++ handles — the mechanism a level file needs.

---

## What's Next

We built the socket; next we plug in the real thing. In **Chapter 19: The `.map` Parser &
Brush Geometry**, we install TrenchBroom, define the QEngine entity set in a `.fgd` file, and
write the loader that reads a `.map` from disk — parsing entities into `SpawnParams` (which
flow straight into the `spawnScene` we just built) and brushes into renderable, collidable
geometry. The hard-coded `showcaseDescriptors()` becomes `showcase.map`, and the engine stops
having its levels compiled into it.
```
