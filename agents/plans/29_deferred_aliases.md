# Plan 29: lazy mapped templates and tsc's deferred type references

Written 2026-09-27 (planning session, from plan 19 R18/R19). Stages 1-3 are done; only the optional stage 4 is left.
The full plan and the prototype (`test/probes/plan29/experiment.patch`, deleted with stage 3, still in git at dbc107b)
are in git history.

## Status
- **Stage 1** (lazy mapped template, `ty_mapped_template()`) and **stage 2** (deferred generic interface/class
  references, syntactic `in_alias`, awaitedTypeStack guard): suite 8579 -> 8599, type-fest 53 -> 46, valibot 60 -> 54.
- **Stage 3** (2026-09-27, diagnostics at tsc's sites): `Checker.current_node` (tsc's currentNode) and
  `ty_error_at_current_node()`/`ty_error_at()` report straight into the program's diagnostics; TS2589 (instantiation
  limits, relation overflow), TS2456 (`Checker.aliases_in_flight`, tsc's pushTypeResolution), TS4109/TS4110
  (`TyDeferredRef.circular`), TS2615 (`Prop.circular` + `Prop.mapped_type`; the per-key template property is lazy now,
  `Prop.template_of`), TS1062 (awaited cycle). `report_instantiation_limit()` and `instantiation_limit_hit` are gone.
  Reporting exposed eager spots, fixed tsc's way: `[...T]['length']` (isGenericTupleType), per-conditional tail
  count, the "Resolved by now" deferred-reference shortcut (removed), `is_empty_anonymous_object` (syntactic for type
  literals), conditional types defer on a generic mapped extends type. Suite 8599 -> 8603, both components stages
  1-2 lost are back (recursivelyExpandingUnionNoStackoverflow types, and awaitedType types on top), type-fest 46 ->
  48 (different: see plan 19 R21), valibot 54 (RSS 544 -> 525 MB), react 1, tsc 0, callgrind +0.42%.
  Leftovers in triage.md ("Plan 29 stage 3 leftovers").

## Stage 4 (optional, measure first): always-lazy conditional branches
tsc resolves `trueType`/`falseType` on first use, always; tttt only when `node_reaches_alias_in_flight_deep()`
(`lower_branch()`). The prototype (`P29_BRANCH` in the old patch) gave suite +1 test but type-fest +2 (writable-deep
2 x TS2345, writable TS2589). Only do it if those two are understood; then `node_reaches_alias_in_flight()` can go
unless `prop_judgeable()`/`members_judgeable()` (checker.c) still need it. Validate with `./fasttest.sh --summary`,
all four corpora and `./findcrash.py`.

## Not this plan (checked)
- conditional-pick-deep (type-fest 6): plan 19 R20.
- `rd_2589_min.ts` types (`r : RD<J>` where tsc expands the union): plan 19's distributed-alias display item.
