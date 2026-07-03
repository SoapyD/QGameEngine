# Chapter 20a: Audio Assets & the Manifest

## What You'll Learn
- Why a game needs a *named* sound collection — code that loads audio by id, never by raw path
- The manifest pattern: `manifest.json` maps logical ids (`"weapon.shotgun"`) to file paths
- A tiny stdlib-only synth (`gen_placeholders.py`) that fills every slot with a beep/noise/loop
- Building that synth from four primitives — `tone`, `glide`, `noise`, `env` — plus `mix`/`seq`/`arp`
- Making the generator *idempotent*: skip files that already exist, `--force` to regenerate
- Importing real audio from OpenArena `.pk3` archives (`import_openarena.py`) onto the same slots
- Organising the files under `assets/sounds/` and documenting the placeholder + licensing story

---

## Where We Are

The engine can shoot, take damage, and hand out pickups (Chapter 19) — in total silence. Before
we write a single line of audio-*engine* code, we need something for it to play. That is this
chapter's whole job: build the **asset pipeline** so that, by the end, there is a folder of
sound files and one JSON file that says *which sound is which*.

This is deliberately the first of three audio parts:

- **20a (this chapter)** — the assets and the manifest. No engine, no gameplay wiring.
- **20b — The Audio Engine** — loading the manifest and actually making noise (miniaudio).
- **20c — Wiring Sound Events** — making the shotgun, the lava, the pickups *trigger* sounds.

We separate them because the asset problem is genuinely independent: the game needs a stable
*catalogue* of sounds with fixed names, and it needs that catalogue to exist whether or not the
final audio is any good. We solve that with placeholders now, and swap in real audio later
without touching a line of engine code.

---

## Step 1: The Problem — Sounds by Name, Not by Path

Imagine the combat system wanting to play the shotgun blast. The naive approach is to hard-code
the path where the shot is fired:

```cpp
playSound("assets/sounds/sfx/weapons/shotgun.wav"); // ← don't do this
```

Now that path is duplicated across every file that fires a shotgun. Rename the file, change its
folder, or swap the `.wav` for an `.ogg`, and you are hunting through C++ for string literals.
Worse, you cannot ship a *placeholder* shotgun and quietly replace it with a real one later —
the "real" filename is baked into the code.

The fix is a layer of indirection borrowed from every serious asset pipeline: gameplay code
asks for a **logical id** — a stable name like `"weapon.shotgun"` — and a **manifest** resolves
that name to a file on disk. The id is the contract; the path is an implementation detail.

> **Why an id instead of a path?** The id is what your *design* cares about ("the shotgun fire
> sound"); the path is where that sound happens to live *today*. Decoupling them means moving,
> renaming, or re-formatting a file is a one-line manifest edit, never a code change. It also
> lets us fill every id with a throwaway placeholder now and replace the files one at a time,
> with the game none the wiser.

So the two artefacts we build in this chapter are:

1. `assets/sounds/manifest.json` — the id→path table.
2. The sound files themselves, under `assets/sounds/`.

And the two *tools* that produce them:

1. `scripts/audio/gen_placeholders.py` — synthesises a placeholder for every slot.
2. `scripts/audio/import_openarena.py` — overwrites those placeholders with real OpenArena audio.

We'll build the synth first, because it defines the full set of ids the game needs.

---

## Step 2: The Placeholder Generator — Header & Setup

Create `scripts/audio/gen_placeholders.py`. The docstring states the contract up front — these
are *synthesised* sounds, not real game audio, and the whole point is that real files can later
take their place at the same paths:

```python
#!/usr/bin/env python3
"""Generate placeholder audio for QEngine.

These are SYNTHESISED beeps / noise bursts / simple loops — NOT real game audio.
They exist so the audio system can be built and tested against real files, and so
every sound the game needs has a slot. Replace the .wav files with real assets
(same paths) and the manifest keeps working. See assets/sounds/README.md.

    python scripts/audio/gen_placeholders.py

Writes 16-bit mono WAVs under assets/sounds/ and regenerates manifest.json.
stdlib only; deterministic (fixed seed).
"""
import json
import math
import os
import random
import struct
import wave

SR = 22050  # placeholder quality; small files
random.seed(42)

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
SOUNDS = os.path.join(ROOT, "assets", "sounds")
```

Three decisions worth calling out:

- **`SR = 22050`** — half CD rate. Placeholders don't need fidelity; they need to be small and
  to exist. Real replacements can be any sample rate the audio backend reads.
- **`random.seed(42)`** — the noise bursts are pseudo-random, but the seed is fixed, so running
  the script twice produces byte-identical files. Determinism means the generated assets don't
  churn in version control.
- **stdlib only** — `wave`, `struct`, `math`, `random`. No `numpy`, no external audio libs, so
  anyone can run it with a bare Python install.

> **Why compute `ROOT` from the script's own location?** `os.path.dirname(os.path.abspath(__file__))`
> is `scripts/audio/`; two `..` climb back to the repo root. This means the script writes to the
> right place *regardless of the working directory you run it from* — you can invoke it from the
> repo root, from `scripts/`, or from an IDE, and it always finds `assets/sounds/`.

---

## Step 3: The Synth Toolkit — Four Primitives

Every placeholder is built from a handful of tiny functions that each return a **list of float
samples** in the range `[-1.0, 1.0]`. First, the oscillator — `tone` produces a fixed-frequency
wave in one of four shapes:

```python
def tone(freq, dur, kind="sine", vol=0.5):
    n = int(dur * SR)
    out = []
    for i in range(n):
        t = i / SR
        ph = 2 * math.pi * freq * t
        if kind == "sine":
            v = math.sin(ph)
        elif kind == "square":
            v = 1.0 if math.sin(ph) >= 0 else -1.0
        elif kind == "saw":
            v = 2 * ((freq * t) % 1.0) - 1.0
        elif kind == "tri":
            v = 2 * abs(2 * ((freq * t) % 1.0) - 1) - 1
        else:
            v = 0.0
        out.append(v * vol)
    return out
```

`sine` is a pure tone; `square` is harsh and buzzy (good for a nailgun); `saw` is bright and
ripping (rockets); `tri` is soft (UI/menu). `dur * SR` is simply "seconds × samples-per-second =
number of samples."

Next, `glide` — a tone that *sweeps* from one frequency to another. This is what gives a rocket
launch its downward "whoosh" or the railgun its descending zap:

```python
def glide(f0, f1, dur, kind="sine", vol=0.5):
    n = int(dur * SR)
    out, phase = [], 0.0
    for i in range(n):
        f = f0 + (f1 - f0) * (i / max(1, n))
        phase += 2 * math.pi * f / SR
        v = math.sin(phase) if kind == "sine" else (1.0 if math.sin(phase) >= 0 else -1.0)
        out.append(v * vol)
    return out
```

> **Why accumulate `phase` instead of recomputing `sin(2πft)` like `tone` does?** When the
> frequency changes every sample, `2π·f·t` produces *discontinuities* — the wave jumps, which
> you hear as clicks. Integrating the phase (`phase += 2π·f/SR` each step) keeps the waveform
> continuous across the sweep. This is the standard way to build a frequency-modulated
> oscillator.

Then `noise` — the raw material for impacts, explosions, footsteps and wind:

```python
def noise(dur, vol=0.5):
    return [(random.random() * 2 - 1) * vol for _ in range(int(dur * SR))]
```

And the most important shaper, `env` — an amplitude **envelope**. Raw tones and noise start and
stop abruptly, which clicks; `env` fades the signal in over `atk` seconds and out over `rel`:

```python
def env(sig, atk=0.005, rel=0.05):
    n = len(sig)
    a = max(1, int(atk * SR))
    r = max(1, int(rel * SR))
    out = []
    for i, s in enumerate(sig):
        if i < a:
            g = i / a
        elif i > n - r:
            g = max(0.0, (n - i) / r)
        else:
            g = 1.0
        out.append(s * g)
    return out
```

> **Why is `env` on almost every sound?** A sample that starts at full amplitude on sample zero
> produces a sharp step in the waveform — an audible *click*. Ramping the gain from 0→1 at the
> start (attack) and 1→0 at the end (release) removes those clicks. It also shapes character: a
> short attack + long release is a "hit"; a long attack is a swell. Loopable beds (hum, wind,
> lava) deliberately *skip* `env` so their ends stay flat and repeat seamlessly.

---

## Step 4: The Composition Helpers

Four more helpers combine primitives into finished sounds. `mix` sums signals simultaneously
(and clamps to avoid clipping past `±1.0`):

```python
def mix(*sigs):
    n = max(len(s) for s in sigs)
    out = [0.0] * n
    for s in sigs:
        for i, v in enumerate(s):
            out[i] += v
    return [max(-1.0, min(1.0, x)) for x in out]
```

`seq` plays signals one after another (used for two-note jingles like the pickup chime):

```python
def seq(*sigs):
    out = []
    for s in sigs:
        out += s
    return out
```

`arp` builds a looping arpeggio from a list of note frequencies — the backbone of the music
placeholders — enveloping each note so it plucks:

```python
def arp(notes, note_dur, reps, kind="sine", vol=0.3):
    out = []
    for _ in range(reps):
        for f in notes:
            out += env(tone(f, note_dur, kind, vol), 0.01, note_dur * 0.4)
    return out
```

And `under` lays a sustained bass tone beneath a signal, to thicken the music beds:

```python
def under(sig, freq, vol=0.12):
    """Mix a sustained bass tone under a signal (for music)."""
    return mix(sig, tone(freq, len(sig) / SR, "sine", vol))
```

> **Why build a mini-DSP toolkit instead of shipping a few hand-made WAVs?** Because the
> generator is *code*, adding a new sound slot is one line (a `tone`/`noise`/`glide` recipe),
> the whole set regenerates deterministically, and there are no binary blobs to hand-edit. The
> toolkit — `tone`, `glide`, `noise`, `env`, `mix`, `seq`, `arp`, `under` — is small enough to
> read in a minute and expressive enough to fake every sound the game needs.

---

## Step 5: The Sound Collection — id → (path, recipe)

This is the heart of the file: `SOUND_DEFS`, a dictionary whose **keys are the logical ids the
game will ask for** and whose values are `(relative path, generator lambda)` pairs. A few note
frequencies are named first for readability:

```python
# ─── the collection: logical id -> (relative path, generator) ───────────────
# Keys are what the game asks for; paths are where the files live.
A = 220.0        # note frequencies (A minor-ish)
C, E, Amid = 261.63, 329.63, 440.0

SOUND_DEFS = {
    # weapons — fire sounds
    "weapon.shotgun":         ("sfx/weapons/shotgun.wav",         lambda: env(mix(noise(0.12, 0.7), tone(80, 0.12, "sine", 0.4)), 0.002, 0.09)),
    "weapon.supershotgun":    ("sfx/weapons/supershotgun.wav",    lambda: env(mix(noise(0.20, 0.8), tone(70, 0.20, "sine", 0.4)), 0.002, 0.14)),
    "weapon.nailgun":         ("sfx/weapons/nailgun.wav",         lambda: env(tone(1200, 0.05, "square", 0.3), 0.002, 0.03)),
    "weapon.rocketlauncher":  ("sfx/weapons/rocket_launcher.wav", lambda: env(mix(glide(320, 120, 0.30, "saw", 0.4), noise(0.30, 0.25)), 0.005, 0.1)),
    "weapon.grenadelauncher": ("sfx/weapons/grenade_launcher.wav",lambda: env(glide(220, 90, 0.25, "saw", 0.4), 0.005, 0.1)),
    "weapon.lightninggun":    ("sfx/weapons/lightning_gun.wav",   lambda: env(mix(tone(60, 0.20, "saw", 0.4), tone(240, 0.20, "square", 0.15)), 0.002, 0.05)),
    "weapon.railgun":         ("sfx/weapons/railgun.wav",         lambda: env(glide(1600, 300, 0.35, "sine", 0.4), 0.002, 0.12)),
    "weapon.dryfire":         ("sfx/weapons/dry_fire.wav",        lambda: env(tone(400, 0.03, "square", 0.25), 0.001, 0.02)),
    "weapon.switch":          ("sfx/weapons/weapon_switch.wav",   lambda: seq(env(tone(600, 0.04), 0.002, 0.03), env(tone(900, 0.05), 0.002, 0.03))),
    # … combat, player, pickups, world, ui, ambient, music …
}
```

Read one recipe to see how the toolkit composes. The shotgun is a **noise burst mixed with a
low sine thump, enveloped**: `env(mix(noise(0.12, 0.7), tone(80, 0.12, "sine", 0.4)), 0.002, 0.09)`
— 0.12 s of noise for the "crack", an 80 Hz sine for the "body", a 2 ms attack and 90 ms
release so it snaps then decays. The nailgun, by contrast, is a single short 1200 Hz *square*
tone — thin and electronic. Every recipe is one line, and the whole collection groups by domain:

- **weapons** — one fire sound per weapon, plus `weapon.dryfire` and `weapon.switch`.
- **combat** — `bullet_impact`, `flesh_hit`, `explosion`, `rail_zap`.
- **player** — four footsteps, `jump`, `land`, `pain`, `death`, `respawn`.
- **pickups** — `health`, `armor`, `ammo`, `weapon`.
- **world** — doors, lifts, `teleport`, `trigger`, and a loopable `lava_loop`.
- **ui** — `select`, `confirm`, `back` (for future menus).
- **ambient** — `hum_loop`, `wind_loop` (loopable beds).
- **music** — `exploration`, `action`, `menu` (loopable arpeggios via `arp`/`under`).

Note the loopable ones — `world.lava_loop`, the ambient beds, the music — omit `env` entirely so
their ends don't fade, letting them repeat without a gap:

```python
"world.lava_loop":  ("sfx/world/lava_loop.wav",  lambda: mix(noise(1.0, 0.25), tone(50, 1.0, "sine", 0.25))),  # loopable
"ambient.hum_loop": ("sfx/ambient/hum_loop.wav", lambda: tone(60, 2.0, "sine", 0.15)),
"music.exploration":("music/exploration_loop.wav",lambda: under(arp([A, C, E, Amid, E, C], 0.35, 4, "sine", 0.25), 110, 0.10)),
```

> **Why note the music path as `.wav` here when the shipped file is `.ogg`?** The synth can only
> produce WAV (that's all Python's `wave` module writes), so the *default* path for a music slot
> is a `.wav`. When real OpenArena music is imported in Step 8 it arrives as `.ogg`, and the
> importer repoints the manifest to the new extension. The generator is careful never to fight
> that — see the gap-filler logic in Step 7.

---

## Step 6: Writing a WAV

The `write_wav` helper turns a list of floats into a 16-bit mono WAV, creating parent folders as
needed:

```python
def write_wav(path, samples):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with wave.open(path, "w") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        frames = b"".join(struct.pack("<h", int(max(-1.0, min(1.0, s)) * 32767)) for s in samples)
        w.writeframes(frames)
```

Each float sample is clamped to `[-1.0, 1.0]`, scaled to the 16-bit signed range (`× 32767`),
and packed little-endian (`"<h"` = signed short). One channel, two bytes per sample — mono.

> **Why mono?** Positional 3D audio (which 20b will add) pans and attenuates a source based on
> its position in the world, and that only works on a *single* mono source — a pre-mixed stereo
> file has its spatialisation baked in and can't be placed. SFX are therefore mono by rule;
> music, which isn't positional, is allowed to be stereo.

---

## Step 7: The Idempotent Gap-Filler

`main` is where the generator earns its "safe to re-run" promise. It does **not** blindly write
every file — it *merges* with any existing manifest and only fills slots whose file is missing:

```python
def main():
    import argparse
    ap = argparse.ArgumentParser(description="Fill any empty sound slot with a synth placeholder.")
    ap.add_argument("--force", action="store_true",
                    help="regenerate ALL slots, overwriting real/imported audio too")
    args = ap.parse_args()

    # Merge into any existing manifest so imported real audio (e.g. from
    # import_openarena.py) keeps its path/extension and is not clobbered.
    manifest_path = os.path.join(SOUNDS, "manifest.json")
    sounds = {}
    if os.path.exists(manifest_path):
        with open(manifest_path, encoding="utf-8") as f:
            sounds = json.load(f).get("sounds", {})

    written = kept = 0
    for key, (default_rel, gen) in sorted(SOUND_DEFS.items()):
        rel = sounds.get(key, default_rel)  # honour an existing (maybe real) slot
        out_path = os.path.join(SOUNDS, rel.replace("/", os.sep))
        if os.path.exists(out_path) and not args.force:
            kept += 1
            continue
        # Only synth-fill .wav slots (can't synthesise into a real .ogg path).
        if not rel.endswith(".wav"):
            rel = default_rel
            out_path = os.path.join(SOUNDS, rel.replace("/", os.sep))
        write_wav(out_path, gen())
        sounds[key] = rel
        written += 1
        print(f"  synth  {key:26s} -> assets/sounds/{rel}")

    with open(manifest_path, "w", encoding="utf-8") as f:
        json.dump({"_note": "Sound slots — replace files, keep manifest ids. Some are real "
                            "imported audio; missing ones are synth placeholders.",
                   "sampleRate": SR, "sounds": sounds}, f, indent=2)
    print(f"\n  {written} synth placeholder(s) written, {kept} existing slot(s) kept.")
```

Trace the three guards, because together they make the tool *composable* with the importer:

1. **`rel = sounds.get(key, default_rel)`** — if the manifest already maps this id (maybe to a
   real, imported path), reuse that path rather than the built-in default.
2. **`if os.path.exists(out_path) and not args.force: kept += 1; continue`** — if a file already
   lives there, leave it alone. This is the idempotence: run the script ten times, and after the
   first run every subsequent run reports "0 written, N kept."
3. **`if not rel.endswith(".wav")`** — a real `.ogg` music slot cannot receive synthesised WAV
   samples, so if the merged path is an `.ogg`, fall back to the default `.wav` path rather than
   corrupt the real file.

> **Why default to gap-fill and gate full regeneration behind `--force`?** The two tools are
> meant to run *together*: import real OpenArena audio first, then run the generator to fill any
> slot the import missed. If the generator overwrote everything by default, it would clobber the
> real audio you just imported. Making "skip existing" the default and "`--force` = overwrite
> everything (even real audio)" the opt-in makes the safe path the easy one. `--force` exists for
> when you *do* want a clean, fully-synth set — e.g. to reset after experimenting.

Run it and you get the manifest and a full tree of WAVs:

```
python scripts/audio/gen_placeholders.py
```

Every id in `SOUND_DEFS` now has a file and a manifest entry — the game has a complete, named
sound catalogue built from nothing but the standard library.

---

## Step 8: Importing Real Audio from OpenArena

Synth beeps prove the pipeline works, but they sound like beeps. `import_openarena.py` upgrades
them to real audio by mapping the manifest's slots onto files inside **OpenArena's `.pk3`
archives** (a `.pk3` is just a `.zip`). You point it at a `baseoa` folder and it extracts the
mapped files over the placeholders:

```python
#!/usr/bin/env python3
"""Import OpenArena audio into QEngine's placeholder sound slots.

OpenArena (https://openarena.ws) game data is GPLv2 — treat these as PLACEHOLDERS
and see assets/sounds/CREDITS.md. Point --src at a `baseoa` folder of .pk3 files
(pk3 == zip). Overwrites the synth placeholder for each mapped slot; leaves the
rest as synth. Music slots switch to the imported .ogg (manifest updated).

    python scripts/audio/import_openarena.py --src "D:/Downloads/openarena-0.8.8/openarena-0.8.8/baseoa"
"""
import argparse
import glob
import json
import os
import zipfile
```

The mappings are three tables. `SFX_MAP` maps a manifest id to a path **inside the pk3s** — the
destination path comes from the manifest, so SFX keep their existing `.wav` slot and only the
file *contents* change:

```python
SFX_MAP = {
    "weapon.shotgun":        "sound/weapons/shotgun/sshotf1b.wav",
    "weapon.supershotgun":   "sound/weapons/shotgun/sshotf1b.wav",  # reuse shotgun for now (OA has none)
    "weapon.nailgun":        "sound/weapons/nailgun/wnalfire.wav",
    "weapon.rocketlauncher": "sound/weapons/rocket/rocklf1a.wav",
    # … the rest of the base slots …
    "pickup.health":         "sound/items/n_health.wav",
    "world.teleport":        "sound/world/telein.wav",
    "ambient.hum_loop":      "sound/world/neonhum.wav",
}
```

`EXTRA_SFX_MAP` banks **new** slots the game doesn't use yet — future weapons, powerups,
projectile loops, pain tiers — as `id -> (pk3 source, destination path)`. These *create* new
manifest ids:

```python
EXTRA_SFX_MAP = {
    # future weapons
    "weapon.machinegun":       ("sound/weapons/machinegun/machgf1b.wav", "sfx/weapons/machinegun.wav"),
    "weapon.plasma":           ("sound/weapons/plasma/hyprbf1a.wav",     "sfx/weapons/plasma.wav"),
    # weapon idle hums (loop while held/charging)
    "weapon.lightning_hum":    ("sound/weapons/lightning/lg_hum.wav",    "sfx/weapons/lightning_hum.wav"),
    # powerups
    "powerup.quad":            ("sound/items/quaddamage.wav",           "sfx/powerups/quad.wav"),
    # … projectile flys, combat impacts, pain tiers, health tiers, ambiences …
}
```

`MUSIC_MAP` is special: OpenArena music is `.ogg`, so importing it **changes the slot's
extension**, and the importer must repoint the manifest and delete the old `.wav`:

```python
MUSIC_MAP = {
    "music.exploration": ("music/OA01.ogg", "music/exploration_loop.ogg"),
    "music.action":      ("music/OA06.ogg", "music/action_loop.ogg"),
    "music.menu":        ("music/OA02.ogg", "music/menu_loop.ogg"),
    "music.ambient":     ("music/OA07.ogg", "music/ambient_loop.ogg"),
    "music.intense":     ("music/OA13.ogg", "music/intense_loop.ogg"),
}
```

The extraction machinery is two small functions. `build_index` walks every `.pk3` in the source
folder and builds a "member path → (pk3, member)" map, with **first pk3 wins** so base packs
override later ones:

```python
def build_index(baseoa):
    """member path -> (pk3 file, member) across all .pk3 in baseoa."""
    index = {}
    for pk in sorted(glob.glob(os.path.join(baseoa, "*.pk3"))):
        try:
            z = zipfile.ZipFile(pk)
        except zipfile.BadZipFile:
            continue
        for n in z.namelist():
            index.setdefault(n, (pk, n))  # first pk3 wins (pak0 before pak5/6)
    return index


def extract(index, member, dest_abs):
    if member not in index:
        return False
    pk, name = index[member]
    os.makedirs(os.path.dirname(dest_abs), exist_ok=True)
    with zipfile.ZipFile(pk) as z, open(dest_abs, "wb") as out:
        out.write(z.read(name))
    return True
```

`main` loads the manifest, runs the three maps, and rewrites the manifest. The music loop is the
interesting one — it removes the stale `.wav` before repointing the id:

```python
    # Music: import .ogg, repoint manifest, drop the old .wav placeholder.
    for mid, (member, dest_rel) in MUSIC_MAP.items():
        dest_abs = os.path.join(SOUNDS, dest_rel.replace("/", os.sep))
        if extract(index, member, dest_abs):
            old = sounds.get(mid)
            if old and old != dest_rel:
                old_abs = os.path.join(SOUNDS, old.replace("/", os.sep))
                if os.path.exists(old_abs):
                    os.remove(old_abs)
            sounds[mid] = dest_rel
            imported.append((mid, member, dest_rel))
        else:
            missing.append((mid, member + " (not in pk3s)"))
```

At the end it reports what imported and, crucially, **what is still a synth placeholder**:

```python
    still_synth = sorted(set(sounds) - {m[0] for m in imported})
    print(f"\n  imported {len(imported)} from OpenArena; "
          f"{len(still_synth)} slot(s) still synth placeholder: {', '.join(still_synth) or 'none'}")
```

> **Why keep one manifest as the single source of truth, updated by both tools?** The generator
> and the importer are two producers writing the *same* `manifest.json`. Because the generator
> merges (Step 7) and the importer repoints in place, whichever you run — in either order, any
> number of times — the manifest ends up describing exactly the files on disk. Gameplay code
> reads only that one file and never needs to know whether a given id resolves to a beep or a
> real OpenArena sample.

> **Why does `weapon.supershotgun` point at the shotgun file?** OpenArena has no super-shotgun
> sound, so the slot reuses the ordinary shotgun sample as a stand-in. The id still exists and
> still resolves — the game can reference `weapon.supershotgun` today, and a real double-barrel
> sample can drop in later with no code change. That's the manifest pattern paying off.

---

## Step 9: The Folder Structure

After running both tools, `assets/sounds/` holds the manifest plus a tidy tree grouped by domain
— `sfx/<group>/` for effects, `music/` for tracks:

```
assets/sounds/
├── manifest.json          logical id → file path (what code loads by name)
├── sfx/
│   ├── weapons/           one per weapon fire + dry-fire + weapon switch
│   ├── combat/            impacts, explosion, rail zap
│   ├── player/            footsteps, jump, land, pain, death, respawn
│   ├── pickups/           health, armour, ammo, weapon
│   ├── world/             doors, lifts, teleport, trigger, lava (loop)
│   ├── ui/                select, confirm, back (for future menus)
│   └── ambient/           hum, wind (loopable beds)
└── music/                 exploration, action, menu (loopable)
```

The manifest itself is just the id→path table. An excerpt:

```json
{
  "_note": "Sound slots — replace files, keep manifest ids. Some are real imported audio; missing ones are synth placeholders.",
  "sampleRate": 22050,
  "sounds": {
    "weapon.shotgun": "sfx/weapons/shotgun.wav",
    "pickup.health": "sfx/pickups/pickup_health.wav",
    "world.lava_loop": "sfx/world/lava_loop.wav",
    "music.action": "music/action_loop.ogg"
  }
}
```

Note `music.action` resolves to `.ogg` (imported) while the SFX are `.wav` — the manifest
absorbs that difference so code never sees it. The folder groups exist purely for humans; code
navigates entirely by id.

---

## Step 10: Documenting the Placeholders & the Licence Caveat

Two Markdown files under `assets/sounds/` make the pipeline self-explanatory and — importantly —
flag the legal caveat, because the imported audio is **not** freely licensed.

`assets/sounds/README.md` explains the placeholder workflow: which tool does what, the folder
layout, the manifest rule ("reference sounds by id, never by raw path"), and how to swap in real
audio. Its opening states the situation plainly:

```markdown
# QEngine — Audio Collection

**These are placeholder sounds.** All 78 slots hold audio imported from **OpenArena**
(GPLv2 — see [CREDITS.md](CREDITS.md)). `weapon.supershotgun` reuses the shotgun sound for now
(OpenArena has no super-shotgun). …
They exist so the audio system can be built and tested, and so every sound has a reserved slot
and name. **Replace the files with your own assets, keep the paths**, and everything keeps
working.

> ⚠️ The OpenArena assets are **GPLv2** — fine for a GPL/hobby build, but swap them out before
> any closed-source release. See [CREDITS.md](CREDITS.md).
```

It also lists the swap procedure — drop a real file at the same path (or a new path + a one-line
manifest edit), any format the backend reads, and keep SFX short and mono — plus a list of
royalty-free asset sources (Kenney CC0, OpenGameArt, Freesound, Sonniss, Incompetech) for when
you replace the placeholders for real.

`assets/sounds/CREDITS.md` tracks the source and licence of every asset, and spells out the
GPLv2 implication:

```markdown
## OpenArena (imported via `scripts/audio/import_openarena.py`)

- **Source:** OpenArena 0.8.8 — https://openarena.ws
- **Licence:** OpenArena game data is **GPL v2** …
- **Implication:** GPLv2 is copyleft. Shipping these assets means honouring GPLv2 for them
  (source availability, licence notices). **Fine for a GPL/hobby build; a blocker if you ever
  want QEngine's assets to be proprietary.** Swap these out before a closed-source release.
```

> **Why belabour the licence in two files?** GPLv2 is *copyleft* — shipping GPLv2 assets binds
> obligations onto whatever ships them. That's fine for a hobby/GPL build but a genuine blocker
> for any closed-source release. The whole placeholder architecture exists precisely so this
> swap is painless: because code binds to ids, not files, replacing every OpenArena sample with
> a CC0 one is a bag of file copies (and maybe a few manifest path edits) — never a code change.
> `CREDITS.md` even keeps an empty table ready for you to record the licence of each real asset
> as you add it.

---

## What Changed — Summary

| File | Change |
|------|--------|
| `scripts/audio/gen_placeholders.py` | **New** — stdlib synth (`tone`/`glide`/`noise`/`env`/`mix`/`seq`/`arp`/`under`); `SOUND_DEFS` id→(path, recipe) collection; idempotent gap-filler with `--force`; writes `manifest.json`. |
| `scripts/audio/import_openarena.py` | **New** — maps OpenArena `.pk3` (zip) audio onto manifest slots via `SFX_MAP` / `EXTRA_SFX_MAP` / `MUSIC_MAP`; extracts + repoints; music becomes `.ogg`. |
| `assets/sounds/manifest.json` | **New** — logical id → file path table; the single source of truth both tools maintain. |
| `assets/sounds/sfx/<group>/*.wav` | **New** — per-domain SFX (weapons, combat, player, pickups, world, ui, ambient, projectile, powerups). |
| `assets/sounds/music/*.ogg` | **New** — loopable music tracks (imported from OpenArena). |
| `assets/sounds/README.md` | **New** — placeholder workflow, folder layout, manifest rule, swap procedure, royalty-free sources. |
| `assets/sounds/CREDITS.md` | **New** — asset sources + the GPLv2 licensing caveat and an empty attribution table. |

---

## What You Should See

There is nothing to *build* or run in the engine yet — this chapter produces assets, not code.
The observable results are on disk:

1. **`python scripts/audio/gen_placeholders.py`** prints a `synth …` line per slot on a fresh
   tree, then reports e.g. `N synth placeholder(s) written, 0 existing slot(s) kept`. Run it
   again and it reports `0 written, N kept` — proof it's idempotent.
2. **`assets/sounds/`** fills with a `sfx/<group>/` + `music/` tree of small mono WAVs (and OGG
   music after import), plus a populated `manifest.json` whose `sounds` map has one entry per id.
3. **`python scripts/audio/import_openarena.py --src <baseoa>`** (with OpenArena installed)
   prints `[ok] <id> <- <pk3 path>` for each imported slot and `[skip] …` for any it couldn't
   find, then lists how many slots remain synth placeholders. The manifest now points music ids
   at `.ogg` files and the SFX WAVs hold real OpenArena audio.
4. **Playing any file** in an audio player — the WAVs are genuine, playable sounds (beeps and
   noise bursts before import; real weapon/pickup/footstep audio after).

The game still makes no noise — but every sound it will ever need now has a name, a file, and a
manifest entry, ready to be loaded.

---

## What's Next

We have a named catalogue of sounds but nothing that plays them. In **Chapter 20b: The Audio
Engine**, we bring in **miniaudio**, load `manifest.json` at startup, resolve every id to a
decoded audio buffer, and build the C++ interface the rest of the engine will call — the machine
that turns `"weapon.shotgun"` into an actual bang. Only after that (in **Chapter 20c: Wiring
Sound Events**) do we connect it to gameplay so firing, footsteps, pickups, and lava finally
make the sounds we just catalogued here.
