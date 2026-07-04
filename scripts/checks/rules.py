"""Configuration for the QEngine convention checks.

Single edit point for size caps, skip globs, and allowlists. Mirrors the intent
of wyrdwars/scripts/checks/file-size-rules.ts, adapted to C++ and this repo.
See docs/architecture/CODING_STANDARD.md for the rules these encode.
"""

# (glob relative to repo root, line cap, area label). First match wins; order
# most-specific first. Caps are smell thresholds (see CODING_STANDARD §4).
# Caps recalibrated after the first full report (Plan 01 §4 said "tune during 02"):
# a cohesive single-responsibility C++ system runs ~150-190 lines, longer than the
# TS equivalent. These flag genuine monsters (combat was 524, debug_hud 433) while
# leaving cohesive units alone.
SIZE_RULES = [
    ("src/engine/ecs/systems/**/*.cpp", 190, "system free-fn"),
    ("**/factories*.cpp", 175, "factory"),   # cohesive spawn module — accretes one fn per entity type
    # The classname→factory dispatch is a table of thin (~3-line) adapters over the
    # factories; cohesive, not logic-heavy. Capped like the factory module, not the
    # tight generic-level cap below.
    ("**/classname_factory*.cpp", 200, "classname dispatch (adapter table)"),
    ("src/engine/level/**/*.cpp", 100, "level free-fn"),
    ("src/engine/core/**/*.cpp", 200, "core class"),
    ("src/engine/renderer/**/*.cpp", 200, "renderer class"),
    ("src/engine/physics/**/*.cpp", 200, "physics"),
    ("src/engine/audio/**/*.cpp", 200, "audio"),   # subsystem class impl (miniaudio wrapper)
    ("src/engine/app/**/*.cpp", 150, "app"),
    ("src/harness/**/*.cpp", 750, "test harness"),  # scenario-heavy, lenient; grows per scenario
    ("src/**/*.cpp", 150, "cpp (default)"),
    ("src/**/*.h", 100, "header"),
]

# Files/dirs never checked by any rule (data, third-party, generated, dead code).
SKIP_GLOBS = [
    "src/engine/ecs/components/**",
    "src/engine/ecs/components.h",
    "**/types/**",
    "**/archived/**",
    # in-code level data: a hand-written SpawnParams list standing in for a .map
    # file. Length is data (push_back rows), not logic — skip like components.
    "src/engine/level/showcase_descriptor*.cpp",
    # banner-dead units (not compiled) — don't lint dead code
    "**/collision.h", "**/collision.cpp",
    "**/spatial_hash.h", "**/spatial_hash.cpp",
    "**/level_loader.h", "**/level_loader.cpp",  # dead .qlvl parser (removed from build)
    "src/engine/physics/jolt_setup.h",   # Jolt layer/filter boilerplate (third-party-shaped)
    "extern/**",
    "build/**",
]

# Type definitions (struct/enum/using) are allowed in these locations even though
# they're outside a types/ folder. ECS components are first-class (CODING_STANDARD
# §2); level.h is a cohesive types header pending the Plan 03 move decision.
TYPE_LOCATION_ALLOWLIST = [
    "src/engine/ecs/components/**",
    "src/engine/ecs/components.h",
    "**/types/**",
    "src/engine/level/level.h",          # cohesive data header; Plan 03 decides move-vs-keep
    "src/engine/physics/jolt_setup.h",   # Jolt layer/filter glue, tightly Jolt-coupled
    "src/engine/physics/jolt_world.h",   # JoltWorld struct = the class for that unit
    "src/engine/physics/physics_config.h",
    "src/engine/audio/**",               # AudioEngine uses pimpl — the private Impl struct
                                         # lives out-of-line in the .cpp by design
    "src/engine/app/simulation.h",       # forward-decls only
    "src/harness/**",                    # test scaffolding (wyrdwars skips tests too)
    "extern/**",
]

# Umbrella barrels that headers must not pull in (CODING_STANDARD §3). A .h that
# includes one of these should use a specific leaf or a forward declaration.
UMBRELLA_BARRELS = [
    "engine/ecs/components.h",
]

# Banner-labelled dead headers — no live file should include them.
DEAD_HEADERS = [
    "engine/physics/collision.h",
    "engine/physics/spatial_hash.h",
]
