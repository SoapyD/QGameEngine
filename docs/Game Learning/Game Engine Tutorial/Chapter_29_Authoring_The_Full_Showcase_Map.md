# Chapter 29: Authoring the Full Showcase Map — Entities, Target Linking, and the Coordinate Gotcha

## What You'll Learn
- How to reproduce the **entire hard-coded showcase arena** (`showcase_descriptor.cpp`) as a hand-authored
  `assets/maps/showcase.map` — so the richest level in the engine becomes **data, not code**
- Placing **point entities** by eye — the player start, the sun (`light_environment`), a plain white
  `light`, three coloured torches, every `weapon_*`/`item_*` pickup, and two `monster_grunt`s — and which
  key/values you actually have to type versus which you can leave to the factory defaults
- The **`target`/`targetname` linking pattern** for brush entities — wiring a `func_door` to its
  `trigger_multiple`, a `func_plat` to its trigger, and a `trigger_teleport` to its
  `info_teleport_destination` — the single most important authoring skill in the chapter
- The **coordinate gotcha that bites everyone**: `endpos` (and `velocity`, and the sun's `direction`) are
  typed in **map space**, because the loader converts them on import — grounded in the real conversion in
  `map_to_descriptors.cpp`, with a worked lift example
- The authoring **pitfalls we actually hit**: a stray untextured (`__TB_empty`) worldspawn brush that
  renders as garbage, a texture-name typo silently rescued by the majority-vote rule, and missing optional
  keys quietly falling back to factory defaults
- **Loading your map**: the `main.cpp` launch argument and the `.vscode/launch.json` `args`, including the
  `""`/`showcase` shortcuts to fall back to the in-code arena

---

## Where We Are

Chapter 26 taught TrenchBroom to speak QEngine and drew a throwaway box, `smoke.map`. Chapter 27 wrote the
parser that reads a `.map` into a `MapData` tree. Chapter 28 turned that tree into a walkable, collidable,
textured room and wired `main.cpp` so any `.map` on the command line becomes the running level — with the
hard-coded showcase kept one argument behind a `showcase` flag as a regression net.

So the pipeline is complete: editor → parser → geometry → entities → scene. What's *missing* is content.
`smoke.map` is a bare box with one light. The engine's real showreel — doors, lifts, teleporters, a lava
hazard, the full arsenal, coloured lighting, two grunts — still lives in C++, in
`src/engine/level/showcase_descriptor.cpp`. This chapter retires that. **We rebuild the entire showcase as
an authored map, `assets/maps/showcase.map`**, using nothing but TrenchBroom and the entity palette from
Chapter 26. No engine code is written here — this is a pure authoring chapter — but it's the chapter that
proves the pipeline can carry a *real* level, and it's where you learn the two things every level designer
needs: how to link entities, and how to think about coordinates.

Everything below is grounded in the file it produces, `assets/maps/showcase.map`, in the mapping table in
`assets/maps/README.md`, in the entity palette `tb/QEngine.fgd`, and — for the one genuinely subtle part —
in the loader's conversion code, `src/engine/level/map_to_descriptors.cpp`.

---

## Step 1: The Goal — Reproduce the Showcase as Data

Open `src/engine/level/showcase_descriptor.cpp` and `assets/maps/README.md` side by side. The descriptor is
a hand-written `std::vector<SpawnParams>`: a player, a sun, five lights, a shelf, three demo cubes, a door
and its trigger, a lift and its trigger, a teleporter and its destination, a lava pool and its hurt
volume, a rack of weapons and items, and two grunts. The README's rebuild table is the map of that
descriptor onto TrenchBroom classes — every showcase element has a matching class in the FGD, and the
property keys are identical to the descriptor so the loader maps them 1:1:

```
| Showcase element | TrenchBroom class | Key properties |
|------------------|-------------------|----------------|
| Player @ (15, 1.7, 15) | `info_player_start` | `angle` |
| Sun | `light_environment` | `direction`, `_color`, `ambient` |
| Ceiling / torch lights | `light` | `_color`, `ambient`, `linear`, `quadratic` |
| Door → `door1` | `func_door` | `endpos`, `speed`, `wait`, `startdelay`, `targetname` |
| Lift → `lift1` | `func_plat` | `endpos`, `speed`, `wait`, `startdelay`, `targetname` |
| Door / lift trigger zones | `trigger_multiple` | `target`, `cooldown` |
| Teleporter + destination | `trigger_teleport` + `info_teleport_destination` | `target` / `targetname` |
| Lava surface + damage | `func_decor` + `trigger_hurt` | `dmg`, `cooldown` |
| Pickups | `item_*` / `weapon_*` | `amount` |
| Enemies | `monster_grunt` | `angle` |
```

The goal is to make the descriptor redundant: author a `.map` that spawns the same cast, so that launching
`QEngine.exe assets/maps/showcase.map` gives you a level indistinguishable from the in-code one. Because the
loader (Chapter 28) reuses the exact same `spawnScene` dispatch the descriptor feeds, "the same keys" really
does mean "the same behaviour" — a `func_door` with `endpos`/`speed`/`wait` spawns the identical door whether
those keys came from a `SpawnParams` initialiser or from a line of `.map` text.

> **Why bother rebuilding a level we already have working in code?** Because "working in code" is exactly the
> thing we spent three chapters escaping. The descriptor arena is the proof-of-concept for *level-as-code*,
> and it has every flaw that motivated TrenchBroom: you place an entity by typing `glm::vec3(15.0f, 1.7f,
> 15.0f)` and recompiling to see where it landed, nobody but a C++ programmer can touch it, and the level
> and the engine are welded together in one translation unit. Rebuilding it as `showcase.map` does two
> things at once. It **stress-tests the loader** against the fullest content the engine has — if doors,
> lifts, teleporters, triggers, and coloured lights all survive the round trip through the parser and the
> transform, the pipeline is real, not a smoke-test toy. And it produces the artefact that lets us
> eventually **delete `showcase_descriptor.cpp` altogether**, which is the moment level-as-code is finally
> gone. This chapter is that rebuild.

---

## Step 2: The Room Shell (Recap)

Everything sits inside a closed box, and you build it exactly the way Chapter 26 built `smoke.map`: draw a
big brush, **Edit → Make Hollow** to carve it into six slabs (four walls, a floor, a ceiling), and texture
the faces. The showcase shell is larger — roughly 960 units across and textured `grid_grey` throughout —
and TrenchBroom writes it as the first block of the file, under `worldspawn`. Here is the top of
`assets/maps/showcase.map`, one shell brush shown in full:

```
// Game: QEngine
// Format: Standard
// entity 0
{
"classname" "worldspawn"
// brush 0
{
( 464 480 96 ) ( 464 -480 96 ) ( 464 480 -96 ) grid_grey 0 0 0 1 1
( 480 -480 -96 ) ( 464 -480 -96 ) ( 480 -480 96 ) grid_grey 0 0 0 1 1
( 480 480 -96 ) ( 464 480 -96 ) ( 480 -480 -96 ) grid_grey 0 0 0 1 1
( 480 -480 96 ) ( 464 -480 96 ) ( 480 480 96 ) grid_grey 0 0 0 1 1
( 480 480 96 ) ( 464 480 96 ) ( 480 480 -96 ) grid_grey 0 0 0 1 1
( 480 -480 96 ) ( 480 480 96 ) ( 480 -480 -96 ) grid_grey 0 0 0 1 1
}
```

Brush 0 is the `+X` wall slab (`x` from 464 to 480); brushes 1–3 are the other three walls, brush 4 the
ceiling, and brush 5 the floor. As Chapter 28 showed, the loader reduces each worldspawn brush to its
axis-aligned bounding box and stamps six textured quads from it, so these six slabs become the room you
stand in. There is nothing new to learn about the shell — it's the Chapter-26/28 workflow at a bigger scale
— so we spend no more time on it and move to the part this chapter is really about: the entities.

> **Why texture the whole shell one texture (`grid_grey`) rather than varying it wall by wall?** Because the
> shell is the backdrop, and the interesting visual information in the showcase comes from the *entities* —
> the orange door, the green lift, the red lava, the coloured torch pools washing over the grey. A uniform
> shell reads as "the room," and against it every entity stands out as "a thing in the room." It also keeps
> the geometry cheap: the per-texture mesh split (Chapter 28) builds one mesh per distinct texture, so a
> single-texture shell is a single mesh, and the texture variety in the scene lives where it earns its
> keep.

---

## Step 3: Point Entities — Spawn, Lights, Pickups, Grunts

A **point entity** is just an origin and a bag of properties — no brushes. You place one by selecting its
class from the entity browser and clicking in the viewport; TrenchBroom writes a small `{ … }` block of
`"key" "value"` pairs. The showcase's point entities are the player start, the lights, all the pickups, and
the two grunts. They're the easy half of the level, and the showcase leans hard on **factory defaults** to
keep them terse.

### The player start

```
// entity 1
{
"classname" "info_player_start"
"origin" "0 -384 -56"
}
```

One key, `origin`, in Quake units and Z-up — the last component (`-56`) is the *up* coordinate. There's no
`angle` here, so the spawn faces the factory default. Drop the entity, position it near the floor, done.

### The lights — a sun, a white lamp, and three coloured torches

The showcase has three *kinds* of light, and they show off how much the FGD defaults do for you. First the
sun — a single `light_environment`:

```
// entity 15
{
"classname" "light_environment"
"origin" "-8 -168 72"
}
```

Notice what's *not* there: no `direction`, no `_color`, no `ambient`. The FGD declares defaults for all
three (`direction` `"-0.2 -1.0 -0.3"`, `_color` `"1 1 1"`, `ambient` `"0.08"`), and the factory uses them
when a key is absent — so a bare `light_environment` is a plain white sun coming from the default angle. You
only type a key when you want to *differ* from the default.

Then a plain white point light — again just an origin, defaulting to white:

```
// entity 2
{
"classname" "light"
"origin" "40 456 -8"
}
```

And the three coloured torches, which *do* set `_color` because their whole point is to be coloured:

```
// entity 24
{
"classname" "light"
"origin" "72 456 -8"
"_color" "3 0.2 0.2"
}
// entity 25
{
"classname" "light"
"origin" "104 456 -8"
"_color" "0.2 3 0.2"
}
// entity 26
{
"classname" "light"
"origin" "136 456 -8"
"_color" "0.2 0.2 3"
}
```

Read the colours: `3 0.2 0.2` is a strong red (the red channel is **3.0** — well above 1.0, which is why
the FGD types `_color` as `string` and not the 0–255 `color` type; an HDR value over 1 must survive
verbatim). `0.2 3 0.2` is green, `0.2 0.2 3` blue. Three torches in a row down one wall, each throwing a
tight coloured pool — the same red/green/blue trio the descriptor hard-codes, now three three-line entity
blocks.

### The pickups

Every weapon and item is a point entity with just an origin — the FGD gives each an `amount` default, so a
bare pickup grants the default ammo. The showcase lines them up along a wall:

```
// entity 3
{
"classname" "weapon_grenadelauncher"
"origin" "-398 -366 -80"
}
```

…and so on for `weapon_lightninggun`, `weapon_nailgun`, `weapon_railgun`, `weapon_supershotgun`, then the
items `item_armor`, `item_cells`, `item_health`, `item_nails`, `item_rockets`, `item_shells` — each a
three-line block differing only in `classname` and `origin`. You place the whole arsenal by clicking a dozen
times.

### The grunts

```
// entity 14
{
"classname" "monster_grunt"
"origin" "-176 368 -80"
}
// entity 27
{
"classname" "monster_grunt"
"origin" "-80 368 -80"
}
```

Two grunts, side by side. That's the same enemy you spent Chapters 22–25 making shootable and path-finding
— now a thing you drop into a room by clicking.

> **Why does the showcase set so few keys — a bare `light_environment`, origin-only pickups — instead of
> being explicit about every property?** Because the FGD defaults *are* the descriptor's values, by design
> (Chapter 26 wrote the FGD to mirror the descriptor). Typing a key you don't want to change adds nothing
> but a chance to fat-finger it, and it hides the keys that *do* matter. A `light` with only an origin is a
> plain white lamp; a `light` with `"_color" "3 0.2 0.2"` is *obviously* the red torch, because the one line
> present is the one thing that's special about it. Leaning on defaults makes each entity block a diff
> against "the ordinary one of these," which is exactly what you want to read at a glance in a level of
> forty entities.

---

## Step 4: Brush Entities and the `target`/`targetname` Linking Pattern

Here's the real lesson. A **brush entity** is geometry — a brush you draw — that carries a behaviour instead
of being static world. A door *is* a brush tagged `func_door`; a trigger volume *is* a brush tagged
`trigger_multiple`. You make one by drawing the brush, then assigning it a class from the palette instead of
leaving it as worldspawn. The loader (Chapter 28) derives the entity's origin and collider size from the
brush's bounding box, exactly like it does for world geometry.

The interactive parts of the showcase are all **pairs**: a mover plus the trigger that activates it. They're
wired together by the **`target`/`targetname`** convention from the FGD's base classes (Chapter 26):

- the **mover** carries a `targetname` — its name;
- the **trigger** carries a `target` — the name it fires.

Match the two strings and they're linked. TrenchBroom even draws a line between a trigger and its target in
the viewport, so you can see the wiring. Get the two strings to match and the loader's `spawnScene` does the
rest — its second pass resolves `target`→`targetname` into a live link.

### The door and its trigger — linked by `door1`

The door is a thin brush tagged `func_door`, named `door1`, textured orange:

```
// entity 16
{
"classname" "func_door"
"targetname" "door1"
"endpos" "144 217 28"
"texture" "grid_orange"
// brush 0
{
( 112 208 -80 ) ( 112 208.39583333333334 -80 ) ( 112 208 -75.50000000000001 ) grid_orange 0 0 0 1 1
… five more face lines …
}
}
```

And the trigger that opens it — a separate brush (a volume in front of the door) tagged `trigger_multiple`,
whose `target` is the string `door1`:

```
// entity 18
{
"classname" "trigger_multiple"
"target" "door1"
// brush 0
{
( 112 176 -80 ) ( 112 177 -80 ) ( 112 176 -79 ) __TB_empty 0 0 0 1 1
… five more face lines …
}
}
```

That's the whole pattern: the door says `"targetname" "door1"`, the trigger says `"target" "door1"`, and
they are now one mechanism. Walk into the trigger volume in-game and `door1` slides open. Note the trigger's
brush is textured `__TB_empty` — TrenchBroom's placeholder for "no texture" — because a trigger is an
invisible sensor; it's never drawn, so its faces don't need a real texture. (That `__TB_empty` is harmless
*here*, on a trigger; leave it on a worldspawn brush by accident and it's a bug — see Step 6.)

### The lift and its trigger — linked by `lift1`

Identical shape, different classes. The lift is a pad brush tagged `func_plat`, named `lift1`, textured
green:

```
// entity 19
{
"classname" "func_plat"
"targetname" "lift1"
"endpos" "416 -32 40"
"texture" "grid_green"
// brush 0
{
( 384 -64 -95.01045637636956 ) ( 384 -63 -95.01045637636956 ) ( 384 -64 -94.01045637636956 ) grid_green 0 0 0 1 1
… five more face lines …
}
}
```

And its trigger, `target` `lift1`, with a shorter re-fire `cooldown` than the default:

```
// entity 20
{
"classname" "trigger_multiple"
"target" "lift1"
"cooldown" "0.5"
// brush 0
{
( 384 -64 -78.5 ) ( 384 -63 -78.5 ) ( 384 -64 -78.4375 ) __TB_empty 0 0 0 1 1
… five more face lines …
}
}
```

Same recipe: matching strings (`lift1`), mover carries `targetname`, trigger carries `target`. The
`cooldown` key overrides the FGD default (`1.0`) with `0.5` so the lift re-triggers quickly.

### The teleporter and its destination — linked by `tp_dest1`

The third pair links a `trigger_teleport` to an `info_teleport_destination` — same string convention, but
here the destination is a *point* entity (where you come out) rather than a mover. The trigger volume:

```
// entity 21
{
"classname" "trigger_teleport"
"target" "tp_dest1"
// brush 0
{
( 112 -416 -80 ) ( 112 -415 -80 ) ( 112 -416 -79 ) wall 0 0 0 1 1
… five more face lines …
}
}
```

And the destination point entity it sends you to, carrying the matching `targetname`:

```
// entity 22
{
"classname" "info_teleport_destination"
"origin" "376 392 -72"
"targetname" "tp_dest1"
}
```

Step into the teleport volume and you're moved to `tp_dest1`'s origin, in the far corner. Note that here the
`targetname` is on a *point* entity — the linking convention doesn't care whether the target is a brush
mover or a point marker; it only cares that the strings match.

### The lava hazard

The lava is a `trigger_hurt` volume — a brush that damages whatever stands in it. It has no partner (nothing
targets it and it targets nothing); it just sits there and hurts:

```
// entity 23
{
"classname" "trigger_hurt"
// brush 0
{
( 32 -115 -80 ) ( 32 -113.9375 -80 ) ( 32 -115 -79.8671875 ) grid_red 0 0 0 1 1
… five more face lines …
}
}
```

It uses the FGD defaults for `dmg` (25/sec) and `cooldown` (0), so no keys beyond the classname are needed.

> **Why link entities by matching name strings rather than, say, by proximity or by drawing an explicit
> connection?** Because names are unambiguous and position-independent. A trigger and the door it opens are
> often nowhere near each other — the trigger is the approach corridor, the door is the wall at the end — so
> "the nearest mover" would be a guess that breaks the moment two mechanisms sit close together. A shared
> string like `door1` is an *exact* statement of intent that survives moving either brush anywhere in the
> level, survives the round trip through the text `.map` file, and survives the loader's two-pass spawn
> (pass one spawns everything and records each `targetname`; pass two resolves each `target` against that
> table). It's the same reason the engine's factories were built around named links in the first place
> (Chapter 18): a name is a stable identity, and geometry is not.

---

## Step 5: The Coordinate Gotcha — `endpos` Is in Map Space

This is the part that catches every mapper the first time, and it's worth being precise about because
getting it wrong sends a door sliding to a coordinate thirty-two times too far away.

A `func_door`/`func_plat` has an `endpos` key: the absolute world position the mover travels *to* when it
opens. The question is: **in which coordinate space do you type `endpos`?** The brush you drew is in map
space (Z-up, Quake units). The engine runs in engine space (Y-up, small float units). So which one goes in
the key?

The answer is **map space**, and the reason is that the loader converts it for you — the same one-place
transform that converts every brush vertex. Look at `src/engine/level/map_to_descriptors.cpp`, the function
that turns a `MapEntity` into a `SpawnParams`:

```cpp
        // Vector-valued props factories read back by name must cross into engine
        // space here (props are otherwise raw map-space strings).
        auto convertPoint = [&](const char* key)
        {
            if (!e.has(key)) return;
            std::istringstream ss(e.getString(key));
            glm::vec3 m(0.0f);
            if (ss >> m.x >> m.y >> m.z)
                p.props[key] = vec3ToStr(qmap::mapPointToEngine(m));
        };
        auto convertDir = [&](const char* key)
        {
            if (!e.has(key)) return;
            std::istringstream ss(e.getString(key));
            glm::vec3 m(0.0f);
            if (ss >> m.x >> m.y >> m.z)
                p.props[key] = vec3ToStr(qmap::mapDirToEngine(m));
        };
        convertPoint("endpos");    // func_door/func_plat travel target (world pos)
        convertPoint("velocity");  // prop_dynamic initial velocity (linear)
        convertDir("direction");   // light_environment sun vector
```

Three spatial keys get converted on the way in: `endpos` and `velocity` are *positions* (run through
`mapPointToEngine` — axis swap **and** scale), and `direction` (the sun vector) is a *direction* (run
through `mapDirToEngine` — axis swap only, no scale, since a direction has no length that should shrink).
The rule that falls out of this is simple and absolute:

> **Any vector key you type in TrenchBroom — `endpos`, `velocity`, `direction` — is authored in MAP space.
> The loader converts it. Never type engine units into these keys.**

Which means the natural way to author `endpos` is to *read it off the brush you drew*, in the same map
coordinates. Work the lift example. Its pad brush spans, in map units, roughly `x` 384→448, `y` −64→0, with
the pad surface near `z ≈ −79`. The centre of that pad in `x`/`y` is `(416, −32)`. Now look at the lift's
`endpos`:

```
"endpos" "416 -32 40"
```

`416` and `−32` are **exactly the pad's centre X and Y** — the lift rises straight up, so it doesn't move in
`x` or `y` at all — and `40` is the raised Z (up from the pad's ~−79 to +40, so it travels ~119 map units
upward). You didn't compute anything in engine space; you took the pad's own map coordinates, kept X and Y,
and raised Z. The loader then runs `mapPointToEngine(416, −32, 40)` → `(416, 40, 32)/32` = `(13.0, 1.25,
1.0)` engine units, and the door factory reads *that* back out of `props["endpos"]`, already in engine
space, with no idea map space ever existed.

The door's `endpos "144 217 28"` follows the identical logic: `144`/`217` sit at the centre of the door
brush's footprint (it slides straight up, no horizontal travel), and `28` is the raised top of its travel.

> **Why make the mapper type `endpos` in map space and convert it, rather than in engine units to match what
> the factory ultimately uses?** Because the mapper is *looking at map space* — the brush, its grid, its
> coordinates are all Quake units in the editor — and the only sane way to say "this pad rises to *here*" is
> in the coordinates you can see. Forcing engine units into the key would mean doing the axis-swap-and-scale
> in your head for every mover, against a grid that isn't showing you engine units, which is exactly the
> error-prone hand-conversion the whole `map_transform` boundary exists to abolish (Chapter 28, Step 1).
> Instead the key crosses the *same* boundary every vertex crosses, in the *same* one place, so a mover's
> travel target is authored in the same space as the brush it belongs to — read X/Y straight off the pad,
> raise Z — and the loader guarantees it lands consistently with the geometry. Cross the boundary once, for
> vertices *and* for the properties that name positions.

---

## Step 6: Authoring Pitfalls We Actually Hit

Building the showcase turned up three failure modes worth naming, because each is quiet — the map loads, but
something is subtly wrong.

### A stray untextured (`__TB_empty`) worldspawn brush renders as garbage

TrenchBroom textures a freshly drawn brush with `__TB_empty`, its "no texture yet" placeholder. On a
**trigger** brush that's fine (Step 4) — triggers are never drawn. But leave a `__TB_empty` brush in
**worldspawn** — a stray box you drew and forgot to texture, or one you meant to make into a trigger but
never re-classed — and it becomes level geometry. The loader picks the brush's texture by majority vote
(Chapter 28's `majorityTexture`), so it dutifully tries to render six faces with the texture *named*
`__TB_empty`, which doesn't exist in `assets/textures`. The result is a broken, garbage-textured slab
floating in your room. The fix is discipline: **every worldspawn brush must carry a real texture**; only
trigger/mover brushes (which aren't rendered as world) may stay `__TB_empty`. If you see a corrupt surface
in-game, hunt for an untextured worldspawn brush first.

### A texture typo is silently rescued by majority vote

The flip side of `majorityTexture` is forgiving in a way that can hide a mistake. Because the loader
represents a brush by *one* texture — whichever most of its six faces use — a single mistyped face texture
(say five faces `grid_grey` and one accidental `gird_grey`) is simply outvoted: the brush still renders
`grid_grey` and nothing looks wrong. That's convenient, but it means a typo on a *minority* of faces leaves
no visible trace, and a typo that happens to hit the *majority* silently swaps the whole brush's texture.
When a brush comes out the wrong texture with no obvious cause, check every face's name — majority vote will
have hidden the odd one out.

### Missing optional keys fall back to factory defaults

The showcase's terseness (Step 3) relies on this, and it's usually what you want — but it's also a trap when
you *meant* to set a key and didn't. A `light_environment` with no `direction` isn't an error; it's a sun
pointing the FGD's default way (`-0.2 -1.0 -0.3`). A `func_door` with no `speed` isn't broken; it opens at
the default `3.0`. Every optional key silently defaults, so a forgotten key produces a *working* entity with
the *wrong* value and no diagnostic. The habit that saves you: type a key **only** to differ from the
default (which keeps blocks readable), but when a mover behaves oddly, remember that "the key isn't there"
means "the default is in force," not "nothing is happening."

> **Why does the loader forgive so much — outvoting a bad texture, defaulting a missing key — instead of
> erroring on anything unexpected?** Because a level editor's output is inherently messy: brushes get drawn
> and re-textured and re-classed, keys get left at defaults on purpose, and a loader that halted on every
> `__TB_empty` face or absent key would reject nine good maps to catch one mistake. The forgiveness is the
> right default — it lets the terse, default-leaning authoring style of Step 3 exist at all. The cost is
> that the failures are *quiet*, which is why they're worth cataloguing here: knowing that majority vote can
> hide a typo, and that a missing key is a silent default rather than an error, turns a baffling "why does
> this look wrong with no message?" into a short checklist. Forgiving loaders trade loud failures for quiet
> ones; the antidote is knowing exactly which quiet failures to look for.

---

## Step 7: Loading Your Map

The wiring to load a `.map` was built in Chapter 28; here's how you point it at the showcase. `main.cpp`
reads the map path from the command line:

```cpp
int main(int argc, char** argv)
{
	std::string mapPath = (argc > 1) ? argv[1] : "assets/maps/smoke.map";  // ""/"showcase" → showcase
	if (mapPath == "showcase") mapPath.clear();
```

So there are four ways to launch, and it's worth knowing all of them:

- **`QEngine.exe`** (no argument) → loads the default, `assets/maps/smoke.map`.
- **`QEngine.exe assets/maps/showcase.map`** → loads **your authored showcase** — the map this chapter built.
- **`QEngine.exe showcase`** → the literal word `showcase` clears `mapPath`, taking the fallback branch to
  the **hard-coded** arena in `showcase_descriptor.cpp` (the regression net).
- **`QEngine.exe ""`** → an empty argument also clears the path to the hard-coded arena.

The distinction between the middle two is the whole point of this chapter: `assets/maps/showcase.map` is the
level as **data**, parsed at runtime; the bare word `showcase` is the same level as **code**, compiled in.
Run them back to back and they should look identical — that's the parity check that says the rebuild worked.

For day-to-day debugging, `.vscode/launch.json` already points at the authored map, so pressing **F5** loads
`showcase.map` under the debugger:

```json
        {
            "name": "QEngine Debug",
            "type": "cppdbg",
            "request": "launch",
            "program": "${workspaceFolder}/build/QEngine.exe",
            "args": ["assets/maps/showcase.map"],
```

To debug the in-code fallback instead, change that `args` entry to `["showcase"]` (or `[""]`). Both debug
configurations in the file carry the same `args`, so either one launches straight into the authored
showcase.

> **Why keep the hard-coded showcase reachable at all once `showcase.map` reproduces it?** Because it's the
> reference the rebuild is *measured against*, and you don't delete the reference while you're still checking
> your work against it. Being able to flip between `assets/maps/showcase.map` (data) and `showcase` (code)
> with a one-word argument means any divergence — a torch the wrong colour, a lift that stops short, a
> pickup in the wrong spot — is a direct A/B comparison, not a memory test. The in-code arena is also the
> known-good fallback if a map edit breaks something: the engine always has a level it can load. Only once
> `showcase.map` is a pixel-for-pixel match, sustained across changes, does deleting the descriptor become
> safe — and until then, the cheap flag that keeps both paths alive is exactly what makes the rebuild
> verifiable.

---

## What Changed — Summary

No engine code changed — this is an authoring chapter. The artefact is one new map file, built entirely in
TrenchBroom from the Chapter-26 palette:

| Part of `assets/maps/showcase.map` | What you authored |
|------------------------------------|-------------------|
| `worldspawn` shell (brushes 0–5) | The closed box room — four walls, floor, ceiling — all `grid_grey`, drawn with **Make Hollow** (Chapters 26/28). |
| `info_player_start` | Point entity, `origin` only — the spawn, near the floor. |
| `light_environment` (the sun) | Bare point entity — no keys, so default direction/colour/ambient from the FGD. |
| `light` × 4 | One plain white (origin only), three coloured torches setting `_color` to HDR red/green/blue (`3 0.2 0.2`, etc.). |
| `weapon_*` × 5, `item_*` × 6 | Origin-only pickups lined along a wall; `amount` left at FGD defaults. |
| `monster_grunt` × 2 | Two origin-only grunts side by side. |
| `func_door` + `trigger_multiple` | Mover named `targetname "door1"` (with `endpos`, `texture`), trigger with `target "door1"` — the linking pattern. |
| `func_plat` + `trigger_multiple` | Lift named `targetname "lift1"` (`endpos`, `texture`), trigger `target "lift1"` with `cooldown "0.5"`. |
| `trigger_teleport` + `info_teleport_destination` | Teleport volume `target "tp_dest1"` linked to a destination point entity carrying `targetname "tp_dest1"`. |
| `trigger_hurt` | Standalone lava-damage volume; `dmg`/`cooldown` left at FGD defaults. |
| `prop_dynamic` | Physics demo cube point entity. |

The launch wiring (`src/main.cpp`, `.vscode/launch.json`) was already in place from Chapter 28 — this chapter
just uses it, pointing `args` at `assets/maps/showcase.map`. `src/engine/level/showcase_descriptor.cpp` is
now redundant content (the map reproduces it) but is deliberately kept as the regression fallback behind the
`showcase` argument.

---

## What You Should See

1. **`QEngine.exe assets/maps/showcase.map` drops you into the full arena** — grey room, the door, the green
   lift, the lava, the coloured torch wall, the weapon and item rack, two grunts — all from a hand-authored
   `.map`, not from compiled code.
2. **The door and lift work.** Walk into the door's trigger volume and `door1` slides up; step onto the lift
   pad's trigger and `lift1` rises to its `endpos` — proof the `target`/`targetname` links resolved and the
   map-space `endpos` converted correctly.
3. **The teleporter sends you across the room** to `tp_dest1`'s corner — the trigger→destination link intact.
4. **The lava hurts**, the torches throw red/green/blue pools, the sun lights the room from its default
   angle — every entity behaving as the descriptor's does.
5. **`QEngine.exe showcase` looks identical.** Flipping to the in-code arena is your A/B check: the authored
   map and the compiled descriptor should be indistinguishable. Any difference is a bug in the rebuild.
6. **No broken surfaces.** If a slab renders as garbage, you left a `__TB_empty` brush in worldspawn
   (Step 6) — texture it and reload.

---

## What's Next

The showcase is now **data**. The engine's richest level — doors, lifts, teleporters, a hazard, the full
arsenal, coloured lighting, enemies — is a plain-text `.map` a designer authored by eye in a real editor,
parsed and spawned through the exact same pipeline the smoke test uses. That closes the loop the TrenchBroom
chapters opened: from Chapter 26's "levels are code and that doesn't scale" to a chapter where the flagship
level is a file you could hand to someone who has never opened the `src/` tree.

The obvious next move is the one Step 7's callout guards against doing prematurely: once `showcase.map` holds
parity across a few rounds of changes, **delete `showcase_descriptor.cpp`** and let the map stand alone —
retiring level-as-code for good. Beyond that, the geometry is still AABB-fidelity (Chapter 28's documented
follow-up): the showcase shell is axis-aligned boxes, so it's lossless today, but the first ramp or angled
buttress a designer wants will need the general brush geometry that slots in behind the same `Surface`
output. Either way, the authoring workflow itself is finished — you can build a QEngine level, and the engine
will play it.
