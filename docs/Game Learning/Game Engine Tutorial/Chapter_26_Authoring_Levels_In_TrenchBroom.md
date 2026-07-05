# Chapter 26: Authoring Levels in TrenchBroom — the Game Config, the FGD, and a First Map

## What You'll Learn
- Why we stop hand-writing level geometry in C++ and adopt **TrenchBroom**, a real Quake-lineage
  brush editor — and what a `.map` file actually *is*
- The **repo integration folder** (`tb/`) that teaches TrenchBroom about QEngine, and the new
  `assets/maps/` source folder where `.map` files live
- The v9 **`GameConfig.cfg`** schema field by field — `fileformats`, `filesystem`, the `materials`
  block, `entities`, and the `trigger_*` brush **tag**
- A real debugging war-story: a **missing `filesystem.packageformat`** made TrenchBroom *silently
  drop QEngine from the New-Map list* — no error, no log line, just an absent entry — and how the
  fix was found
- Writing the **`QEngine.fgd`** entity-definition file so it mirrors the engine's `classname`→factory
  dispatch **1:1**, and why every vector/intensity property is typed `string` and not `integer`
- Installing the config into a folder TrenchBroom scans and pointing its **Game Path** at the repo so
  `assets/textures` resolves
- Drawing, texturing, and **saving `smoke.map`** — and reading the Standard-format brush syntax that
  comes out the other end

---

## Where We Are

For twenty-five chapters the levels have been *code*. The showcase arena you've been running lives in
`src/engine/level/showcase_descriptor.cpp` — a hand-written list of surfaces and entity descriptors,
compiled into the binary. That was fine for bootstrapping an engine: it kept the level in the same
language as everything else and needed no tools. But it does not scale to *building a game*. Nobody
authors a real level by typing vertex coordinates into a `.cpp` file, recompiling, and running to see
where the wall landed.

So this chapter and the next two bring in a **level editor**. We're not writing one — that's a
multi-year project in its own right. We're adopting **TrenchBroom**, the mature, free, open-source
editor built for the Quake family of engines. It edits **brushes** (convex solids), places
**entities** (lights, spawns, doors, monsters) from a game-supplied palette, and saves everything as a
plain-text **`.map`** file. QEngine descends from the same design lineage — EnTT/Jolt under the hood,
but Quake-shaped level semantics on top — so TrenchBroom fits it almost off the shelf.

This chapter is the **authoring** half: getting TrenchBroom to understand QEngine well enough that you
can draw a room, texture it, drop a spawn point and a light, and save a `.map`. **No engine code is
written here at all** — everything is configuration and one hand-drawn map. That matters, because the
map we produce (`assets/maps/smoke.map`) is what the *next* two chapters build the loader against.
You cannot write a parser with nothing to parse.

The work splits cleanly:

1. **The integration folder** — where TrenchBroom looks for game definitions, and the two files we put
   there.
2. **`GameConfig.cfg`** — the schema that tells TrenchBroom "this is a game called QEngine, here's how
   its files and textures are laid out."
3. **The `packageformat` war-story** — a silent failure that cost real time, and its one-line fix.
4. **`QEngine.fgd`** — the entity palette, kept in lockstep with the engine's factories.
5. **Installing and pointing** — dropping the config where the editor scans, and setting the Game Path.
6. **Drawing `smoke.map`** — and what the saved file looks like.

Everything below is grounded in the files that shipped: `tb/GameConfig.cfg`, `tb/QEngine.fgd`,
`assets/maps/smoke.map`, and `assets/maps/README.md`.

---

## Step 1: Why a Brush Editor, and What a `.map` Is

Before touching a config file, it's worth being precise about the thing we're adopting, because it
shapes every decision in the next two chapters.

A **brush** is a convex solid defined not by its corners but by its *faces* — each face is a plane, and
the brush is the region on the inside of all its planes at once (their intersection). A cube is six
planes; a wedge is five. This is the Quake way of modelling level geometry, and it has a lovely
property: a brush is *always* a valid, closed, convex solid, because it's the intersection of
half-spaces — you cannot accidentally draw a non-manifold mess. You build a level by carving space out
of brushes: draw a big box, hollow it into a room, add brushes for pillars and steps.

An **entity** is everything that *isn't* static geometry: the player's spawn point, lights, doors,
triggers, monsters, pickups. Some entities are **point entities** (a light is just a position and some
properties); others are **brush entities** (a door *is* a brush, but one tagged with behaviour). Each
entity has a **`classname`** — `info_player_start`, `light`, `func_door`, `monster_grunt` — and a bag
of string key/value properties.

A **`.map` file** is the plain-text serialisation of all of that. It's human-readable, diff-able, and
completely engine-agnostic in syntax — the *meaning* of a `classname` is up to the game. Here's a
single brush face from the map we'll draw, so the shape is concrete:

```
( 48 64 112 ) ( 48 -192 112 ) ( 48 64 -16 ) grid_orange 0 0 0 1 1
```

Three points define the face's plane; `grid_orange` is the texture; the five trailing numbers are the
texture placement (offset X, offset Y, rotation, scale X, scale Y). A brush is a `{ … }` block of these
face lines; an entity is a `{ … }` block of `"key" "value"` pairs and/or brushes; the whole file is a
list of entities. That's the entire format, and Chapter 27 will write the parser for it.

> **Why adopt an existing editor instead of writing our own, or just keep hand-coding levels?** Writing
> a level editor is an enormous undertaking — viewport rendering, brush CSG, undo, texture browsing,
> entity inspectors — and none of it is *our* game. TrenchBroom has had two decades of polish poured
> into exactly this problem and is free and open-source. Hand-coding levels, meanwhile, was only ever a
> bootstrap: it couples level design to the compiler and to whoever can read C++. Adopting a real
> editor is the moment the engine stops being a tech demo and starts being something you can build
> *content* for. The one-time cost is a config file and an entity-definition file — this chapter — and
> from then on levels are data authored by eye, not code authored by hand.

---

## Step 2: The Integration Folder

TrenchBroom is a *generic* Quake-family editor; it doesn't know QEngine exists until we tell it. We
tell it with two files, and we keep those files **in the repo** so they're versioned alongside the
engine they describe. Create a `tb/` directory at the repo root:

```
QEngine/
├── tb/
│   ├── GameConfig.cfg     ← tells TrenchBroom what "QEngine" is
│   ├── QEngine.fgd        ← the palette of entities you can place
│   └── Icon.png           ← optional toolbar icon
└── assets/
    ├── textures/          ← already exists (grid_*.png, wall.png)
    └── maps/              ← NEW — .map source files live here
        └── smoke.map
```

Two things are new: `tb/` (the game definition) and `assets/maps/` (where authored maps are saved).
The `assets/textures/` folder already existed — those grid textures the showcase uses are exactly the
ones TrenchBroom will show you in its texture browser, because we're about to point it at that folder.

Keeping the game config *in the repo* rather than only in TrenchBroom's own install directory means a
new contributor clones the repo, copies `tb/` into TrenchBroom once, and has the same entity palette
and texture set as everyone else — the definition of "what QEngine levels can contain" is source-
controlled, not living only on one person's machine.

> **Why put the `.map` sources under `assets/maps/` rather than somewhere outside the shipped assets?**
> Because the engine loads them at runtime by path, exactly like it loads textures and models. A `.map`
> is a game asset in the same sense a `.png` is — the running engine reads `assets/maps/smoke.map` off
> disk (you'll see `main.cpp` default to precisely that path in Chapter 28). Keeping map sources beside
> the other assets means one folder is *the* game content, and the editor's output drops straight into
> the place the engine already reads from. There's no export/convert step: the file TrenchBroom saves
> is the file the engine loads.

---

## Step 3: `GameConfig.cfg` — Teaching TrenchBroom About QEngine

The `GameConfig.cfg` is the file that makes "QEngine" appear as a game in TrenchBroom. It's JSON, and
this build of TrenchBroom (v2026.1) expects **config version 9**. Here it is in full — `tb/GameConfig.cfg`:

```json
{
    "version": 9,
    "name": "QEngine",
    "icon": "Icon.png",
    "fileformats": [
        { "format": "Standard" }
    ],
    "filesystem": {
        "searchpath": "assets",
        "packageformat": { "extension": ".pak", "format": "idpak" }
    },
    "materials": {
        "root": "textures",
        "format": { "extensions": [ ".png" ], "format": "image" }
    },
    "entities": {
        "definitions": [ "QEngine.fgd" ],
        "defaultcolor": "0.6 0.6 0.6 1.0"
    },
    "tags": {
        "brush": [
            {
                "name": "Trigger",
                "attribs": [ "transparent" ],
                "match": "classname",
                "pattern": "trigger_*"
            }
        ],
        "brushface": []
    },
    "faceattribs": {
        "surfaceflags": [],
        "contentflags": []
    }
}
```

Read it block by block, because every field is load-bearing:

- **`version: 9`** — the config schema version. TrenchBroom refuses (or misreads) a config whose
  version doesn't match what it expects, so this must be the version *your installed build* wants. This
  is v2026.1's number; an older editor wanted a different schema (with a `textures`/`attribute` block
  instead of the `materials` block below — see the war-story in Step 4's cousin).
- **`name` / `icon`** — the display name in the game list and an optional toolbar icon.
- **`fileformats`** — the on-disk `.map` dialects TrenchBroom may save. We allow only **`Standard`**,
  the classic Quake face format (three points + texture + placement). The alternative, **Valve-220**,
  adds explicit texture-axis vectors per face — richer, but our Chapter-27 parser deliberately only
  reads Standard, so we constrain the editor to emit what the engine can read.
- **`filesystem`** — where game data lives. `searchpath: "assets"` means "relative to the game path
  (the repo root), the asset tree is under `assets/`." The `packageformat` line is the subject of the
  next step.
- **`materials`** — how textures are found. `root: "textures"` (so, `assets/textures`) and a
  `format` saying "files ending `.png`, read as plain images." This is why TrenchBroom's texture
  browser shows exactly `grid_grey`, `grid_orange`, and friends — the same PNGs the engine renders with.
- **`entities.definitions: [ "QEngine.fgd" ]`** — the entity palette, defined in the FGD file we write
  in Step 5. `defaultcolor` is the wireframe colour for entities that don't specify their own.
- **`tags.brush`** — a **Trigger tag**: any brush whose `classname` matches `trigger_*` is given the
  `transparent` attribute, so trigger volumes (which are invisible in-game) render see-through in the
  editor instead of as opaque blocks you can't see past. It's an editing convenience that mirrors the
  engine's reality — triggers are sensors, not solids.
- **`faceattribs`** — surface/content flags (Quake's `SURF_*`/`CONTENTS_*`). QEngine doesn't use them,
  so both lists are empty, but the block must exist for the schema to validate.

> **Why constrain `fileformats` to Standard only, when Valve-220 carries more texture information?**
> Because the parser is the constraint, not the editor. Valve-220 writes two extra 4-component texture-
> axis vectors on every face; Standard writes a compact `offset/rotation/scale` trailer. Our loader
> (Chapter 27) reads the Standard trailer and *nothing else* — hand it a Valve-220 face and it would
> choke on the unexpected extra numbers. By allowing only Standard in the config, we guarantee the
> editor can never save a file the engine can't read. It's the same discipline as pinning a data
> format anywhere: make the producer emit exactly what the consumer accepts, and the mismatch class of
> bug simply cannot occur.

---

## Step 4: The War-Story — A Missing `packageformat` Silently Hid the Game

Here is the field that cost the most time to get right, and it's worth telling as a debugging story
because the failure mode is so quietly nasty. Look again at the `filesystem` block:

```json
    "filesystem": {
        "searchpath": "assets",
        "packageformat": { "extension": ".pak", "format": "idpak" }
    },
```

The `packageformat` line describes how *archived* game data would be bundled — Quake shipped its assets
inside `.pak` archives (`idpak` format). QEngine ships loose files under `assets/`, uses no `.pak`
archives at all, and never will. So the first version of this config **left `packageformat` out** — it
seemed like pure Quake baggage irrelevant to a loose-file game.

The symptom: after installing the config and restarting TrenchBroom, **QEngine did not appear in the
New-Map game list.** Not greyed out, not erroring — simply *absent*, as though the config file didn't
exist. And crucially: **nothing was logged.** No parse error, no "invalid config," no diagnostic
anywhere. The config validated enough to be loaded and then silently discarded because a *required*
field was missing. From the outside it was indistinguishable from "TrenchBroom isn't reading my file at
all," which sent the investigation down entirely the wrong path — checking install directories,
file permissions, JSON syntax — for far longer than it should have.

The fix was to add the `packageformat` block back, even though QEngine uses no packages:

```json
        "packageformat": { "extension": ".pak", "format": "idpak" }
```

With it present, QEngine appeared in the New-Map list immediately. TrenchBroom treats `packageformat`
as a **mandatory** part of a valid `filesystem` block for this schema version; a config missing it is
rejected wholesale, but the rejection is silent.

> **Why does a game that never uses `.pak` archives still need to declare a `packageformat`?** It
> doesn't need one *functionally* — QEngine reads loose files and the `.pak` declaration is never
> exercised. It needs one to satisfy the config *schema*: the v9 `filesystem` block requires the field
> to be structurally valid, and TrenchBroom's validation is all-or-nothing, so an otherwise-perfect
> config with the field omitted is thrown away entirely. The value is inert; its *presence* is what
> matters. The declaration is a lie we tell the schema (`we bundle assets as idpak .pak archives`) that
> costs nothing because the code path behind it never runs.
>
> **And the real lesson — why is "no error was logged" the most dangerous part?** A loud failure
> ("invalid config at line 8") points you straight at the problem. A *silent* one points you nowhere,
> so you pattern-match to the nearest familiar failure — "the file isn't being found" — and burn time
> in the wrong place. When a tool "just doesn't do the thing" with no diagnostic, the highest-value move
> is to **diff against a known-good example**: TrenchBroom ships its own `games/Generic/GameConfig.cfg`,
> and putting ours beside it makes a missing required block jump out immediately. Comparing to a
> working reference beats re-reading your own file for the tenth time, because your file is exactly the
> thing your assumptions are baked into.

---

## Step 5: `QEngine.fgd` — the Entity Palette

The config told TrenchBroom *that* QEngine has entities; the **FGD** ("Forge Game Data" file) tells it
*which* entities and what properties each one has. This is the palette of things you can place in a
level, and its cardinal rule is that it must mirror the engine's `classname`→factory dispatch exactly —
every classname here must be one the engine can actually spawn, and every property key must match the
name a factory reads. The source of truth is `src/engine/level/showcase_descriptor.cpp`; the FGD keeps
its key names identical so the loader (Chapter 28) can map them 1:1 with no translation table.

Start with the header comment and the **base classes** — reusable property groups that concrete
entities inherit, so a shared property like `targetname` is defined once. From `tb/QEngine.fgd`:

```
// ─── Base classes ────────────────────────────────────────────────────
@baseclass = Targetname [ targetname(target_source) : "Name (linked to by target)" ]
@baseclass = Target     [ target(target_destination) : "Fires / links to this targetname" ]
@baseclass = Angle      [ angle(integer) : "Facing angle (yaw)" : 0 ]
@baseclass = Texture    [ texture(string) : "Texture name (assets/textures, no extension)" ]
```

`Targetname` and `Target` are the two halves of the linking system: a mover carries a `targetname`
(its name), and a trigger carries a `target` (the name it fires). The `target_source`/
`target_destination` property types are special — they make TrenchBroom draw a **link line** between a
trigger and the mover it targets, which is enormously helpful when wiring a level.

Then the concrete entities. A `@PointClass` is a point entity (an origin and properties); a
`@SolidClass` is a brush entity (built *from* brushes you draw). Here's the player spawn, the lights,
and a mover, quoting `tb/QEngine.fgd`:

```
// ─── Player / spawns ─────────────────────────────────────────────────
@PointClass base(Angle) size(-16 -16 -24, 16 16 32) color(0 255 0) = info_player_start : "Player spawn point" []

// ─── Lighting ────────────────────────────────────────────────────────
@PointClass size(-8 -8 -8, 8 8 8) color(255 255 128) = light : "Point light"
[
    _color(string)    : "Color (RGB, float, can exceed 1 for HDR)" : "1 1 1"
    ambient(string)   : "Ambient contribution" : "0.05"
    linear(string)    : "Linear attenuation" : "0.09"
    quadratic(string) : "Quadratic attenuation" : "0.032"
]

// ─── Movers (brush entities) ─────────────────────────────────────────
@SolidClass base(Targetname, Texture) color(128 0 128) = func_door : "Sliding door (opens toward endpos)"
[
    endpos(string)     : "Absolute world position when fully open" : "0 0 0"
    speed(string)      : "Movement speed (units/sec)" : "3.0"
    wait(string)       : "Time open before returning (seconds)" : "4.0"
    startdelay(string) : "Delay before moving (seconds)" : "0.0"
]
```

Read the anatomy of one line. `@PointClass` / `@SolidClass` is the kind; `base(...)` pulls in base
classes; `size(...)` is the editor bounding box (so a point entity has something to click); `color(...)`
is its wireframe colour; the name after `=` is the **classname** the engine dispatches on; the string
is the tooltip; and the `[ … ]` block lists properties as `key(type) : "label" : default`.

The FGD covers the whole showcase palette — spawns, both light kinds, static and decorative brushes,
`func_door`/`func_plat` movers, the `trigger_*` family, every `item_*` and `weapon_*` pickup, and
`monster_grunt`. The classnames are exactly the ones Chapters 18–24 taught the engine to spawn.

One detail runs through nearly every property and is easy to get wrong: **the types are almost all
`string`, even for things that are obviously numbers or vectors.** Look at `light`: `_color`,
`ambient`, `linear`, `quadratic` are all `string`, even though they hold floats. The header comment
spells out why:

```
// Property TYPES below use `string` for any vector or float-intensity field so
// the editor doesn't clamp them to 0-255 ints.
```

> **Why type a light's colour and a door's speed as `string` instead of the `integer` or `color` types
> FGD offers?** Because those typed fields *clamp and coerce*. FGD's `color` type is 0–255 integer RGB —
> but QEngine lights use **floating-point** colour that can exceed 1.0 for HDR, and a `3.5` sun would be
> mangled into an integer. `speed(string) = "3.0"` survives as the literal text `3.0`, which the engine
> parses itself with `std::stof`; `speed(integer)` would forbid the decimal outright. The FGD is just a
> UI hint layer, and the engine does its own typed parsing on the far side (`SpawnParams::getFloat`,
> `getVec3` — Chapter 18). So the safest FGD type for anything the engine parses numerically is the one
> that *doesn't touch the text*: `string`. We let the editor store the value verbatim and let the engine
> be the single authority on how to read it. Reserve the typed FGD fields for things that really are
> small bounded integers, like a pickup `amount`.

The `monster_grunt` line closes the file, and it's the neat proof that the FGD is a mirror of the
engine's capabilities:

```
// ─── Enemies ─────────────────────────────────────────────────────────
@PointClass base(Angle) size(-16 -16 0, 16 16 56) color(255 0 0) = monster_grunt : "Grunt enemy (LoS aggro + A* + melee)" []
```

That grunt — the one you spent Chapters 22–25 making shootable, aggressive, and path-finding — is now
something a level designer can *place*, by clicking, in a room.

---

## Step 6: Installing the Config and Pointing the Editor at the Repo

With the two files written, install them where TrenchBroom scans for games. Copy `GameConfig.cfg`,
`QEngine.fgd`, and `Icon.png` into a `QEngine` folder inside a games directory TrenchBroom reads:

- **Simplest:** the install's own games dir, e.g.
  `TrenchBroom-Win64-…-v2026.1-Release\games\QEngine\`.
- **Survives reinstalls:** the per-user path — `%APPDATA%\TrenchBroom\games\QEngine\` on Windows,
  `~/.TrenchBroom/games/QEngine/` on Linux/Mac.

Then one more setting, and it's the one people forget: TrenchBroom needs to know where the *repo* is, so
that `filesystem.searchpath: "assets"` resolves to a real folder. In **Preferences → Games → QEngine →
Game Path**, set it to the **repo root**. Now `assets/textures` resolves, the texture browser fills with
the grid PNGs, and the FGD entities appear in the entity palette.

Restart TrenchBroom, choose **New Map → QEngine** (which now appears, thanks to Step 4), and you're
editing a QEngine level with the correct textures and the correct entity set.

> **Why is the Game Path a separate setting from the games-config folder — isn't one location enough?**
> They answer two different questions. The games-config folder (where you copied `tb/`) is *"what is a
> QEngine level made of?"* — the same for everyone, part of the tool's installation. The Game Path is
> *"where is this particular checkout of the game's assets?"* — machine-specific, pointing at wherever
> you happened to clone the repo. Separating them is what lets the versioned `tb/` config be shared
> verbatim while each contributor's asset path stays local. Forget the Game Path and the config loads
> but the texture browser is empty and every entity that references an asset comes up blank — the
> tell-tale that TrenchBroom knows *what* QEngine is but not *where* your copy lives.

---

## Step 7: Drawing and Saving `smoke.map`

Now the payoff: draw a room. The workflow is the standard TrenchBroom one — draw a big box brush,
**Edit → Make Hollow** to carve it into a room with walls/floor/ceiling, select faces and texture them
from the browser, then drop a couple of point entities. For our smoke-test we keep it deliberately
minimal: a single closed box room, walls textured `grid_orange`, floor and ceiling `grid_grey`, one
`info_player_start`, and one `light`. **Save As** `assets/maps/smoke.map`.

The point of drawing this *now*, before any loader exists, is that it produces the real file the next
two chapters are written against. Here's what TrenchBroom saved — the complete `assets/maps/smoke.map`,
lightly trimmed to show its shape (the full file has six wall/floor/ceiling brushes):

```
// Game: QEngine
// Format: Standard
// entity 0
{
"classname" "worldspawn"
// brush 0
{
( 48 64 112 ) ( 48 -192 112 ) ( 48 64 -16 ) grid_orange 0 0 0 1 1
( 64 -192 -16 ) ( 48 -192 -16 ) ( 64 -192 112 ) grid_orange 0 0 0 1 1
( 64 64 -16 ) ( 48 64 -16 ) ( 64 -192 -16 ) grid_orange 0 0 0 1 1
( 64 -192 112 ) ( 48 -192 112 ) ( 64 64 112 ) grid_orange 0 0 0 1 1
( 64 64 112 ) ( 48 64 112 ) ( 64 64 -16 ) grid_orange 0 0 0 1 1
( 64 -192 112 ) ( 64 64 112 ) ( 64 -192 -16 ) grid_orange 0 0 0 1 1
}
// … brushes 1–5: the other three walls, the ceiling (grid_grey), the floor (grid_grey) …
}
// entity 1
{
"classname" "info_player_start"
"origin" "-32 -96 24"
"angle" "90"
}
// entity 2
{
"classname" "light"
"origin" "-72 -72 88"
}
```

Read the structure top to bottom, because it's exactly the grammar Chapter 27's parser will consume:

- **The header comments** (`// Game: QEngine`, `// Format: Standard`) are TrenchBroom's own annotations.
  Comments run from `//` to end of line and mean nothing to the engine — the parser strips them.
- **`entity 0` is `worldspawn`** — the special classname that owns all the *static world geometry*. Its
  six brushes are the room's six slabs: four walls plus a floor and a ceiling. Each brush is a `{ … }`
  block of **six face lines** (a box has six faces).
- **Each face line** is `( p0 ) ( p1 ) ( p2 ) TEXTURE offX offY rotation scaleX scaleY`. The three
  points define the face's plane and winding; the rest is texture placement. In `smoke.map` the walls
  are `grid_orange` and the floor/ceiling `grid_grey`, with a trivial `0 0 0 1 1` placement (no offset,
  no rotation, unit scale).
- **Entities 1 and 2 are point entities** — no brushes, just `"key" "value"` pairs. The spawn carries an
  `origin` and an `angle`; the light just an `origin`. Notice the coordinates are **integers in Quake
  units** and **Z-up** — the player spawn's `origin "-32 -96 24"` has Z (the last number, 24) as the
  *up* axis. Converting that to the engine's Y-up, small-float world is a job for Chapter 28, done in
  exactly one place.

That's it. There's no binary, no compile step, no lightmap bake — the file you see is the file the
engine will read. Six brushes make 36 faces (6 × 6), one player start, one light: a hollow box you can
stand in. It is the smallest map that exercises every part of the pipeline we're about to build, which
is why it's called `smoke.map`.

> **Why author a throwaway box now, before the loader that reads it even exists?** Because a parser
> written against an *imagined* file format is a parser written against your assumptions, and the
> assumptions are always slightly wrong — is the trailer really five numbers, does TrenchBroom quote
> texture names, where exactly do comments appear? A *real* file saved by the *real* editor settles all
> of that by inspection. `smoke.map` is a test fixture produced by the ground-truth tool: every quirk of
> TrenchBroom's Standard output is baked into it, so when Chapter 27's parser reads it correctly, we
> know it reads what TrenchBroom actually writes — not what we guessed it writes. Building the fixture
> first, then the code that consumes it, is the same "capture reality before you model it" discipline
> that makes the headless scenarios trustworthy.

---

## What Changed — Summary

| File | Change |
|------|--------|
| `tb/GameConfig.cfg` | **New file** — v9 TrenchBroom game config: `Standard` file format, `filesystem` (`searchpath: "assets"` + the required `packageformat`), a `materials` block rooting textures at `assets/textures` (`.png`, `image`), `entities` pointing at the FGD, and a `trigger_*` transparent brush **tag**. |
| `tb/QEngine.fgd` | **New file** — the entity palette, mirroring the engine's `classname`→factory dispatch 1:1: base classes (`Targetname`/`Target`/`Angle`/`Texture`), spawns, both light kinds, static/decor brushes, `func_door`/`func_plat`, the `trigger_*` family, all `item_*`/`weapon_*` pickups, and `monster_grunt`. Vector/intensity properties typed `string` to dodge editor clamping. |
| `tb/Icon.png` | **New file** — optional toolbar icon for the QEngine game entry. |
| `assets/maps/` | **New folder** — where authored `.map` sources live, beside the other runtime assets. |
| `assets/maps/smoke.map` | **New file** — a hand-drawn hollow box room (6 worldspawn brushes → 36 faces, `grid_orange` walls, `grid_grey` floor/ceiling) with one `info_player_start` and one `light`. The fixture the loader is developed against. |
| `assets/maps/README.md` | **New file** — how to author a map, the available textures, and the showcase→`.map` rebuild mapping. |

No engine code changed in this chapter — it's all editor configuration and one authored map. The
compiler doesn't know any of this exists yet.

---

## What You Should See

There's nothing to run in the engine yet — the loader is Chapters 27–28. What you *should* have is a
working authoring pipeline:

1. **QEngine appears in TrenchBroom's New-Map list.** With `packageformat` present and the config
   installed, choosing New Map offers "QEngine" as a game. (If it doesn't — re-read Step 4; a missing
   required `filesystem` field hides it silently.)
2. **The texture browser shows the grid textures.** With the Game Path set to the repo root,
   `grid_grey`, `grid_orange`, and the rest appear, loaded straight from `assets/textures`.
3. **The entity palette lists QEngine's entities.** `info_player_start`, `light`, `func_door`,
   `monster_grunt`, the pickups — every classname from the FGD, placeable by click.
4. **`assets/maps/smoke.map` exists on disk** as plain, readable, diff-able text — the six-brush box
   room with a spawn and a light, ready for a parser to eat.

---

## What's Next

We have an editor that speaks QEngine and a real `.map` file it produced, but the engine can't read a
byte of it. The next chapter closes exactly half that gap: **Chapter 27 writes the `.map` parser** — a
tokeniser that strips comments and splits the text into braces, parens, quoted strings and bare words,
and a recursive-descent parser that assembles those tokens into an in-memory `MapData` (entities →
brushes → faces) with line-numbered errors on malformed input. It reads `smoke.map` into structs, and
proves it with headless scenarios — but still builds no geometry and spawns no entities. Turning those
structs into a room you can walk around, with the Z-up→Y-up conversion and the scene assembly, is
Chapter 28. One step at a time, each leaving the build green.
