# Plan 74: relation reporting runs shaped per call site like tsc's (half a session)
Written for plan 71's question "Two passes where tsc has one: is one reporting run worth a plan?" (2026-10-15).

## Findings (measured with a throwaway counter build, `check_related()` top-level runs)
- **tsc isn't one-run either.** checkTypeRelatedToAndOptionallyElaborate (assignments, arguments, returns, the
  elaborate* parts, satisfies) does `isTypeRelatedTo` then `checkTypeRelatedToEx`: two runs, the second finding the
  first's cached failures. Only callers of checkTypeRelatedTo/checkTypeAssignableTo/checkTypeComparableTo *with an error
  node* run once, reporting from the start (heritage TS2415/2420/2430, TS2344 constraints, TS2352/2678 comparable,
  TS2677, TS2786 JSX components, checker.go's `this` argument and spread rest checks, generator/iterator results,
  ...). In the suite 5023 of 5964 reporting runs come from tttt's report_unassignable (the two-run shape).
- **Cost of the second run: negligible.** Suite: 5964 reporting runs did 5.28M is_related_to calls against 10.85M in
  224744 plain runs, but 5.25M of them are one test (relationComplexityError, the TS2859 pair rerun to exhaustion; see
  stage 0). Without it reporting is 25k calls, 0.5% of relation work, in an error-dense suite. Corpora (checkers 1):
  drizzle 2.8k of 801k calls (2.4% of relation cycles: message printing, which one run still pays), webpack 1.2k/1.24M,
  effect 1.1k/681k, vue 0.9k/1.85M, xstate 1.9k/228k, TypeScript6 0. The plain run a reporting run of the same pair
  follows (what one run would save) is smaller still (suite without the outlier: 20k calls).
- **Results never differ.** 0 of the suite's 5964 reporting runs and of the corpora's ~950 found the pair related.
- **What hangs off the shape:** only `Checker.failed_log` (relate.c failed_log_push(), ~40 lines in check_related(),
  ty.h, checker.c) and its hacks.md entry. The other reporting-only code (`!st->reporting` fast paths in
  check_related, fresh_union, alias_fallback_*, tuple_array_source, head_reported) mirrors tsc's isTypeRelatedTo vs
  checkTypeRelatedTo and reportErrors distinctions, which tsc has too.
- **failed_log is wrong in the other direction:** it forgets the first run's failures before *any* reporting run of
  the same pair, including the two-run callers where tsc keeps them (check_assignable when elaborate_error() relates
  nothing in between, e.g. `ty_check_assignable`'s expr-less callers that are tsc's OptionallyElaborate ones).

So: not a perf plan. It's a small fidelity cleanup that deletes a hack: each tttt caller runs the shape its tsc caller
does, and failed_log goes.

## Stage 0: cache a top-level overflow (tsgo's ComplexityOverflow entry)
tsgo's checkTypeRelatedToEx records an overflowing top-level pair as `Failed|ComplexityOverflow`; recursiveTypeRelatedTo
under reportErrors then reports TS2859 from the cache instead of recomputing. tttt's check_assignable() complexity
branch reruns the whole relation (5.25M calls in relationComplexityError, ~half that test's time). Add a
`CACHE_OVERFLOW` bit set in check_related() on `st->relation_count <= 0`, and have recursive_related() under `report`
return FALSE and set `st->overflow` (so check_related reports TS2859 as now) on it. Output must stay identical
(relationComplexityError's two TS2859); on real code this halves the cost of every TS2859.

## Stage 1: one-run callers
1. Add `ty_check_related_reporting(me, source, target, relation, head_code, VecTyReportLine* out)` (or reuse
   `ty_explain_unrelated[_headed]`, which already returns whether the pair is related) and make the tsc-one-run call
   sites call it *instead of* `ty_is_related()` + explain. Audit each call site of report_unrelated_with_head /
   report_unassignable_with_head / ty_explain_unrelated / ty_check_assignable / ty_report_unfit_this against its tsgo
   caller (`grep -n "checkTypeRelatedTo\|checkTypeAssignableTo\|checkTypeComparableTo" internal/checker/*.go`): a tsgo
   caller passing an error node to those is one run; checkTypeRelatedToAndOptionallyElaborate and
   `isTypeRelatedTo(...) || checkTypeRelatedTo(...)` are two. `ty_check_assignable` (check_assignable with no expr) is
   both today: split it (one-run `ty_check_assignable`, two-run where the tsgo caller is the OptionallyElaborate one).
2. Delete `failed_log` (relate.c, ty.h, checker.c) and the hacks.md entry.
3. Watch the success path: a one-run caller now explains pairs that are related (every class's heritage check). The
   reporting run skips check_related's fast paths (tsc's checkTypeRelatedTo skips isTypeRelatedTo's too) and collects
   lines in branches that later succeed (union members): keep `scripts/instrcount.sh` flat (tsc, vscode, effect).
   If a related pair can come out unrelated under `report` (it shouldn't: 0 seen), that's a bug to fix, not to paper
   over with a plain run first.

## Validation
- complexRecursiveCollections still passes without failed_log (the one-run heritage check finds the pair on its maybe
  stack, as tsgo does); suite 0 lost (`scripts/fasttest.sh --summary`); any test that moves is argued from its tsgo
  caller's shape.
- `scripts/oracles.py` unchanged; `scripts/instrcount.sh` flat or better (stage 0: relationComplexityError's time halves).
- If stage 1 loses tests that only pass through failed_log's over-forgetting, stop at stage 0 + a corrected hacks.md
  entry and record why in this file.
