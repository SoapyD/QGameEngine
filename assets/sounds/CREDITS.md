# Audio Credits & Licences

Track the source and licence of every audio asset here as you add real files.

## ⚠️ Placeholder status

The bulk of the current audio is imported from **OpenArena** as **placeholders**, plus a few
synthesised tones. None of this is original to QEngine. Replace before any release you care
about the licence of.

## OpenArena (imported via `scripts/audio/import_openarena.py`)

- **Source:** OpenArena 0.8.8 — https://openarena.ws
- **Licence:** OpenArena game data is **GPL v2** (with some assets under other free licences;
  see the `credits`/`CREDITS` files inside the pk3s, e.g. `sound/world/CREDITS`).
- **Implication:** GPLv2 is copyleft. Shipping these assets means honouring GPLv2 for them
  (source availability, licence notices). **Fine for a GPL/hobby build; a blocker if you ever
  want QEngine's assets to be proprietary.** Swap these out before a closed-source release.
- **Covers (77 slots):** all in-game weapon fires + dry-fire/switch; extra weapon fires banked
  for future guns (machinegun, plasma, chaingun, BFG, gauntlet) + lightning/railgun idle hums;
  projectile fly loops + bounce; combat impacts/explosions (incl. plasma + small); footsteps,
  jump/land/falling/gasp and pain tiers (25/50/75/100) + death/respawn; health tiers
  (small/normal/large/mega), armour/ammo/weapon pickups; powerups (quad/regen/haste/battlesuit/
  invisibility/flight + respawns); doors/lifts/teleport (in+out)/trigger/jumppad/lava; UI menu
  sounds; ambience beds (hum/wind/fire/waterfall/machine/drone/electric); and five music tracks
  (`OA01/02/06/07/13`).
- Player voice (jump/land/pain/death/gasp) is the **`sarge`** character set.

## Reused slot

- `weapon.supershotgun` — no super-shotgun exists in OpenArena, so this slot currently reuses
  the OpenArena shotgun sample (a copy of `weapon.shotgun`). Swap for a real double-barrel later.

## Synthesised placeholders (`scripts/audio/gen_placeholders.py`)

- None currently — every slot is filled from OpenArena. The synth generator remains as a
  gap-filler for any future slot added without a real asset.

## Replacing an asset

When you drop in a real file, add a row here:

| Manifest id | File | Source | Licence | Attribution required? |
|-------------|------|--------|---------|-----------------------|
| _example.id_ | sfx/…/x.wav | Kenney "Impact Sounds" | CC0 | No |

CC-BY and most Freesound assets require crediting the author — record it above.
