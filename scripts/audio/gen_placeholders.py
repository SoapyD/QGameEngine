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


# ─── tiny synth toolkit ─────────────────────────────────────────────────────
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


def glide(f0, f1, dur, kind="sine", vol=0.5):
    n = int(dur * SR)
    out, phase = [], 0.0
    for i in range(n):
        f = f0 + (f1 - f0) * (i / max(1, n))
        phase += 2 * math.pi * f / SR
        v = math.sin(phase) if kind == "sine" else (1.0 if math.sin(phase) >= 0 else -1.0)
        out.append(v * vol)
    return out


def noise(dur, vol=0.5):
    return [(random.random() * 2 - 1) * vol for _ in range(int(dur * SR))]


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


def mix(*sigs):
    n = max(len(s) for s in sigs)
    out = [0.0] * n
    for s in sigs:
        for i, v in enumerate(s):
            out[i] += v
    return [max(-1.0, min(1.0, x)) for x in out]


def seq(*sigs):
    out = []
    for s in sigs:
        out += s
    return out


def arp(notes, note_dur, reps, kind="sine", vol=0.3):
    out = []
    for _ in range(reps):
        for f in notes:
            out += env(tone(f, note_dur, kind, vol), 0.01, note_dur * 0.4)
    return out


def under(sig, freq, vol=0.12):
    """Mix a sustained bass tone under a signal (for music)."""
    return mix(sig, tone(freq, len(sig) / SR, "sine", vol))


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

    # combat — impacts / explosions
    "combat.bullet_impact":   ("sfx/combat/bullet_impact.wav",    lambda: env(noise(0.06, 0.5), 0.001, 0.05)),
    "combat.flesh_hit":       ("sfx/combat/flesh_hit.wav",        lambda: env(mix(noise(0.08, 0.4), tone(150, 0.08, "sine", 0.3)), 0.001, 0.06)),
    "combat.explosion":       ("sfx/combat/explosion.wav",        lambda: env(mix(noise(0.5, 0.85), glide(120, 40, 0.5, "sine", 0.5)), 0.002, 0.35)),
    "combat.rail_zap":        ("sfx/combat/rail_zap.wav",         lambda: env(glide(2000, 500, 0.2, "sine", 0.4), 0.002, 0.1)),

    # player
    "player.footstep_01":     ("sfx/player/footstep_01.wav",      lambda: env(mix(noise(0.05, 0.28), tone(110, 0.05, "sine", 0.25)), 0.001, 0.04)),
    "player.footstep_02":     ("sfx/player/footstep_02.wav",      lambda: env(mix(noise(0.05, 0.30), tone(95, 0.05, "sine", 0.25)), 0.001, 0.04)),
    "player.footstep_03":     ("sfx/player/footstep_03.wav",      lambda: env(mix(noise(0.05, 0.26), tone(125, 0.05, "sine", 0.25)), 0.001, 0.04)),
    "player.footstep_04":     ("sfx/player/footstep_04.wav",      lambda: env(mix(noise(0.05, 0.32), tone(100, 0.05, "sine", 0.25)), 0.001, 0.04)),
    "player.jump":            ("sfx/player/jump.wav",             lambda: env(glide(300, 600, 0.12, "sine", 0.35), 0.002, 0.05)),
    "player.land":            ("sfx/player/land.wav",             lambda: env(mix(noise(0.08, 0.3), tone(90, 0.10, "sine", 0.4)), 0.001, 0.07)),
    "player.pain":            ("sfx/player/pain.wav",             lambda: env(glide(420, 260, 0.20, "saw", 0.35), 0.003, 0.1)),
    "player.death":           ("sfx/player/death.wav",           lambda: env(glide(360, 80, 0.7, "saw", 0.4), 0.005, 0.3)),
    "player.respawn":         ("sfx/player/respawn.wav",          lambda: seq(env(tone(500, 0.08)), env(tone(700, 0.08)), env(tone(1000, 0.14)))),

    # pickups
    "pickup.health":          ("sfx/pickups/pickup_health.wav",  lambda: seq(env(tone(660, 0.07)), env(tone(880, 0.13)))),
    "pickup.armor":           ("sfx/pickups/pickup_armor.wav",   lambda: seq(env(tone(500, 0.07, "tri")), env(tone(750, 0.13, "tri")))),
    "pickup.ammo":            ("sfx/pickups/pickup_ammo.wav",    lambda: env(tone(900, 0.06, "square", 0.3), 0.002, 0.04)),
    "pickup.weapon":          ("sfx/pickups/pickup_weapon.wav",  lambda: seq(env(tone(523, 0.08)), env(tone(659, 0.08)), env(tone(784, 0.16)))),

    # world / movers / hazards
    "world.door_open":        ("sfx/world/door_open.wav",         lambda: env(glide(120, 190, 0.4, "sine", 0.3), 0.02, 0.15)),
    "world.door_close":       ("sfx/world/door_close.wav",        lambda: env(glide(190, 120, 0.4, "sine", 0.3), 0.02, 0.15)),
    "world.lift_start":       ("sfx/world/lift_start.wav",        lambda: env(tone(100, 0.30, "saw", 0.28), 0.02, 0.1)),
    "world.lift_stop":        ("sfx/world/lift_stop.wav",         lambda: env(tone(140, 0.15, "sine", 0.3), 0.01, 0.1)),
    "world.teleport":         ("sfx/world/teleport.wav",          lambda: seq(env(glide(400, 1200, 0.15, "sine", 0.35)), env(glide(1200, 400, 0.15, "sine", 0.35)))),
    "world.trigger":          ("sfx/world/trigger.wav",           lambda: env(tone(700, 0.05, "sine", 0.25), 0.002, 0.04)),
    "world.lava_loop":        ("sfx/world/lava_loop.wav",         lambda: mix(noise(1.0, 0.25), tone(50, 1.0, "sine", 0.25))),  # loopable

    # ui (for future menus)
    "ui.select":              ("sfx/ui/ui_select.wav",            lambda: env(tone(800, 0.04, "sine", 0.3), 0.002, 0.03)),
    "ui.confirm":             ("sfx/ui/ui_confirm.wav",           lambda: seq(env(tone(800, 0.05)), env(tone(1200, 0.09)))),
    "ui.back":                ("sfx/ui/ui_back.wav",              lambda: seq(env(tone(600, 0.05)), env(tone(400, 0.07)))),

    # ambient beds (loopable)
    "ambient.hum_loop":       ("sfx/ambient/hum_loop.wav",        lambda: tone(60, 2.0, "sine", 0.15)),
    "ambient.wind_loop":      ("sfx/ambient/wind_loop.wav",       lambda: noise(2.0, 0.10)),

    # music (loopable placeholder arpeggios)
    "music.exploration":      ("music/exploration_loop.wav",      lambda: under(arp([A, C, E, Amid, E, C], 0.35, 4, "sine", 0.25), 110, 0.10)),
    "music.action":           ("music/action_loop.wav",           lambda: under(arp([A, E, Amid, 523.25], 0.15, 10, "square", 0.18), 110, 0.10)),
    "music.menu":             ("music/menu_loop.wav",             lambda: under(arp([C, E, 392.0, 523.25], 0.30, 5, "tri", 0.22), 130.81, 0.10)),
}


def write_wav(path, samples):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with wave.open(path, "w") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        frames = b"".join(struct.pack("<h", int(max(-1.0, min(1.0, s)) * 32767)) for s in samples)
        w.writeframes(frames)


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


if __name__ == "__main__":
    main()
