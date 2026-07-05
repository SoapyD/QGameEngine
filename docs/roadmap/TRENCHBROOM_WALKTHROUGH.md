# TrenchBroom v2026.1 — Step-by-Step: Build & Save a QEngine Test Map

A click-by-click walkthrough for **TrenchBroom-Win64-AMD64-v2026.1-Release**. Follow
it top to bottom and you'll end with `assets/maps/smoke.map` — a hollow room, a player
start, and a light — which is enough to drive the whole `.map` loader as it comes online.

Menu paths and terms here were checked against the manual shipped inside your install
(`TrenchBroom-Win64-AMD64-v2026.1-Release/manual/`). If a label differs slightly, the
menu it lives under is still correct.

> **What each element is for** (which loader step it exercises) lives in the companion
> spec, [`TRENCHBROOM_TEST_MAP.md`](TRENCHBROOM_TEST_MAP.md). This doc is the *how*.

---

## 0. Install the QEngine game profile (once)

TrenchBroom discovers "games" by scanning its `games/` folders. Copy our profile
([`tb/`](../../tb/)) in as a game called **QEngine**.

**Do this (Git Bash or drag-and-drop in Explorer):**

```bash
# from anywhere in a bash shell:
mkdir -p "/d/Downloads/TrenchBroom-Win64-AMD64-v2026.1-Release/games/QEngine"
cp "/d/Documents/Programming/C_Projects/QEngine/tb/GameConfig.cfg" \
   "/d/Documents/Programming/C_Projects/QEngine/tb/QEngine.fgd" \
   "/d/Documents/Programming/C_Projects/QEngine/tb/Icon.png" \
   "/d/Downloads/TrenchBroom-Win64-AMD64-v2026.1-Release/games/QEngine/"
```

Result — the folder should contain exactly:
```
TrenchBroom-Win64-AMD64-v2026.1-Release/games/QEngine/
├── GameConfig.cfg
├── QEngine.fgd
└── Icon.png
```

> **Note:** this lives inside the extracted TrenchBroom folder. If you later move or
> re-extract TrenchBroom, re-copy it. (Alternative permanent location that survives
> reinstalls: `%APPDATA%\TrenchBroom\games\QEngine\` — same three files.)

---

## 1. Launch and point QEngine at the repo

1. Run `TrenchBroom.exe`.
2. On the **Welcome** window, click **Preferences…** (or, once a map is open,
   menu **File → Preferences**, hotkey typically `Ctrl+,`).
3. Select the **Games** pane on the left.
4. Find **QEngine** in the games list (it appears because of step 0). Click it.
5. On the right, next to **Game Path**, click the **…** button and browse to the repo
   root: `D:\Documents\Programming\C_Projects\QEngine`. Select the folder.
   - This is what makes `filesystem.searchpath: "assets"` → `QEngine/assets` and
     `materials.root: "textures"` → `QEngine/assets/textures` resolve, so our
     `grid_*` textures show up.
6. Close Preferences.

**Checkpoint:** if QEngine is missing from the games list, jump to
[Troubleshooting](#troubleshooting) → "QEngine not listed".

---

## 2. Create a new map (QEngine + Standard format)

1. **File → New** (or **New Map** on the Welcome window).
2. A dialog asks for the **game** and **map format**:
   - **Game:** QEngine
   - **Map format:** **Standard** ← this matters. Our parser reads Standard, not
     Valve. (QEngine's config only offers Standard, so it should be preselected.)
3. Click **OK**. You get the editor: a large 3D viewport, and 2D ortho views.

**Camera controls you'll need:**
| Action | Input |
|--------|-------|
| Look around (3D) | Hold **right mouse button** + move mouse |
| Fly | **W A S D** while holding right mouse |
| Zoom | Mouse **scroll wheel** |
| Pan 2D view | **Middle-drag**, or scroll |
| Deselect all | Click empty space, or **Esc** |

---

## 3. Set the grid size (controls wall thickness)

CSG Hollow (next step) makes walls **as thick as the current grid**. Pick a sensible
size first.

- Grid controls are on the toolbar (a grid icon with **+ / −**) and under the
  **View** menu (**Grid → Set Grid Size**, or the `[` / `]` keys to shrink/grow).
- Set grid to **16** or **32**. (Recall the loader's planned ratio is 1 engine unit =
  32 map units, so a 32-unit wall ≈ 1 engine unit thick.)

---

## 4. Draw the room's outer brush

1. Make sure nothing is selected (**Esc**).
2. In the **3D viewport**, click-drag on the "floor" grid to rubber-band a rectangle,
   then release — TrenchBroom creates a box brush. (You can also drag a rectangle in a
   **2D view** to set footprint, then drag its top handle up to set height.)
3. Size it roughly **512 × 512 × 256** map units (≈ 16 × 16 × 8 engine units). Exact
   size doesn't matter for a smoke test — big enough to stand in.
   - Read/adjust dimensions via the drag handles; the status bar shows the current size.

You now have one **solid** block. The room is the hollow *inside* it (Quake mapping is
subtractive).

---

## 5. Hollow it into 6 wall brushes

1. Select the box (left-click it; it highlights).
2. Menu **Edit → CSG → Hollow**.
3. The single block becomes **6 thin brushes** (floor, ceiling, 4 walls) with an empty
   interior. Wall thickness = the grid size you set in step 3.

**Checkpoint:** in the 3D view, fly *inside* — you should be in an enclosed room.

---

## 6. Texture the faces

Untextured faces use a placeholder. Texture at least the floor so you can tell up from
down (and to prove texture resolution works).

1. Open the **Face** inspector (right-hand panel; tabs are usually **Map / Entity /
   Face** — click **Face**). It shows a **Material Browser**.
2. If the browser is empty: your Game Path (step 1.5) isn't set to the repo root, or the
   textures didn't resolve — see Troubleshooting → "No textures".
3. **Select faces to paint:** in the 3D view, click a **single face** to select just
   that face; **Shift-click** more faces to add. (Selecting the whole brush paints all
   its faces.)
4. In the Material Browser, **click a material** (e.g. `grid_grey`) — it applies to the
   selected faces immediately.
5. Texture the floor with `grid_grey`; do the walls however you like. Available:
   `grid_grey`, `grid_blue`, `grid_green`, `grid_orange`, `grid_red`, `wall`.

---

## 7. Place the player start (mandatory)

Without `info_player_start` the loaded map has nowhere to spawn you.

1. **Esc** to deselect.
2. **Right-click** in the 3D viewport at a spot on the floor inside the room.
3. Choose **Create Point Entity** → in the submenu/list pick **info_player_start**.
   (These entity types come from our `QEngine.fgd`.)
4. It drops a marker box. Make sure it sits **above the floor**, not buried in it — drag
   it up a little if needed.
5. (Optional) With it selected, open the **Entity** inspector and set **angle** (facing
   direction, degrees) — click the property, type a value like `90`.

---

## 8. Place a light and set its properties

1. **Esc**. **Right-click** near the ceiling → **Create Point Entity** → **light**.
2. Select it, open the **Entity** inspector (the key/value property table).
3. The FGD pre-lists the light's keys with defaults. To edit or add one:
   - Click a row to edit its value, **or** click the **+** (add property) button, type
     the **key** then the **value**.
   - Useful keys (all optional — defaults exist): `_color` = `1 1 1`,
     `linear` = `0.09`, `quadratic` = `0.032`.

> Keep to the keys the FGD lists — the loader only reads those; unknown keys are ignored.

---

## 9. Save as `smoke.map`

1. **File → Save As…**
2. Navigate to **`D:\Documents\Programming\C_Projects\QEngine\assets\maps\`**.
3. Filename: **`smoke.map`**. Save.

That's the smoke test done. Drop it in `assets/maps/` (this folder already exists with a
README).

---

## 10. Verify it saved as Standard format (important)

Open `assets\maps\smoke.map` in any text editor and look at a **face line** inside the
`worldspawn` block. It must look like:

```
( -256 -256 -64 ) ( -256 -255 -64 ) ( -256 -256 -63 ) grid_grey 0 0 0 1 1
```

- **Exactly 5 numbers after the texture name** (`0 0 0 1 1`) = **Standard** �y. Good.
- If you instead see brackets — `... grid_grey [ 1 0 0 0 ] [ 0 -1 0 0 ] 0 1 1` — that's
  **Valve** format and our parser will reject it. Redo step 2 with **Standard**.

You can sanity-check the parser accepts real editor output later, once the loader's file
path is wired (step 2.5); for now the parser is covered by the `map_parse` harness test.

---

## 11. (Tier 2, optional) A door + trigger — the linking test

Do this in a **second** map, `showcase.map`, once the smoke map loads cleanly. It
exercises brush entities and the two-pass `target`/`targetname` linking.

1. **Draw a thin brush** where a door should be (e.g. 32 units thick, filling a doorway).
2. Select it → **right-click → Create Brush Entity → func_door**.
3. In the **Entity** inspector set:
   - `targetname` = `door1`
   - `endpos` = the absolute open position, e.g. `25 4.5 15` *(engine-space target the
     factory reads directly)*
   - `speed` = `3`, `wait` = `4`
4. **Draw another brush** in front of the door (the approach zone).
5. Select it → **Create Brush Entity → trigger_multiple**.
6. Set its **`target`** = **`door1`** (must match the door's `targetname`).
7. Save. When the loader lands (steps 2.3–2.4), stepping into the trigger opens the door.

Repeat the pattern for `func_plat` (lift), `trigger_teleport` +
`info_teleport_destination`, and `trigger_hurt` — full mapping table in
[`TRENCHBROOM_TEST_MAP.md`](TRENCHBROOM_TEST_MAP.md) and
[`../../assets/maps/README.md`](../../assets/maps/README.md).

---

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| **QEngine not listed** in the New-map / Games dialog | The profile isn't where TrenchBroom scans. Recheck step 0: three files in `…/games/QEngine/`. Restart TrenchBroom (it scans on launch). Confirm `GameConfig.cfg` is valid (it's plain JSON). |
| **No textures / Material Browser empty** | Game Path not set to the repo root (step 1.5), so `assets/textures` doesn't resolve. Set it and reopen the Face inspector. Confirm the PNGs exist in `QEngine\assets\textures\`. |
| **Only "Valve"/"Quake2" formats offered, or map saved with `[ ]` brackets** | You picked the wrong format. New map → choose **Standard** (step 2). Our config only lists Standard, so if others appear you may have selected a different game. |
| **Entities have no QEngine types** (only generic) | The FGD didn't load — check `QEngine.fgd` is in the profile folder and `GameConfig.cfg` lists `"definitions": [ "QEngine.fgd" ]`. |
| **Room is inside-out / can't see walls** | You're looking at brush backfaces from outside. Fly *inside* the hollowed room; backface culling hides far walls — normal. |
| **CSG → Hollow greyed out** | You must have a single solid brush selected first (step 4), not an entity or nothing. |

---

## After you have a map

- Put `smoke.map` (and later `showcase.map`) in `assets/maps/`.
- Ping me: I'll continue the loader (step 2.2 brush→mesh onward) and, at step 2.5, add a
  headless scenario that loads your `smoke.map` and asserts the player spawns on the
  floor — the regression guard before the hard-coded showcase is retired.
