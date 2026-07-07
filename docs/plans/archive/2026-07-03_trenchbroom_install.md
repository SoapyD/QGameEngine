# Plan — TrenchBroom: Install & Configure

**Group:** `trenchbroom` (part 1 of 2 — pairs with
[2026-07-03_trenchbroom_engine-loader.md](../2026-07-03_trenchbroom_engine-loader.md)).
**Graduated from** [2026-06-08_trenchbroom-setup.md](2026-06-08_trenchbroom-setup.md) Part 1.

**Goal:** get TrenchBroom installed and pointed at QEngine's assets so you can *draw and save a
`.map`* — **no engine code required**. This can be done today and produces a real sample `.map`
to develop the loader against.

**Status (2026-07-07): COMPLETE.** Install + registration done — TrenchBroom v2026.1 installed,
`tb/` config (`GameConfig.cfg` + `QEngine.fgd` + `Icon.png`) copied into
`%APPDATA%\TrenchBroom\games\QEngine\`, Game Path set to the repo root in `Preferences.json`.
Two maps authored and saved: [`assets/maps/smoke.map`](../../../assets/maps/smoke.map) (the
loader-unblocking smoke test, loads/plays) and the full
[`assets/maps/showcase.map`](../../../assets/maps/showcase.map) — a complete rebuild of the
hardcoded showcase per the [maps README](../../../assets/maps/README.md) table (room shell, player
start, sun + white/RGB torch lights, all weapons/items, 2 grunts, door+trigger, lift+trigger,
teleporter+destination, trigger_hurt, prop_dynamic), reviewed clean 2026-07-07. The "Done when"
is met. Both deliverables shipped; plan archived.

Follow-on (not this plan's scope): fill in `GameEngineProfiles.cfg` to launch QEngine from TB's
Run button; general angled-brush geometry — tracked in the engine-loader plan's deferred work.

---

## 1. Install
- Download from **https://trenchbroom.github.io/** (Win/Mac/Linux, 2.x series), install, launch.

## 2. Repo integration folder
Add a `tb/` directory + an `assets/maps/` source folder:
```
QEngine/
├── tb/
│   ├── GameConfig.cfg     ← tells TrenchBroom about QEngine
│   ├── QEngine.fgd        ← entity definitions (what you can place)
│   └── Icon.png           ← optional toolbar icon
└── assets/
    ├── textures/          ← already exists (grid_*.png, wall.png)
    └── maps/              ← NEW — .map sources live here
        └── showcase.map
```

## 3. `GameConfig.cfg`
Standard-format config; `filesystem.searchpath = "assets"`, a **`materials`** block with
`root = "textures"` + `format {extensions:[".png"], format:"image"}`,
`entities.definitions = ["QEngine.fgd"]`, plus a `trigger_*` transparent brush tag.

> **v2026.1 note (corrected 2026-07-04):** this build uses config `version: 9` with a
> **`materials`** block — NOT the older `textures`/`attribute` block the roadmap §Phase 4
> template shows. The shipped [`tb/GameConfig.cfg`](../../../tb/GameConfig.cfg) already uses the
> correct v2026.1 schema (verified against the install's own `games/Generic/GameConfig.cfg`).

## 4. `QEngine.fgd` skeleton
Define at least the archetypes the showcase uses so they can be placed:
`worldspawn`, `info_player_start`, `light` / `light_environment`, `func_static`,
`func_door`, `func_plat`, `trigger_multiple`, `trigger_teleport` +
`info_teleport_destination`, `trigger_hurt`, `prop_dynamic` (demo cube), and the already-built
`item_*` / `weapon_*` / `monster_grunt` classnames. (Full FGD property tables: archived original §3.1.)

## 5. Install the config
Copy `GameConfig.cfg` + `QEngine.fgd` + `Icon.png` into a folder TrenchBroom scans:
- **Reliable (v2026.1):** the install's own games dir —
  `TrenchBroom-Win64-AMD64-v2026.1-Release\games\QEngine\`.
- **Survives reinstalls:** `%APPDATA%\TrenchBroom\games\QEngine\` (Win) /
  `~/.TrenchBroom/games/QEngine/` (Linux/Mac).
- Then Preferences → **Games → QEngine → Game Path** = repo root, so `assets/textures`
  resolves. Restart → **New Map → QEngine** lists our textures + FGD entities.

**Full click-by-click walkthrough for v2026.1:**
[`../roadmap/TRENCHBROOM_WALKTHROUGH.md`](../../roadmap/TRENCHBROOM_WALKTHROUGH.md).

## Done when
You can draw a hollow room, texture it, place a few entities, and **save `assets/maps/showcase.map`**.
The engine can't load it yet — that's the engine-loader plan. This sample `.map` unblocks it.

**What to build & the settings that matter:** see
[`../roadmap/TRENCHBROOM_TEST_MAP.md`](../../roadmap/TRENCHBROOM_TEST_MAP.md) — the minimal
smoke-test map, what each element exercises in the loader, and the format/texture/scale
gotchas that break loading silently.
