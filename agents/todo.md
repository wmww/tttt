# Todo
Sessions in order. Delete entries as they complete. See `vision.md` for why.

1. **Execution - type core stage 4b3**: expressions/statements/bindings/functions/namespaces evaluate to `Ty`, shaped
   for contextual typing, literal freshness and a single identifier-reference hook (plan 02). Compiler options are
   `compiler_options()` (`src/compiler_options.h`, all of tsc's boolean/enum options, defaults resolved); the suite is
   now 13165 tests (multi-config), see the top of `measurements.md` before comparing pass counts.
   **Must cover explicit type arguments on calls/`new`**: `new Map<string, number>()` reports TS2558 + TS2351 today,
   because `f<A>(x)` is a legacy `GenericParamsExpr` around the callee that only knows legacy generics
   (`generic_params_expr_*` in `expr.c`), and `ty/call.c` has no path for explicit type arguments (tsc: filter
   candidates by type argument count, TS2558 only if none fits, mapper from the arguments instead of inference). Every
   test that constructs a `Map`/`Set`/`Promise` with type arguments hits this now that the ES2015+ libs load. Read "Stage 4b1 - DONE" and "Stage 4b2 - DONE" first. 4b1 left hooks for the context-sensitive inference pass and one mitigation to remove (plan 02).
2. **Execution - type core stage 5**: delete the old world, rename, docs, bench.
3. **Planning - symbols + control flow analysis.** Investigate how persistent symbols and a bind-time flow graph
   fit the new type core (start from `Prop` and decl links in plan 02); write `plans/03-flow-analysis.md`. Pay attention to what tsc observably does
   (narrowing rules, reachability errors, definite assignment), and to parallel-bind friendliness.
4. **Triage.** New since harness fidelity: JS files parsed as TS (~900 newly running `@allowJs` tests), target-gated
   diagnostics, harness-only directives. Drain `triage.md`, prune `agents/`, refresh this file.

Then: insights.
