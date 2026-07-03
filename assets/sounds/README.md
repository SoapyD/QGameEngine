# QEngine — Audio Collection

**These are placeholder sounds.** All 78 slots hold audio imported from **OpenArena**
(GPLv2 — see [CREDITS.md](CREDITS.md)). `weapon.supershotgun` reuses the shotgun sound for now
(OpenArena has no super-shotgun). The set covers current needs plus extras banked for future
features (more weapons, powerups, projectile/hum loops, extra ambiences, pain/health tiers).
They exist so the audio system can be built and tested, and so every sound has a reserved slot
and name. **Replace the files with your own assets, keep the paths**, and everything keeps
working.

> ⚠️ The OpenArena assets are **GPLv2** — fine for a GPL/hobby build, but swap them out before
> any closed-source release. See [CREDITS.md](CREDITS.md).

Two tools populate the slots:
- `scripts/audio/gen_placeholders.py` — synthesises a beep/noise/loop for any **empty** slot
  (safe to re-run; won't clobber real audio unless `--force`).
- `scripts/audio/import_openarena.py --src <baseoa>` — maps OpenArena's `.pk3` audio onto our
  slots and overwrites their files (music slots become `.ogg`).

Formats: SFX are mono WAV; music is OGG. Real replacements can be any rate/format your audio
backend (miniaudio) reads.

## Layout

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

## The manifest

`manifest.json` maps a **logical id** (what gameplay code asks for) to a **file path**:

```json
{
  "sounds": {
    "weapon.shotgun":  "sfx/weapons/shotgun.wav",
    "pickup.health":   "sfx/pickups/pickup_health.wav",
    "music.action":    "music/action_loop.wav"
  }
}
```

Gameplay code should reference sounds by id (`"weapon.shotgun"`), never by raw path — so
swapping a file, or moving it, is a one-line manifest edit, not a code change. The planned
audio system (miniaudio) will load this manifest at startup and resolve ids to loaded
buffers.

Loop-friendly clips (designed to repeat seamlessly-ish): `world.lava_loop`,
`ambient.hum_loop`, `ambient.wind_loop`, and all three `music.*` tracks.

## Replacing placeholders with real audio

1. Drop your real file at the **same path** (e.g. overwrite `sfx/weapons/shotgun.wav`), or
   put it at a new path and update that id in `manifest.json`.
2. Any format your audio backend supports is fine (WAV/OGG/MP3/FLAC for miniaudio); if you
   change the extension, update the path in `manifest.json`.
3. Keep SFX short and mono (positional 3D audio wants mono sources); music can be stereo.

## Regenerating the placeholders

```
python scripts/audio/gen_placeholders.py
```

Deterministic (fixed seed) — it rewrites every `.wav` here and `manifest.json`. Add a new
sound by adding an entry to `SOUND_DEFS` in that script and rerunning; the manifest updates
automatically.

## Where to get real (royalty-free) assets

- **Kenney** — https://kenney.nl/assets (CC0; game SFX packs, impact/UI/weapons)
- **OpenGameArt** — https://opengameart.org (filter by CC0/CC-BY)
- **Freesound** — https://freesound.org (per-sound licences; check each)
- **Sonniss GDC Game Audio Bundle** — https://sonniss.com/gameaudiogdc (huge, royalty-free)
- **Incompetech (Kevin MacLeod)** — https://incompetech.com (CC-BY music, attribution)
- Classic-shooter feel: many CC0 "retro FPS" SFX packs on itch.io.

Whatever you use, record the licence/attribution for each asset (a `CREDITS.md` here is a
good habit) — CC-BY and Freesound clips usually require crediting the author.
