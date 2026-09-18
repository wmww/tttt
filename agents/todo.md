# Todo
Sessions in order. Delete entries as they complete. See `vision.md` for why.

1. **Execution - measurement tooling.** `plans/00-measurement-tooling.md`.
2. **Planning - type core redesign.** `plans/01-type-core-investigation.md` (that file is a brief; the
   deliverable is the real plan, `plans/02-type-core.md`, staged into session-sized steps, and new todo entries
   for those stages inserted here).
3. **Execution - type core stage 1** (per `plans/02-type-core.md`).
4. **Execution - type core stage 2.** Continue until the plan's stages are done or the next insights session
   re-evaluates; if the stages number more than ~4, do the first 4 then fall through.
5. **Execution - harness fidelity.** Use the aggregation tool from (1) to find which ignored `// @option`
   directives cost the most tests; implement option parsing properly (one `CompilerOptions` struct, not ad-hoc
   bools), including multi-value directives (`@strict: true,false` produce multiple baselines - check how
   `thirdparty/tsc/tests/baselines` names them). Also find out why 726 tests are `undefined`.
6. **Planning - symbols + control flow analysis.** Investigate how persistent symbols and a bind-time flow graph
   fit the (new) type core; write `plans/03-flow-analysis.md`. Pay attention to what tsc observably does
   (narrowing rules, reachability errors, definite assignment), and to parallel-bind friendliness.
7. **Triage.** Drain `triage.md`, prune `agents/`, refresh this file.

Then: insights.
