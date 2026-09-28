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
- **Thread startup per program** (plan 36 stage 3): `baseline_tests --checkers 4 --threads 4` takes 38 s against
  24 s at `--threads 1` (same output): ~1 ms per program for starting and joining 3 threads (8 MB stacks, glibc arenas;
  most suite tests have one file, so there is nothing to overlap). Irrelevant for one CLI run; a persistent pool
  (which stage 1's load pool would be) fixes it if a watch/LSP mode or a threaded harness ever wants it. Also, with
  fewer checked files than checkers, the empty checkers still build their global types (~0.2 ms each).
- **`Array<T>` annotation display depends on when the global Array type was first built** (plan 37 D side finding):
  creating Array early passes genericFunctionsWithOptionalParameters1/2, jsdocTypeGenericInstantiationAttempt,
  unusedLocalsAndParametersTypeAliases (`Array<T>` vs `T[]`). An order dependence: find what the printer keys on.
- **Type-predicate narrowing should return the candidate unchanged** (tsc's getNarrowedType; plan 37 B finding):
  tttt prints `A | B` where tsc has `Both` (flowControlTypeGuardThenSwitch, partiallyDiscriminantedUnions,
  stringLiteralCheckedInIf02).
- `test/probes/plan37/return_only.ts` (plan 37 F): `k(() => () => ({ a: true, b: x => x }))` with `k<T>(g: () => () =>
  { a: T, b: (x: T) => void }): T` types both arrows as returning `{ a: true; ... }` where tsc widens their return
  types to `{ a: boolean; ... }` (the object literal itself is `a: true` in both): the inner arrow's inferred return
  type isn't widened in the return-only/final check the way tsc's getReturnTypeFromBody does.
- Corpus session 2026-09-28 leftovers (display, from probes in `test/probes/plan19/webpack/`): a `require("m").X`
  alias that resolves to nothing prints `error` in tsc (tttt `any`), tsc lists a same-named `@typedef` as a second
  declaration of the alias symbol, and `module.exports = f` of a function declaration prints `typeof f`
  (require_property_value_only.ts); a JS `const S = Symbol()` read through a `@type {typeof S}` const prints `symbol`
  in tsc (jsdoc_accessor_type.ts). Also: `declare const B = -1n` under a pre-ES2020 target lacks TS2737.
- Suite checker bugs 5 (flow bucket) found the same bug twice: code that reads signatures/members only when
  `apparent->flags & TYF_OBJECT`, skipping intersections whose `ty_members()` are well defined
  (`constructor_instance_type` in flow.c, `call_signatures_of` in call.c). Grep for other `TYF_OBJECT ? ty_members`
  gates and check whether tsc's counterpart (getSignaturesOfType etc.) accepts intersections there.
