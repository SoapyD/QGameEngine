# QEngine — Active Plans (June 2026)

A fresh set of forward-looking plans, written after the evaluation/fix bundle in
[`archive/eval/`](archive/eval/README.md) shipped (lift/teleporter/projectile fixes
+ headless harness, 2026-06-08). These look at *where the project goes next* rather
than fixing existing bugs.

| # | Plan | Question it answers |
|---|------|--------------------|
| 2 | [02-next-features.md](02-next-features.md) | Given the objects we can build today + the roadmap, what should we build next? |
| 3 | [03-trenchbroom-setup.md](03-trenchbroom-setup.md) | How do we get the map-building tool (TrenchBroom) running, and what's needed to author a level like the hard-coded showcase? |
| 4 | [04-architecture-docs-and-engine-overview.md](04-architecture-docs-and-engine-overview.md) | Plan to update the architecture docs + a high-level "how the engine works" refresher. |

**Active implementation plans (dated, graduated from the above):**

| Date | Plan | Graduated from | Status |
|------|------|----------------|--------|
| 2026-07-02 | [item pickups](2026-07-02-item-pickups.md) | Plan 02 #2 | Phase 1 shipped; Phase 2 pending |

These are **planning documents** — they propose work, they don't perform it.
Implementation plans graduate into their own dated `docs/plans/YYYY-MM-DD-<name>.md` files
and are moved to `archive/` once shipped (the project's existing habit).


**Shipped & archived:**
- [2026-07-02-entity-factory-classname-dispatch.md](archive/2026-07-02-entity-factory-classname-dispatch.md)
  — data-driven entity spawning: `classname`→factory dispatch + `SpawnParams` + two-pass
  `targetname` linking; showcase rebuilt from a descriptor list. Graduated from Plan 02 #1 /
  Plan 03 step 2.0. Shipped 2026-07-02; tutorial Chapter 18.
- [conventions/](archive/conventions/README.md) — adopt WyrdWars-style file
  conventions in C++ (one-thing-per-file, `types/` folders, barrel/header
  discipline) + C++-aware enforcement checks in CI. 7 plans; shipped 2026-06-15.
- [01-project-improvements.md](archive/01-project-improvements.md) — current-system
  improvements & layout review. Implemented and verified 2026-06-14 (factories,
  gravity→PhysicsConfig, input/camera systems, no-GL harness, legacy labeling,
  trigger generalisation, shader uniform caching, `components.h` split, doc-sync).
