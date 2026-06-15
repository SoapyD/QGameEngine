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
