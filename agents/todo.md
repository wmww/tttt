# Todo
Sessions in order. Delete entries as they complete. See `vision.md` for why.

1. **Execution - type core stage 5**: delete the old world, rename, docs, bench (plan 02). 4b3 left functions as the
   last legacy-evaluated thing (`function_impl_evaluate_type` builds a legacy callable the new core lowers by node;
   `statement_evaluate_return_type` unions with `type_composite`): give functions a real `Ty` first, then
   `type.c`'s evaluators, `legacy_view.c`, `type_clone`/`type_drop` for semantic use and `LazyMemo.evaluated` as a legacy
   `Type` can go. Read "Stage 4b3 - DONE" in plan 02 and the "Expressions on the new core" section of `hacks.md`.
   Suite is 13165 tests (multi-config), see the top of `measurements.md` before comparing pass counts.
2. **Planning - symbols + control flow analysis.** Investigate how persistent symbols and a bind-time flow graph
   fit the new type core (start from `Prop` and decl links in plan 02); write `plans/03-flow-analysis.md`. Pay attention to what tsc observably does
   (narrowing rules, reachability errors, definite assignment), and to parallel-bind friendliness.
3. **Triage.** New since harness fidelity: JS files parsed as TS (~900 newly running `@allowJs` tests), target-gated
   diagnostics, harness-only directives. Drain `triage.md`, prune `agents/`, refresh this file.

Then: insights.
