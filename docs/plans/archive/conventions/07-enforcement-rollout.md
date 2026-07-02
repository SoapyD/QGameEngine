# Plan 07 — Enforcement, CI Wiring & Rollout

**Status: PROPOSED.** The closing plan: flip the checks from report-only to blocking
once the tree is compliant, wire them into the commit/CI flow, refresh the docs, and
archive the bundle.

## Preconditions

- Plans 03–06 complete: `run_all.py --report` shows zero findings (or every remaining
  finding is an explicit, documented allowlist entry).
- Build clean; all 6 headless scenarios pass; windowed smoke run OK.

## Steps

1. **Flip to strict.** Make `run_all.py --strict` exit non-zero on any non-allowlisted
   finding. Keep the allowlist file as the single escape hatch (each entry commented
   with *why*).
2. **Local hook.** Add a pre-commit hook (or a `cmake` `checks` target invoked by the
   hook) running `run_all.py --strict` on staged `src/` files. Mirror WyrdWars'
   pre-commit habit. Document in the standard doc.
3. **CI.** Add a `conventions` job to
   [`.github/workflows/code-quality.yml`](../../../../.github/workflows/code-quality.yml)
   running `run_all.py --strict`. It joins the existing branch-flow guard; make it a
   **required status check** if/when branch protection is configured.
4. **Docs refresh.**
   - Finalise `docs/architecture/CODING_STANDARD.md` (from Plan 01) as the canonical
     reference; link from `docs/architecture/README.md`.
   - Update `docs/architecture/ARCHITECTURE.md` project-structure tree to the new
     folder layout (systems/<domain>/, types/, relocated files).
   - Update `docs/processes/*` and `docs/status/*` where file paths moved.
5. **Archive.** Move `docs/plans/conventions/` under `docs/plans/archive/` and add the
   shipped-bundle line to `docs/plans/README.md` (per the project's archive habit).

## Risks

- Turning on `--strict` may surface stragglers the per-folder plans missed — treat the
  first strict run as a punch-list, not a wall; allowlist with intent or fix.
- A required CI check can block merges — only mark it required after a green run on the
  integration branch (mirrors the branch-flow-guard caution).

## Verification

- `run_all.py --strict` exits 0 on a clean checkout.
- A deliberately-broken file (extra export / type outside `types/` / header pulling a
  barrel) makes both the hook and CI fail — proving enforcement works.
- Build clean, all 6 scenarios pass, windowed smoke run OK.

## Done when

Checks are blocking locally + in CI, docs reflect the new structure, and this bundle
is archived. The convention is then self-sustaining — new code can't drift without a
red check.
