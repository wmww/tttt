# Todo
Sessions in order. Delete entries as they complete. See `vision.md` for why.

1. **Planning - type core redesign.** `plans/01-type-core-investigation.md` (that file is a brief; the
   deliverable is the real plan, `plans/02-type-core.md`, staged into session-sized steps, and new todo entries
   for those stages inserted here). New evidence from parser round 2 (see `measurements.md` takeaways and the
   `type_extends_internal` entry in `hacks.md`): recursive generic classes made the suite eat 38 GB until the
   relation depth limit was lowered; `varianceProblingAndZeroOrderIndexSignatureRelationsAlign` is the repro and
   should be a validation case for the plan (tsc checks it instantly).
2. **Execution - type core stage 1** (per `plans/02-type-core.md`).
3. **Execution - type core stage 2.** Continue until the plan's stages are done or the next insights session
   re-evaluates; if the stages number more than ~4, do the first 4 then fall through.
4. **Execution - harness fidelity.** Use `./failstats.py` (its header lists which options the 412 skipped multi-config tests vary; `target` and `module` dominate) to find which ignored `// @option`
   directives cost the most tests; implement option parsing properly (one `CompilerOptions` struct, not ad-hoc
   bools), including multi-value directives (`@strict: true,false` produce multiple baselines - check how
   `thirdparty/tsc/tests/baselines` names them). The 726 `undefined` tests are explained in `measurements.md`: 412 multi-config, 314 without types/symbols baselines.
   After parser round 2 this matters more: `@target`/`@lib` decide whether `Symbol`, `Promise`, `Generator`,
   `IterableIterator` exist, and spurious TS2304/TS2693 for them now show up in hundreds of async/generator/iterator
   tests (plus the target-gated diagnostics TS18028, TS2705, TS1375/1378 that tttt never emits).
5. **Planning - symbols + control flow analysis.** Investigate how persistent symbols and a bind-time flow graph
   fit the (new) type core; write `plans/03-flow-analysis.md`. Pay attention to what tsc observably does
   (narrowing rules, reachability errors, definite assignment), and to parallel-bind friendliness.
6. **Triage.** Drain `triage.md`, prune `agents/`, refresh this file.

Then: insights.
