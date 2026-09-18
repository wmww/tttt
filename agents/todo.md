# Todo
Sessions in order. Delete entries as they complete. See `vision.md` for why.

1. **Execution - parser gaps.** Inserted by the measurement session: `measurements.md` shows ~4.5k tests (40% of
   the suite) fail because the parser rejects valid syntax, which also disables typechecking for them. Work down
   `./failstats.py --syntax` from the top (`module X {}`, `declare module "x"`, `import x = require()`, computed
   method names, labels, `do..while`, `function*`, spread args, decorators, `#private`; also don't parse non-TS
   virtual files such as `.json` as TypeScript). Parse + AST + name resolution only; where the checker can't handle a
   new node yet, degrade quietly rather than building checker features on the old type core. Independent of the type
   core work, so it can run before or between those sessions. Re-run failstats and update `measurements.md` after.
2. **Planning - type core redesign.** `plans/01-type-core-investigation.md` (that file is a brief; the
   deliverable is the real plan, `plans/02-type-core.md`, staged into session-sized steps, and new todo entries
   for those stages inserted here).
3. **Execution - type core stage 1** (per `plans/02-type-core.md`).
4. **Execution - type core stage 2.** Continue until the plan's stages are done or the next insights session
   re-evaluates; if the stages number more than ~4, do the first 4 then fall through.
5. **Execution - harness fidelity.** Use `./failstats.py` (its header lists which options the 412 skipped multi-config tests vary; `target` and `module` dominate) to find which ignored `// @option`
   directives cost the most tests; implement option parsing properly (one `CompilerOptions` struct, not ad-hoc
   bools), including multi-value directives (`@strict: true,false` produce multiple baselines - check how
   `thirdparty/tsc/tests/baselines` names them). The 726 `undefined` tests are explained in `measurements.md`: 412 multi-config, 314 without types/symbols baselines.
6. **Planning - symbols + control flow analysis.** Investigate how persistent symbols and a bind-time flow graph
   fit the (new) type core; write `plans/03-flow-analysis.md`. Pay attention to what tsc observably does
   (narrowing rules, reachability errors, definite assignment), and to parallel-bind friendliness.
7. **Triage.** Drain `triage.md`, prune `agents/`, refresh this file.

Then: insights.
