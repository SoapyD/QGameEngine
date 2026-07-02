# Chapter 17a: Coding Standard & Convention Checks

## What You'll Learn
- Why you would restructure a working engine, and what "two god-files" cost you
- Writing the house style down *first* as a reviewable spec (`CODING_STANDARD.md`)
- The six rules of the standard, and how each one adapts a WyrdWars convention to C++
- Building the checker config (`scripts/checks/rules.py`) — the single edit point for caps, skip globs, and allowlists
- Building the runner (`scripts/checks/run_all.py`) — five heuristic line-scanners
- The difference between `--report` (always exit 0) and `--strict` (exit 1 on any finding)
- Running the checks report-only to produce the baseline survey

---

## Step 1: Why Restructure a Working Engine?

The engine works. The headless suite passes. So why touch the file layout at all?

### Why bother — the cost of god-files

By the end of Chapter 16 two files had quietly grown into *god-files*:

- `combat_system.cpp` — **524 lines**. Hitscan, projectiles, splash damage, knockback, weapon switching — all in one translation unit.
- `debug_hud_system.cpp` — **433 lines**. Text, crosshair, bars, ammo, the damage flash overlay — one function doing everything the HUD draws.

A 500-line file is not *broken*, but it taxes you in three concrete ways:

1. **Traceability.** When a bug report says "rockets don't apply splash to the lift," you want to open *one small file* whose name tells you it owns splash. In a 524-line file you scroll and grep instead.
2. **Testability.** A file that does one thing has one reason to change and a small, obvious surface to test. A file that does eight things has eight.
3. **Compile cost.** Every edit to `combat_system.cpp` recompiles the *whole* 524-line TU. Worse, if its header pulls in an umbrella barrel, every consumer of that header rebuilds too. Small files with strict includes keep incremental rebuilds local.

The goal we are adopting is one line: **small, single-responsibility, traceable, testable files.** It comes from the WyrdWars TypeScript conventions, *adapted* to C++'s header/`.cpp` model — this is not mechanical mimicry. Some WyrdWars rules transfer 1:1 (one thing per file). Some need adapting (size caps — C++ headers are verbose). And one — splitting a class into per-method files — is explicitly **rejected** in favour of a cheaper C++-native move (split the `.cpp` by concern, keep the single header).

### Why write it down before moving anything

The temptation is to dive straight in and start splitting `combat_system.cpp`. Resist it. The restructure spans six plans and touches dozens of files; if the rules live only in your head they drift, and reviewers cannot push back on a spec they have not seen.

So the sequence — captured in the [conventions plan bundle](../../plans/archive/conventions/README.md) — is deliberate:

```
01 coding-standard      → write the rules down (reviewable spec)
02 convention-checks    → C++-aware checks that enforce 01 (report-only first)
03 types-folders        → cross-cutting: establish types/ + move incidental types
04 ecs-restructure      → systems split, factories, relocate misplaced files
05 engine-classes       → renderer/core classes: split long .cpp, header discipline
06 physics-level        → physics free fns, level, retire dead code
07 enforcement-rollout  → flip checks to blocking, wire pre-commit/CI, archive
```

This chapter (17a) covers **01 and 02 only**: write the standard, then build the checker in report-only mode. It moves **no source files** — that is 17b and 17c — and it does **not** wire up pre-commit or CI enforcement, which is 17d. The whole point of report-only first is that the checker's output *becomes the backlog* for the moves that follow.

---

## Step 2: Write the Standard Down (`CODING_STANDARD.md`)

### Why a spec is the first deliverable

Plan 01 produces exactly one artefact: a document. No code. Writing the rules down first makes them reviewable before they are automated, and gives the checks (Step 3–4) something concrete to cite. If a check and the doc disagree, the doc wins — it is the source of truth.

Create `docs/architecture/CODING_STANDARD.md`. Here is the spec in full.

```markdown
# QEngine — Coding Standard

How code is organised in `src/`. The goal is **small, single-responsibility,
traceable, testable files**, adapted from the WyrdWars file conventions to C++'s
header/`.cpp` model. Enforced by `scripts/checks/` (see
[plan bundle](../plans/conventions/README.md) / once shipped, `archive/`).

Applies to `src/` (`engine/`, `game/`, `app/`, `harness/`, `main.cpp`).
**Excludes `extern/`** (third-party) and `build/`.
```

The header sets scope up front: the standard governs *your* code in `src/`, never `extern/` (entt, glfw, glad, glm, stb, Jolt) and never `build/`.

#### Rule 1 — One thing per file

```markdown
## 1. One thing per file

- **Free functions** (ECS systems, factories, jolt/body helpers, core/level free
  functions): **one public function per `.h`/`.cpp` pair.** Small `static` helpers
  private to that function may share the `.cpp`.
- **Classes** (`Shader`, `Mesh`, `Texture`, `Camera`, `Window`, `InputManager`,
  `ResourceManager`, `FixedTimestep`, `JoltWorld`): **one class per `.h`/`.cpp` pair**,
  kept whole — the declaration is never split across headers.
- **A long class `.cpp` splits by concern, keeping the single header.** Example:
  `shader.cpp` → `shader_compile.cpp` + `shader_uniforms.cpp`, both implementing
  `Shader`. This is free — consumers' header is unchanged, nothing cascades.
- **A god-class** (one class doing many unrelated things) is decomposed into smaller
  classes / free functions, not merely scattered across `.cpp` files.
```

This is the core of the whole restructure. A free function (every ECS system is one) gets its own `.h`/`.cpp` pair. A class stays whole — you never split `Shader`'s declaration across two headers — but you *may* split its implementation across several `.cpp` files when it gets long, because that is free: the consumers' header never changes, so nothing downstream recompiles. This is the C++-native answer to WyrdWars' per-method splitting, which would be wrong here.

#### Rule 2 — Types live in `types/`

```markdown
## 2. Types live in `types/`

- **Incidental types** — param/return structs, small value types, standalone enums,
  `using` aliases — live in a `types/` folder near their domain (`ecs/types/`,
  `physics/types/`, `level/types/`, `harness/types/`).
- **Exception: ECS components stay in `components/`.** They are the domain model, not
  incidental shapes. Keep `components/{core,physics,combat,gameplay,rendering,tags}.h`.
- **Enum placement:** an enum that is part of a component's contract (`WeaponType`,
  `FireMode`, `MoverState`, `TriggerAction`) stays with that component group. A
  standalone enum (`SystemPhase`) goes in `types/`.
```

Incidental shapes — the little structs a function takes or returns (`EntityHit`, `Ray`/`RayHit`, `PointLightGPU`) — move to a `types/` folder beside their domain. The deliberate exception is ECS **components**: they are the domain model, not incidental, so they stay first-class in `components/`. Enums split on contract: an enum that is part of a component's contract rides with the component; a standalone enum (`SystemPhase`) goes to `types/`.

#### Rule 3 — Barrels & includes (the compile-cost rule)

```markdown
## 3. Barrels & includes (the compile-cost rule)

- A **barrel** is a header that only `#include`s a folder's leaves (no definitions).
  One per folder, named `<folder>.h`. Barrel **nesting depth ≤ 2** (folder → subfolder
  → leaf).
- **Prefer the narrowest include** that covers a file's needs.
- **Headers are strict:** a `.h` includes a **specific leaf**, or **forward-declares**
  (`struct Foo;`) when only the name is needed — **never the umbrella barrel.** This is
  what stops incremental-rebuild cascades from spreading.
- **`.cpp` files may be relaxed:** including a barrel in a `.cpp` only costs that TU.
- **Pimpl** is available to compile-firewall a heavy class, but is not mandated at the
  current scale.
```

This is the rule that protects your build times. A *barrel* is a convenience front door — a header that only re-exports a folder's leaves. Barrels are fine in `.cpp` files (the cost is local to that one TU). But **headers must be strict**: a `.h` includes the specific leaf it needs, or forward-declares the name, and **never** pulls in an umbrella barrel — because a header is included transitively, and a barrel in a header drags the whole folder into every consumer.

#### Rule 4 — File-size caps (smell thresholds)

```markdown
## 4. File-size caps (smell thresholds)

Coarser than WyrdWars — C++ headers are verbose. These flag *candidates to split*,
not hard failures (blocking only after the rollout plan).

Recalibrated after the first full report — a cohesive C++ system runs ~150–190
lines (longer than the TS equivalent), so the caps catch genuine monsters
(combat was 524, debug_hud 433) without fragmenting cohesive units.

| Area | Cap (lines) |
|------|-------------|
| `ecs/systems/**/*.cpp` (free fn) | 190 |
| factory / app bootstrap `.cpp` | 150 |
| `level/**/*.cpp` free fn | 100 |
| class `.cpp` (`renderer/`, `core/`, `physics/`) | 200 (beyond → split `.cpp`) |
| non-barrel, non-types `.h` | 100 |
| `components/**`, `types/**`, barrels | exempt |
| `extern/**`, `build/**`, tests, Jolt glue | exempt |
```

These are **smell thresholds, not hard law** — at least until the enforcement rollout (17d). Note the calibration story baked into the doc: the caps you see here are *not* the first numbers tried. Plan 01 proposed tighter values (systems at 120, headers at 70). After running the first real report against the live tree, those proved too aggressive — a *cohesive* single-responsibility C++ system genuinely runs ~150–190 lines, longer than its TypeScript equivalent. So the caps were loosened to catch genuine monsters (524, 433) without forcing you to fragment units that are already cohesive. The lesson: tune the threshold against the real codebase, not against a guess.

#### Rule 5 — Folder shape

```markdown
## 5. Folder shape

- Group by domain; one barrel header per folder; one `types/` per folder that needs it.
- Long systems live in `ecs/systems/<domain>/` (entry function + one-function helpers
  + barrel).
- Files live in the folder that owns their domain — e.g. Jolt body creation in
  `physics/`, world bootstrap in `app/`, level content in `level/`.
```

Group by domain. A long system becomes a folder: `ecs/systems/combat/` holds the entry function, the one-function helpers it was split into, and a barrel. Misplaced files move to the folder that owns their domain.

#### Rule 6 — Not governed by this standard

```markdown
## 6. Not governed by this standard

- Runtime behaviour (protected by the headless regression suite).
- `extern/` and third-party patterns.
- Public API naming, unless a move requires an update.
```

Explicitly out of scope: behaviour (the headless suite guards that — every move is file-shuffling plus include edits, never a logic change), third-party code, and existing public names.

#### The verify + enforcement footer

The doc closes by spelling out how to check a change and where enforcement lives — the parts 17d will activate.

```markdown
## Verifying a change

```bash
export PATH="/c/msys64/ucrt64/bin:$PATH"   # required — the compiler needs it on PATH
cmake --build build
for s in rest_no_jitter ride_lift_up walk_onto_lift walk_floor_seams \
         rocket_vs_floor teleporter; do ./build/QEngineHeadless.exe "$s" | grep -E "PASS|FAIL"; done
```

Plus `python scripts/checks/run_all.py --report` for convention compliance.

## Enforcement

- **CI:** the `conventions` job in `.github/workflows/code-quality.yml` runs
  `run_all.py --strict` on every PR — a finding fails the job.
- **Local (opt-in):** `git config core.hooksPath scripts/githooks` installs a
  pre-commit hook running the same checks.
- **Config:** size caps, allowlists, and skip globs live in `scripts/checks/rules.py`
  (one edit point). Allowlist entries should each say *why*.
```

> **Note the PATH gotcha.** The build line exports `/c/msys64/ucrt64/bin` onto PATH *before* `cmake --build`. Without it the compiler dies silently. Always run the 6 headless scenarios after any move to confirm behaviour is unchanged.

With the spec accepted, the checks now have something to cite.

---

## Step 3: The Checker Config (`scripts/checks/rules.py`)

### Why a single config file

Plan 02 chose **Python (stdlib only)** for the checks — QEngine has no Node toolchain, Python is already on the box, and heuristic line-scanning (the same approach WyrdWars takes) is enough. Crucially, *all* the tunable knobs — size caps, skip globs, allowlists — live in **one** file, `scripts/checks/rules.py`. When a cap is wrong or an exception is legitimate, you edit one place. Every allowlist entry carries a comment saying *why* it is exempt, so the file doubles as a ledger of sanctioned exceptions.

Create `scripts/checks/rules.py`:

```python
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
    ("**/factories*.cpp", 150, "factory"),   # cohesive spawn module — cap follows the file
    ("src/engine/level/**/*.cpp", 100, "level free-fn"),
    ("src/engine/core/**/*.cpp", 200, "core class"),
    ("src/engine/renderer/**/*.cpp", 200, "renderer class"),
    ("src/engine/physics/**/*.cpp", 200, "physics"),
    ("src/engine/app/**/*.cpp", 150, "app"),
    ("src/harness/**/*.cpp", 400, "test harness"),  # scenario-heavy, lenient
    ("src/**/*.cpp", 150, "cpp (default)"),
    ("src/**/*.h", 100, "header"),
]

# Files/dirs never checked by any rule (data, third-party, generated, dead code).
SKIP_GLOBS = [
    "src/engine/ecs/components/**",
    "src/engine/ecs/components.h",
    "**/types/**",
    "**/archived/**",
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
```

Walking the five sections:

- **`SIZE_RULES`** — a list of `(glob, cap, label)` tuples, **most-specific first** because **first match wins**. The default `src/**/*.cpp` at 150 is the catch-all; everything above it is a deliberate override (systems get 190, the test harness a lenient 400). These are the recalibrated numbers from Rule 4.
- **`SKIP_GLOBS`** — files no check ever touches: data (`components/**`, `types/**`), third-party (`extern/**`), generated (`build/**`), and crucially the **banner-dead** units (`collision`, `spatial_hash`, the `.qlvl` `level_loader`) — code that was removed from the build but kept on disk pending an explicit deletion call. You don't lint dead code.
- **`TYPE_LOCATION_ALLOWLIST`** — places where a top-level `struct`/`enum`/`using` is *allowed* outside a `types/` folder. Each entry is justified inline: ECS components are first-class; `jolt_world.h`'s struct *is* the class for that unit; `simulation.h` only forward-declares; `level.h` is a cohesive data header whose move-or-keep call is deferred to Plan 03.
- **`UMBRELLA_BARRELS`** — the barrels a header may not pull in. Right now just `engine/ecs/components.h` — the big one whose inclusion in a header would cascade rebuilds.
- **`DEAD_HEADERS`** — banner-labelled legacy headers (`collision.h`, `spatial_hash.h`) that no *live* file should `#include`, so dead code can't silently creep back in.

---

## Step 4: The Runner (`scripts/checks/run_all.py`)

### Why heuristic line-scanners (and not a real parser)

The runner is a set of regex/line-scanners, **not** a C++ parser — exactly like the WyrdWars checks it mirrors. A real parser would be heavier to write and maintain than the problem warrants. Heuristics produce the occasional false positive (a `struct` inside a function, a template), which is why the scanners track brace depth, strip comments, and lean on the allowlist. The bar is "its report matches a manual audit of a sample folder," not "it is a compiler." It is stdlib-only so it runs identically on your Windows box and CI Linux.

Create `scripts/checks/run_all.py`:

```python
#!/usr/bin/env python3
"""QEngine convention checks — enforces docs/architecture/CODING_STANDARD.md.

Heuristic line scanners (like wyrdwars/scripts/checks), NOT a real C++ parser —
good enough to drive the restructure and catch drift. stdlib-only; runs on
Windows + CI Linux.

    python scripts/checks/run_all.py            # report mode (always exit 0)
    python scripts/checks/run_all.py --strict   # exit 1 on any finding (CI/hook)

Checks: file sizes, type locations, header discipline, dead-header includes,
single-function. Config lives in rules.py.
"""
import argparse
import os
import re
import sys
from collections import namedtuple

import rules

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))

Finding = namedtuple("Finding", "check file line detail")

CONTROL = {"if", "for", "while", "switch", "return", "else", "do", "catch", "sizeof"}
DECL_KEYWORDS = {"struct", "class", "enum", "namespace", "using", "typedef",
                 "template", "friend", "public", "private", "protected", "static_assert"}


# ─── glob / path helpers ────────────────────────────────────────────────────
def glob_to_regex(glob):
    out, i = ["^"], 0
    while i < len(glob):
        if glob[i:i + 3] == "**/":
            out.append("(?:.*/)?"); i += 3
        elif glob[i:i + 2] == "**":
            out.append(".*"); i += 2
        elif glob[i] == "*":
            out.append("[^/]*"); i += 1
        elif glob[i] == "?":
            out.append("[^/]"); i += 1
        elif glob[i] in ".+()[]{}^$|\\":
            out.append("\\" + glob[i]); i += 1
        else:
            out.append(glob[i]); i += 1
    out.append("$")
    return re.compile("".join(out))


_GLOB_CACHE = {}


def matches_any(path, globs):
    for g in globs:
        rx = _GLOB_CACHE.get(g) or _GLOB_CACHE.setdefault(g, glob_to_regex(g))
        if rx.match(path):
            return True
    return False


def iter_source_files():
    src = os.path.join(ROOT, "src")
    files = []
    for dirpath, _dirs, names in os.walk(src):
        for n in names:
            if n.endswith((".h", ".hpp", ".cpp", ".cc")):
                rel = os.path.relpath(os.path.join(dirpath, n), ROOT).replace("\\", "/")
                files.append(rel)
    return sorted(files)


def read_text(rel):
    with open(os.path.join(ROOT, rel), "r", encoding="utf-8", errors="replace") as f:
        return f.read()


def strip_block_comments(text):
    """Blank out /* */ regions and // tails so scanners ignore comment content."""
    out, i, n, in_block = [], 0, len(text), False
    while i < n:
        two = text[i:i + 2]
        if in_block:
            if two == "*/":
                in_block = False; i += 2
            else:
                out.append("\n" if text[i] == "\n" else " "); i += 1
        elif two == "/*":
            in_block = True; i += 2
        elif two == "//":
            while i < n and text[i] != "\n":
                i += 1
        else:
            out.append(text[i]); i += 1
    return "".join(out)


def code_lines(rel):
    """List of (lineno, raw_line) with comments stripped; blank lines kept for nums."""
    stripped = strip_block_comments(read_text(rel))
    return list(enumerate(stripped.split("\n"), start=1))


def is_barrel(rel):
    if not rel.endswith((".h", ".hpp")):
        return False
    for _ln, line in code_lines(rel):
        s = line.strip()
        if not s or s.startswith("#pragma") or s.startswith("#include"):
            continue
        return False  # a non-include code line → not a pure barrel
    return True


# ─── checks ─────────────────────────────────────────────────────────────────
def check_file_sizes():
    findings = []
    for rel in iter_source_files():
        if matches_any(rel, rules.SKIP_GLOBS) or is_barrel(rel):
            continue
        cap = area = None
        for glob, lim, label in rules.SIZE_RULES:
            if matches_any(rel, [glob]):
                cap, area = lim, label
                break
        if cap is None:
            continue
        n = len(read_text(rel).split("\n"))
        if n > cap:
            findings.append(Finding("file-sizes", rel, n, f"{n} lines > {cap} ({area})"))
    return findings


# Only incidental types move to types/ — NOT `class` (a class is a first-class
# unit that stays as its own .h/.cpp; see CODING_STANDARD §1/§2).
DEF_ONELINE = re.compile(r"^(struct|enum\s+class|enum|union)\s+(\w+)")
USING_ALIAS = re.compile(r"^using\s+(\w+)\s*=")


NAMESPACE_RE = re.compile(r"^namespace\b")


def check_type_locations():
    """Flag struct/enum/using *definitions* at top level (file or namespace scope).

    Types nested inside a function or class body are local implementation detail
    and are NOT flagged — so brace depth is tracked, treating `namespace` as
    transparent (a namespace-scoped type still counts as top-level).
    """
    findings = []
    for rel in iter_source_files():
        if matches_any(rel, rules.SKIP_GLOBS) or matches_any(rel, rules.TYPE_LOCATION_ALLOWLIST):
            continue
        stack = []          # brace frames: "ns" (namespace) or "block" (fn/class/...)
        pending_ns = False  # saw `namespace X` whose `{` is on a later line
        for ln, line in code_lines(rel):
            s = line.strip()
            at_top = all(f == "ns" for f in stack)
            if at_top:
                m = DEF_ONELINE.match(s)
                if m and not (s.rstrip().endswith(";") and "{" not in s):
                    findings.append(Finding("type-locations", rel, ln,
                                            f"{m.group(1)} {m.group(2)} -> move to a types/ folder"))
                else:
                    u = USING_ALIAS.match(s)
                    if u and not s.startswith("using namespace"):
                        findings.append(Finding("type-locations", rel, ln,
                                                f"using {u.group(1)} -> move to a types/ folder"))
            # update brace stack (namespace braces are transparent)
            is_ns_line = bool(NAMESPACE_RE.match(s))
            if is_ns_line and "{" not in line:
                pending_ns = True
                continue
            for ch in line:
                if ch == "{":
                    if pending_ns:
                        stack.append("ns"); pending_ns = False
                    elif is_ns_line:
                        stack.append("ns")
                    else:
                        stack.append("block")
                elif ch == "}" and stack:
                    stack.pop()
    return findings


INCLUDE_RE = re.compile(r'^\s*#\s*include\s+[<"]([^">]+)[">]')


def check_header_discipline():
    findings = []
    for rel in iter_source_files():
        if not rel.endswith((".h", ".hpp")) or is_barrel(rel):
            continue
        if matches_any(rel, rules.SKIP_GLOBS):
            continue
        for ln, line in code_lines(rel):
            m = INCLUDE_RE.match(line)
            if m and any(m.group(1).endswith(b) for b in rules.UMBRELLA_BARRELS):
                findings.append(Finding("header-discipline", rel, ln,
                                        f'header includes umbrella barrel "{m.group(1)}" '
                                        f"-> use a specific leaf or forward-declare"))
    return findings


def check_dead_includes():
    findings = []
    for rel in iter_source_files():
        # skip archived/ + dead units (their own pairing isn't a violation)
        if matches_any(rel, rules.SKIP_GLOBS):
            continue
        if any(rel.endswith(d) for d in rules.DEAD_HEADERS):
            continue
        for ln, line in code_lines(rel):
            m = INCLUDE_RE.match(line)
            if m and any(m.group(1).endswith(d) for d in rules.DEAD_HEADERS):
                findings.append(Finding("dead-includes", rel, ln,
                                        f'includes banner-dead header "{m.group(1)}"'))
    return findings


SIG_PAREN = re.compile(r"^([A-Za-z_][\w:<>,\*&\s]*?)\b(\w+)\s*\(")
SIG_NAME_ONLY = re.compile(r"^([A-Za-z_][\w:<>,\*&\s]*?)\b(\w+)\s*$")


def check_single_function():
    """Heuristic: a free-function .cpp should define one top-level function.

    Class-impl files (any `Type::method(` definition) are exempt — a class may
    have many methods. Handles both `ret name(` and `ret name` + `(` on next line.
    """
    findings = []
    for rel in iter_source_files():
        if not rel.endswith((".cpp", ".cc")):
            continue
        if matches_any(rel, rules.SKIP_GLOBS):
            continue
        lines = code_lines(rel)
        names, is_class_impl = [], False
        for idx, (ln, raw) in enumerate(lines):
            if not raw or raw[0] in " \t#":  # only column-0 lines = file-scope defs
                continue
            s = raw.rstrip()
            m = SIG_PAREN.match(s)
            if not m:
                mo = SIG_NAME_ONLY.match(s)
                nxt = lines[idx + 1][1].lstrip() if idx + 1 < len(lines) else ""
                if mo and nxt.startswith("("):
                    m = mo
            if not m:
                continue
            prefix, name = m.group(1), m.group(2)
            first = s.split()[0]
            if name in CONTROL or first in DECL_KEYWORDS or first in CONTROL:
                continue
            if "::" in prefix or "::" in name or f"{name}::" in s:
                is_class_impl = True
                break
            names.append((ln, name))
        if is_class_impl:
            continue
        if len(names) > 1:
            detail = ", ".join(n for _l, n in names)
            findings.append(Finding("single-function", rel, names[1][0],
                                    f"{len(names)} top-level functions: {detail}"))
    return findings


CHECKS = [check_file_sizes, check_type_locations, check_header_discipline,
          check_dead_includes, check_single_function]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--strict", action="store_true", help="exit 1 on any finding")
    args = ap.parse_args()

    all_findings = []
    results = []
    for check in CHECKS:
        fs = check()
        results.append((check.__name__.replace("check_", "").replace("_", "-"), fs))
        all_findings.extend(fs)

    print("\n=== QEngine convention report ===\n")
    for name, fs in results:
        print(f"  {name:20s} {len(fs):3d} finding(s)")
    print()

    for name, fs in results:
        if not fs:
            continue
        print(f"-- {name} " + "-" * max(0, 40 - len(name)))
        for f in sorted(fs, key=lambda x: (x.file, x.line)):
            print(f"  {f.file}:{f.line}  {f.detail}")
        print()

    total = len(all_findings)
    print(f"  TOTAL: {total} finding(s)\n")
    if args.strict and total:
        sys.exit(1)


if __name__ == "__main__":
    main()
```

### What each check does

The runner shares a small toolkit first — `glob_to_regex` (the config uses globs; Python's `re` does the matching), `iter_source_files` (walks `src/` for `.h`/`.hpp`/`.cpp`/`.cc`), `strip_block_comments` (blanks `/* */` and `//` so scanners never trip on comment text while keeping line numbers intact), and `is_barrel` (a header that is *only* `#pragma`/`#include` lines — pure re-export, nothing to lint). Then five checks run in order:

- **`check_file_sizes`** — for each non-skipped, non-barrel file, finds the first matching `SIZE_RULES` entry and flags the file if its line count exceeds that cap. This is the check that names the god-files. Barrels and `types/`/`components/` are exempt because length there is fine — they are aggregation, not logic.
- **`check_type_locations`** — flags a top-level `struct`/`enum`/`enum class`/`union`/`using X =` defined *outside* a `types/` folder and *not* on the `TYPE_LOCATION_ALLOWLIST`. The subtlety is brace-depth tracking: a type nested inside a function or class body is local implementation detail and is **not** flagged, but `namespace` braces are treated as transparent so a namespace-scoped type still counts as top-level. A forward declaration (`struct Foo;` with no `{`) is also not flagged.
- **`check_header_discipline`** — for each non-barrel `.h`, flags any `#include` of an `UMBRELLA_BARRELS` entry. This is the C++-specific rule from §3: a header that pulls in `components.h` should use a leaf include or a forward declaration instead. It is the lever against rebuild cascades.
- **`check_dead_includes`** — flags any *live* file that `#include`s a `DEAD_HEADERS` entry, so the banner-marked legacy code (`collision.h`, `spatial_hash.h`) cannot quietly come back into the build. The dead files themselves are skipped — their own pairing isn't a violation.
- **`check_single_function`** — for free-function `.cpp` files, counts column-0 (file-scope) function *definitions*; more than one is flagged. Class-impl files are **exempt** the moment a `Type::method` definition is seen, because a class legitimately has many methods. It handles both `ret name(` on one line and `ret name` with the `(` on the next, and skips control keywords and declaration keywords so an `if (` or a `struct {` is never miscounted as a function.

### `--report` vs `--strict`

`main` runs every check, prints a summary table (findings per check) followed by the detailed per-file list, then the total. The *only* behavioural difference between the two modes is the exit code:

- **report mode (the default)** — always `exit 0`. It surveys and informs; it never fails a build. This is what you run while doing the restructure.
- **`--strict`** — `exit 1` if there is *any* finding. This is the blocking mode for the pre-commit hook and the CI `conventions` job.

In 17a we only ever run report mode. The `--strict` flip — wiring it into the pre-commit hook and CI — is **17d**. Building the strict path now but leaving it dormant is deliberate: the tooling is ready before the tree is clean, so enforcement is a one-line switch once the moves are done.

---

## Step 5: Run It Report-Only — The Baseline Survey

### Why a baseline first

You never start moving files blind. Run the checks report-only against the current tree; the output *is* your backlog. Every finding is either a real convention gap (fix it in 17b/17c) or a legitimate exception (allowlist it in `rules.py`, with a reason). When the report is empty and the headless suite still passes, the restructure is done.

Run it from the repo root:

```bash
python scripts/checks/run_all.py --report
```

The output is a summary table followed by the detail, in this shape:

```
=== QEngine convention report ===

  file-sizes             N finding(s)
  type-locations         N finding(s)
  header-discipline      N finding(s)
  dead-includes          N finding(s)
  single-function        N finding(s)

-- file-sizes ----------------------------
  src/engine/ecs/systems/combat_system.cpp:524  524 lines > 190 (system free-fn)
  src/engine/ecs/systems/debug_hud_system.cpp:433  433 lines > 190 (system free-fn)
  ...

  TOTAL: N finding(s)
```

The two god-files from Step 1 — `combat_system.cpp` at 524 and `debug_hud_system.cpp` at 433 — top the `file-sizes` list, exactly as the calibration intended. `type-locations` enumerates the incidental structs and enums still sitting outside a `types/` folder; `header-discipline` lists any header still reaching for the `components.h` umbrella; `single-function` flags the `.cpp` files that pack more than one free function. Report mode exits 0, so this is a survey, not a gate.

This findings list is the concrete backlog Plans 03–06 work through. The first thing it tells us to attack is the biggest monster — and that is where the next chapter begins.

---

## Summary

In this chapter we laid the foundation for the whole restructure without moving a single line of engine code:

- Diagnosed the motivation: two god-files (`combat_system.cpp` at 524 lines, `debug_hud_system.cpp` at 433) cost us **traceability, testability, and compile time**, with the goal being *small, single-responsibility, traceable, testable files* adapted from WyrdWars to C++.
- Wrote the standard down **first** in [`CODING_STANDARD.md`](../../architecture/CODING_STANDARD.md) — six rules covering one-thing-per-file, `types/` placement, the barrel/include compile-cost rule, recalibrated size caps, folder shape, and what is explicitly out of scope.
- Built the single-edit-point config `scripts/checks/rules.py` — `SIZE_RULES`, `SKIP_GLOBS`, `TYPE_LOCATION_ALLOWLIST`, `UMBRELLA_BARRELS`, `DEAD_HEADERS` — with every exception carrying a *why*, and caps recalibrated to ~150–190 lines after the first real report.
- Built the runner `scripts/checks/run_all.py` — five stdlib-only heuristic scanners (file-sizes, type-locations, header-discipline, dead-includes, single-function) with `--report` (always exit 0) and a dormant `--strict` (exit 1 on any finding) for later enforcement.
- Ran it report-only to produce the baseline survey — the findings list that becomes the restructure backlog.

## What's Next

The checker has named the biggest offender. In **Chapter 17b — Types & The Combat Split** ([`Chapter_17b_Types_And_Combat_Split.md`](Chapter_17b_Types_And_Combat_Split.md)) the actual file restructure begins: we establish the `types/` folders, move the incidental structs and enums out of the way, and break the 524-line `combat_system.cpp` apart into a cohesive `systems/combat/` folder — one function per file, header discipline intact, behaviour verified by the headless suite at every step.
