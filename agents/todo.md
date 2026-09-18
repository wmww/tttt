# Todo
Sessions in order. Delete entries as they complete. See `vision.md` for why.

1. **Insights.** The type core migration (8 sessions) just finished; `vision.md` asked for an audit at this point: is
   `hacks.md` honest and pruned, is the "AST-side walk" section there acceptable as the starting point for plan 03, did
   the migration drift from the goal? Then re-plan. Inputs: `measurements.md` (parse + name resolution are now 4x the
   typecheck phase; TS2322/TS2339 lead the missing diagnostics, JS files and module resolution the spurious ones).
2. **Planning - symbols + control flow analysis.** Investigate how persistent symbols and a bind-time flow graph
   fit the type core (start from `Prop`, `Declaration.nodes`, and the bind-time link nodes described in `hacks.md`
   "The AST-side walk and what's left of the old type world"); write `plans/03-flow-analysis.md`. Pay attention to
   what tsc observably does (narrowing rules, reachability errors, definite assignment), and to parallel-bind
   friendliness. `ty_identifier_reference_type()` is the hook narrowing goes in.
3. **Triage.** New since harness fidelity: JS files parsed as TS (~900 `@allowJs` tests), target-gated diagnostics,
   harness-only directives. Drain `triage.md`, prune `agents/`, refresh this file.

Then: insights.
