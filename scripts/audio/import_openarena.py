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

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
SOUNDS = os.path.join(ROOT, "assets", "sounds")

# manifest id -> path inside the pk3s. Dest path comes from manifest.json (SFX
# keep their .wav slot; only the file contents change).
SFX_MAP = {
    "weapon.shotgun":        "sound/weapons/shotgun/sshotf1b.wav",
    "weapon.supershotgun":   "sound/weapons/shotgun/sshotf1b.wav",  # reuse shotgun for now (OA has none)
    "weapon.nailgun":        "sound/weapons/nailgun/wnalfire.wav",
    "weapon.rocketlauncher": "sound/weapons/rocket/rocklf1a.wav",
    "weapon.grenadelauncher":"sound/weapons/grenade/grenlf1a.wav",
    "weapon.lightninggun":   "sound/weapons/lightning/lg_fire.wav",
    "weapon.railgun":        "sound/weapons/railgun/railgf1a.wav",
    "weapon.dryfire":        "sound/weapons/noammo.wav",
    "weapon.switch":         "sound/weapons/change.wav",

    "combat.bullet_impact":  "sound/weapons/machinegun/ric1.wav",
    "combat.flesh_hit":      "sound/feedback/hit.wav",
    "combat.explosion":      "sound/weapons/rocket/rocklx1a.wav",
    "combat.rail_zap":       "sound/weapons/lightning/lg_hit.wav",

    "player.footstep_01":    "sound/player/footsteps/step1.wav",
    "player.footstep_02":    "sound/player/footsteps/step2.wav",
    "player.footstep_03":    "sound/player/footsteps/step3.wav",
    "player.footstep_04":    "sound/player/footsteps/step4.wav",
    "player.jump":           "sound/player/sarge/jump1.wav",
    "player.land":           "sound/player/sarge/fall1.wav",
    "player.pain":           "sound/player/sarge/pain50_1.wav",
    "player.death":          "sound/player/sarge/death1.wav",
    "player.respawn":        "sound/items/respawn1.wav",

    "pickup.health":         "sound/items/n_health.wav",
    "pickup.armor":          "sound/misc/ar1_pkup.wav",
    "pickup.ammo":           "sound/misc/am_pkup.wav",
    "pickup.weapon":         "sound/misc/w_pkup.wav",

    "world.door_open":       "sound/movers/doors/dr1_strt.wav",
    "world.door_close":      "sound/movers/doors/dr1_end.wav",
    "world.lift_start":      "sound/movers/plats/pt1_strt.wav",
    "world.lift_stop":       "sound/movers/plats/pt1_end.wav",
    "world.teleport":        "sound/world/telein.wav",
    "world.trigger":         "sound/movers/switches/butn2.wav",
    "world.lava_loop":       "sound/world/lava_amb_01_quiet.wav",

    "ui.select":             "sound/misc/menu1.wav",
    "ui.confirm":            "sound/misc/menu2.wav",
    "ui.back":               "sound/misc/menu3.wav",

    "ambient.hum_loop":      "sound/world/neonhum.wav",
    "ambient.wind_loop":     "sound/world/wind1.wav",
}

# Extra slots (not in the base 41) banked for future features: more weapons,
# powerups, projectile/hum loops, ambiences, pain/health variants. id ->
# (pk3 source, dest relative path). New manifest ids are created for these.
EXTRA_SFX_MAP = {
    # future weapons
    "weapon.machinegun":       ("sound/weapons/machinegun/machgf1b.wav", "sfx/weapons/machinegun.wav"),
    "weapon.plasma":           ("sound/weapons/plasma/hyprbf1a.wav",     "sfx/weapons/plasma.wav"),
    "weapon.chaingun":         ("sound/weapons/vulcan/vulcanf1b.wav",    "sfx/weapons/chaingun.wav"),
    "weapon.bfg":              ("sound/weapons/bfg/bfg_fire.wav",        "sfx/weapons/bfg.wav"),
    "weapon.gauntlet":         ("sound/weapons/melee/fstatck.wav",       "sfx/weapons/gauntlet.wav"),
    # weapon idle hums (loop while held/charging)
    "weapon.lightning_hum":    ("sound/weapons/lightning/lg_hum.wav",    "sfx/weapons/lightning_hum.wav"),
    "weapon.railgun_hum":      ("sound/weapons/railgun/rg_hum.wav",      "sfx/weapons/railgun_hum.wav"),
    # projectile fly loops / bounces
    "projectile.rocket_fly":    ("sound/weapons/rocket/rockfly.wav",     "sfx/projectile/rocket_fly.wav"),
    "projectile.plasma_fly":    ("sound/weapons/plasma/lasfly.wav",      "sfx/projectile/plasma_fly.wav"),
    "projectile.grenade_bounce":("sound/weapons/grenade/hgrenb1a.wav",   "sfx/projectile/grenade_bounce.wav"),
    # extra combat impacts
    "combat.plasma_impact":    ("sound/weapons/plasma/plasmx1a.wav",     "sfx/combat/plasma_impact.wav"),
    "combat.explosion_small":  ("sound/weapons/proxmine/wstbexpl.wav",   "sfx/combat/explosion_small.wav"),
    "combat.armor_hit":        ("sound/feedback/hithi.wav",              "sfx/combat/armor_hit.wav"),
    # player pain variants + air
    "player.pain_25":          ("sound/player/sarge/pain25_1.wav",       "sfx/player/pain_25.wav"),
    "player.pain_75":          ("sound/player/sarge/pain75_1.wav",       "sfx/player/pain_75.wav"),
    "player.pain_100":         ("sound/player/sarge/pain100_1.wav",      "sfx/player/pain_100.wav"),
    "player.gasp":             ("sound/player/sarge/gasp.wav",           "sfx/player/gasp.wav"),
    "player.falling":          ("sound/player/sarge/falling1.wav",       "sfx/player/falling.wav"),
    # health tiers
    "pickup.mega_health":      ("sound/items/m_health.wav",             "sfx/pickups/mega_health.wav"),
    "pickup.large_health":     ("sound/items/l_health.wav",             "sfx/pickups/large_health.wav"),
    "pickup.small_health":     ("sound/items/s_health.wav",             "sfx/pickups/small_health.wav"),
    # powerups
    "powerup.quad":            ("sound/items/quaddamage.wav",           "sfx/powerups/quad.wav"),
    "powerup.regen":           ("sound/items/regen.wav",                "sfx/powerups/regen.wav"),
    "powerup.haste":           ("sound/items/haste.wav",                "sfx/powerups/haste.wav"),
    "powerup.battlesuit":      ("sound/items/protect.wav",              "sfx/powerups/battlesuit.wav"),
    "powerup.invisibility":    ("sound/items/invisibility.wav",         "sfx/powerups/invisibility.wav"),
    "powerup.flight":          ("sound/items/flight.wav",               "sfx/powerups/flight.wav"),
    "powerup.respawn":         ("sound/items/poweruprespawn.wav",       "sfx/powerups/respawn.wav"),
    # world / hazards
    "world.jumppad":           ("sound/world/jumppad.wav",              "sfx/world/jumppad.wav"),
    "world.teleport_out":      ("sound/world/teleout.wav",              "sfx/world/teleport_out.wav"),
    # extra ambience beds (loopable)
    "ambient.fire_loop":       ("sound/world/fire1.wav",                "sfx/ambient/fire_loop.wav"),
    "ambient.waterfall_loop":  ("sound/world/waterfall.wav",            "sfx/ambient/waterfall_loop.wav"),
    "ambient.machine_loop":    ("sound/world/machinerydrone01.wav",     "sfx/ambient/machine_loop.wav"),
    "ambient.drone_loop":      ("sound/world/drone6.wav",               "sfx/ambient/drone_loop.wav"),
    "ambient.electric_loop":   ("sound/world/electro.wav",              "sfx/ambient/electric_loop.wav"),
}

# music id -> (pk3 path, dest relative path). OA music is .ogg, so the slot
# extension changes and the manifest is updated; the old .wav is removed.
MUSIC_MAP = {
    "music.exploration": ("music/OA01.ogg", "music/exploration_loop.ogg"),
    "music.action":      ("music/OA06.ogg", "music/action_loop.ogg"),
    "music.menu":        ("music/OA02.ogg", "music/menu_loop.ogg"),
    "music.ambient":     ("music/OA07.ogg", "music/ambient_loop.ogg"),
    "music.intense":     ("music/OA13.ogg", "music/intense_loop.ogg"),
}


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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", required=True, help="path to a baseoa/ folder of .pk3 files")
    args = ap.parse_args()

    index = build_index(args.src)
    manifest_path = os.path.join(SOUNDS, "manifest.json")
    with open(manifest_path, encoding="utf-8") as f:
        manifest = json.load(f)
    sounds = manifest["sounds"]

    imported, missing = [], []

    # SFX: overwrite the existing slot file, keep its manifest path.
    for mid, member in SFX_MAP.items():
        dest_rel = sounds.get(mid)
        if not dest_rel:
            missing.append((mid, "no manifest slot"))
            continue
        dest_abs = os.path.join(SOUNDS, dest_rel.replace("/", os.sep))
        if extract(index, member, dest_abs):
            imported.append((mid, member, dest_rel))
        else:
            missing.append((mid, member + " (not in pk3s)"))

    # Extra SFX: new slots (not in the base manifest) — extract + create the slot.
    for mid, (member, dest_rel) in EXTRA_SFX_MAP.items():
        dest_abs = os.path.join(SOUNDS, dest_rel.replace("/", os.sep))
        if extract(index, member, dest_abs):
            sounds[mid] = dest_rel
            imported.append((mid, member, dest_rel))
        else:
            missing.append((mid, member + " (not in pk3s)"))

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

    with open(manifest_path, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)

    for mid, member, dest in sorted(imported):
        print(f"  [ok]   {mid:26s} <- {member}")
    for mid, why in sorted(missing):
        print(f"  [skip] {mid:26s} ({why})")

    still_synth = sorted(set(sounds) - {m[0] for m in imported})
    print(f"\n  imported {len(imported)} from OpenArena; "
          f"{len(still_synth)} slot(s) still synth placeholder: {', '.join(still_synth) or 'none'}")


if __name__ == "__main__":
    main()
