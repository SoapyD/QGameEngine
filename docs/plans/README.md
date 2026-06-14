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

These are **planning documents** — they propose work, they don't perform it.
Implementation plans graduate into their own `docs/plans/NN-<name>.md` files and are
moved to `archive/` once shipped (the project's existing habit).

**Shipped & archived:**
- [01-project-improvements.md](archive/01-project-improvements.md) — current-system
  improvements & layout review. Implemented and verified 2026-06-14 (factories,
  gravity→PhysicsConfig, input/camera systems, no-GL harness, legacy labeling,
  trigger generalisation, shader uniform caching, `components.h` split, doc-sync).
