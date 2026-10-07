# Plan 48: retarget from TypeScript 5.8 to the current language (6.0 / 7.0)
Written by the insights of 2026-10-07 as a **stub for a planning session**: the planner investigates, decides and
rewrites this file into the real plan (stages, validation, session budget).

## Why
tttt's every oracle is TypeScript 5.8.2 (February 2025): the suite (`thirdparty/tsc` at v5.8.2), `tscbaseline.sh`,
the sweep / `$ExpectType` / flagflip / mutation caches, and every corpus pinned at "the last commit before
TypeScript 6". Since then 5.9 (2025-07), **6.0** (2026-03, the last JavaScript tsc, changed defaults and deprecations
as a bridge to 7) and **7.0** (2026-07, tsgo, now the `typescript` package's `latest`) shipped. The competitor we
benchmark against implements a language one major version newer than the one we check; it rejects two of our corpus
configs outright (`baseUrl`, TS5102). Real projects (our corpora's upstreams, DefinitelyTyped's CI) have moved on, so
"fully support TypeScript" now means 6.0/7.0 semantics. Every session spent polishing 5.8 parity from here is spent on
a frozen target.

## Measured at writing (blob hashes, `git ls-tree` of v5.8.2 vs v6.0.3, `/tmp/ts6probe` is a blobless clone)
- `tests/cases`: 11747 files changed, 245 added, 11 removed. Sampled diffs: mostly options made explicit
  (`// @target: es2015`, `// @module: commonjs`) and code fixed for strict-by-default (`isSunk: boolean = false`).
- `tests/baselines/reference`: 22255 changed (`.types` 3080, `.symbols` 2436, `.errors.txt` 2113, emit the rest),
  11614 added, 3976 removed.
So the suite is mostly the same tests made to keep their meaning under new defaults: the bulk should be cheap, but the
pass count is not comparable across the switch, and "0 lost" needs a per-test definition during the transition.

## Questions for the planning session
1. **Which oracle**: tsc 6.0.3 (JavaScript, the repo's own baselines, runs anywhere tscbaseline.sh/sweep need it) or
   tsgo 7.0.2 (typescript-go's `testdata/baselines/reference/submodule/**` plus its `.diff` files against 6.0). The
   prior: 6.0.3 for the suite and caches, tsgo 7 as the benchmark and a cross-check, unless tsgo's differences are
   ones we'd want anyway. **Look specifically at type ordering**: tsgo orders union members deterministically instead
   of by creation order (6.0 has `--stableTypeOrdering` to emulate it). tttt's creation-order mimicry is a source of
   hacks (hacks.md, `argorder.py`, checkerdiff's "may differ inside quotes, union order") and of parallel
   nondeterminism. If 7.0's ordering is the language's future, adopting it could delete a class of fragility; measure
   how many `.types` baselines differ between 6.0 and tsgo by ordering alone.
2. **What changed in the checker** between 5.8 and 6.0 that our tests will see: option defaults (strict, target,
   module, types, ...), deprecation diagnostics (TS5101/TS5107-style for es5, node10, baseUrl, ...,
   `ignoreDeprecations`), lib changes (es2025, lib.dom refresh), new features (`import defer`, `--module node20`, 5.9's
   and 6.0's release notes), changed messages. List them with test counts.
3. **Staging**: one switch commit (new `thirdparty/tsc`, harness defaults, saved results regenerated) followed by
   fan-outs, or both suites side by side for a while? Prototype the switch in a worktree and measure the suite under
   6.0 before committing to a budget.
4. **The other oracles**: regenerate the sweep / `$ExpectType` / flagflip / mutation / scenarios caches with the new
   tsc, move DefinitelyTyped to a current commit, re-pin the corpora at current upstream commits (and drop the 5.9
   lib shims). Order and budget.
5. What of 5.8 behaviour to keep: none by default (tsc 6 itself still supports the deprecated options under
   `ignoreDeprecations`, so do they). Delete code that only 5.8 needed.

## Validation (for the real plan to refine)
Suite under the new baselines at or above today's 13303 / 13540 equivalent, every corpus re-pinned and at 0 against
the new tsc, sweep and `$ExpectType` at today's match rates against the new caches, bench against tsgo 7 on configs it
accepts.
