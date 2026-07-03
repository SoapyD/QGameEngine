# Plan — TrenchBroom: Install & Configure

**Group:** `trenchbroom` (part 1 of 2 — pairs with
[2026-07-03_trenchbroom_engine-loader.md](2026-07-03_trenchbroom_engine-loader.md)).
**Graduated from** [archive/2026-06-08_trenchbroom-setup.md](archive/2026-06-08_trenchbroom-setup.md) Part 1.

**Goal:** get TrenchBroom installed and pointed at QEngine's assets so you can *draw and save a
`.map`* — **no engine code required**. This can be done today and produces a real sample `.map`
to develop the loader against.

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
Standard-format config; `filesystem.searchpath = "assets"`, `textures.root = "textures"`,
`.png` extension, `entities.definitions = ["QEngine.fgd"]`, plus a `trigger_*` transparent
brush tag. Full template in the archived original and
[`roadmap/ROADMAP_TRENCHBROOM.md`](../../docs/roadmap/ROADMAP_TRENCHBROOM.md) §Phase 4.

## 4. `QEngine.fgd` skeleton
Define at least the archetypes the showcase uses so they can be placed:
`worldspawn`, `info_player_start`, `light` / `light_environment`, `func_static`,
`func_door`, `func_plat`, `trigger_multiple`, `trigger_teleport` +
`info_teleport_destination`, `trigger_hurt`, `prop_dynamic` (demo cube), and the already-built
`item_*` / `weapon_*` / `monster_grunt` classnames. (Full FGD property tables: archived original §3.1.)

## 5. Install the config
- **Windows:** copy `tb/` into `%APPDATA%\TrenchBroom\games\QEngine\` (or set the game path to the
  repo root in Preferences → Game).
- **Linux/Mac:** `~/.TrenchBroom/games/QEngine/`.
- Restart → **New Map → QEngine** should list our textures + FGD entities.

## Done when
You can draw a hollow room, texture it, place a few entities, and **save `assets/maps/showcase.map`**.
The engine can't load it yet — that's the engine-loader plan. This sample `.map` unblocks it.
