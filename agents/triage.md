# Triage
Uninvestigated items: dump here what you don't want to deal with now. Triage sessions drain this into `bugs/*.md`,
plans, or fixes.

Last drained 2026-09-28 (triage session, todo entry 10 of the third window). Fixed: the zod root-config segfault
(deferred reference args in reverse mapped inference), object rest with computed keys, `export default;`, the
webpack-in-worktree numbers (a symlinked corpus path loads webpack twice; realworld.py uses real paths now), and
the override and variance annotation checks (a sub-agent; leftovers in `bugs/missing_checks.md`). Routed:
plan 27 leftovers (rest element grammar, `interface void`, tagged template parsing, TransportStream, reservedWords3),
plan 26 leftovers (JS `0other`), `bugs/type_core.md` (const type parameters with several candidates, plan 32's
anyBaseTypeIndexInfo), `bugs/display.md` (`shows_alias`, `undefined[]`), `bugs/missing_checks.md` (TS2507/TS2735),
`bugs/harness.md` (`port_item_add_alias`, lib-file log_errors), `bugs/perf_leads.md` (valibot +2.5%, the 89 s
duplicated webpack), hacks.md (`string_to_double`).

- **TS2769 overload signatures print differently depending on whether a `.d.ts` was checked first**
  (found by plan 36 stage 2): reactDefaultPropsInferenceSuccess passes, but with `// @skipLibCheck: true` added (or
  `baseline_tests --checkers 2`, where another checker checks react16.d.ts) tttt prints the candidate as
  `'new <P extends Props = BaseProps>(props: Readonly<P>): FieldFeedback<P>'` instead of tsc's instantiated
  `'(props: Readonly<Props>): FieldFeedback<Props>'`. Some memo that checking the lib file fills changes what the
  overload error reports: an evaluation-order dependence, and the only one K checkers show on the suite.
