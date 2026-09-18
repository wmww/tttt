# Todo
Sessions in order. Delete entries as they complete. See `vision.md` for why.

1. **Execution - type core stage 4a**: declarations/classes/interfaces/enums/functions evaluate to `Ty`. Read the
   "Stage 3 - DONE" notes in the plan first: enums as unions of member literals (with real member values), class
   members/`this`/static inheritance and a Declaration -> node link remove several `relate.c`/`members.c` hacks.
2. **Insights (short).** Three type core sessions in: is the scaffolding holding (stage 3 needed `LowerCtx.transient`
   instead of `TYPE_NEW`, and kept a private legacy relation for `infer`), are pass counts at or above baseline, is
   the design surviving contact? Re-plan the rest if not. Otherwise leave the list below as is.
3. **Execution - type core stage 4b**: expressions/statements/calls/index access evaluate to `Ty`.
4. **Execution - type core stage 5**: delete the old world, rename, docs, bench.
5. **Execution - harness fidelity.** Use `./failstats.py` (its header lists which options the 412 skipped multi-config tests vary; `target` and `module` dominate) to find which ignored `// @option`
   directives cost the most tests; implement option parsing properly (one `CompilerOptions` struct, not ad-hoc
   bools), including multi-value directives (`@strict: true,false` produce multiple baselines - check how
   `thirdparty/tsc/tests/baselines` names them). The 726 `undefined` tests are explained in `measurements.md`: 412 multi-config, 314 without types/symbols baselines.
   After parser round 2 this matters more: `@target`/`@lib` decide whether `Symbol`, `Promise`, `Generator`,
   `IterableIterator` exist, and spurious TS2304/TS2693 for them now show up in hundreds of async/generator/iterator
   tests (plus the target-gated diagnostics TS18028, TS2705, TS1375/1378 that tttt never emits).
6. **Planning - symbols + control flow analysis.** Investigate how persistent symbols and a bind-time flow graph
   fit the new type core (start from `Prop` and decl links in plan 02); write `plans/03-flow-analysis.md`. Pay attention to what tsc observably does
   (narrowing rules, reachability errors, definite assignment), and to parallel-bind friendliness.
7. **Triage.** Drain `triage.md`, prune `agents/`, refresh this file.

Then: insights.
