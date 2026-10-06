# Plan 46: oracles for missing diagnostics

## Why
Every oracle we run at scale is clean under tsc: the corpora and the sweep can only show *spurious* diagnostics.
Missing checks are found by the suite (thin now: ~150 failing errors components) and by DefinitelyTyped's
`@ts-expect-error`s. A real user replacing tsc with tttt needs both directions to hold.

## Stage 1: flag flips — built and mostly drained (todo entry 1 of the tenth window, 2026-10-06)
`./flagflip.py` (docstring has the usage): the 12 corpora tsc checks with `-p` (not tsc-src / typescript-eslint, which
are `-b`: `tsc -b` takes no compiler options; not zod-root, which isn't clean) x 13 flag sets (`FLAGS` in the script),
tttt against tsc 5.8 by (file, line, code) multiset. tsc's output is cached gzipped in `test/flagflip/CORPUS/FLAG.json.gz`
(1.1 MB) keyed by the corpus pin: `./flagflip.py --tsc` regenerates stale or missing cells (~15 min on 4 cores for all
156; vscode ~90 s and 4 GB per cell). tttt's own diagnostics at default flags are subtracted (effect's 7). A missing
code with a spurious one on the same line is reported as `TSa->TSb`. A full run of tttt is ~80 s.

Gotcha found on the way: tsc stops before checking when the options are contradictory (TS5052, e.g.
exactOptionalPropertyTypes without strictNullChecks), so the non-strict cells also switch those options off. tttt
already stops there too (unless `--keep-going`, which flagflip must not pass).

### The matrix (tsc diagnostics; tttt missing / spurious / other code, before -> after this session)
| flag | tsc | before | after |
|---|---|---|---|
| nuia (`noUncheckedIndexedAccess`) | 18703 | 280 / 55 / 13 | 6 / 2 / 4 |
| eopt (`exactOptionalPropertyTypes`) | 7864 | 154 / 60 / 997 | 21 / 4 / 3 |
| sloppy (the strict family off) | 1831 | 34 / 612 / 6 | 32 / 28 / 5 |
| nosnc (`strictNullChecks` off) | 2758 | 127 / 89 / 13 | 36 / 27 / 5 |
| nosft (`strictFunctionTypes` off) | 121 | 5 / 633 / 0 | 5 / 3 / 0 |
| nonia, npafis, nio, nir, unused, nfcis, es5, es2015 | 16178 | 3 / 13 / 0 | 3 / 4 / 0 |
| **all 156 cells** | 47455 | **603 / 1460 / 1029** | **103 / 68 / 17** |

noUnusedLocals/Parameters (6045 tsc diagnostics), es5 (5079), es2015, noFallthroughCasesInSwitch, noImplicitOverride
and noPropertyAccessFromIndexSignature were already right: those checks are done.

### Landed this session (four sub-agents root-caused, prototypes merged; suite 13099 unchanged, sweep 8579 -> 8580)
- eopt: TS2412/TS2375/TS2379 head selection on apparent types, intersections and union sources; optional parameter
  properties are `T | undefined` not `T | missing`; missing kept through intersections, contextual types of optional
  properties and `T[K]` write constraints; failed calls keep raw type-argument defaults (nest's TS2352); TS2412 not
  for private names; `maybeTypeOfKind(Void)` through intersections; unused type parameters of overloads/@typedef.
- nuia: TS2339 after TS18048/TS2532 on the non-null rest of a union (211 alone); no discriminant narrowing through a
  union with null/undefined; union property from an own property plus other members' index signatures gets no
  `undefined`; nuia undefined after destructuring assignments and for non-array iterables; tsc's quick-type
  `checkNonNullExpression(callee)` for call initializers; object destructuring assignment from nullable unions; tuple
  elaboration skips rest positions; failed-call candidate for type predicates; super calls skip constraint checks;
  generic index unions defer; TS7053 with an error-typed index; TS2488 on a union once per program (`Error.once_key`).
- non-strict: `infer` in conditional types is contravariant for parameters even without strictFunctionTypes (tsc's
  `AlwaysStrict` priority: 1164 diagnostics, vscode's `h()` and effect/vue's `UnionToIntersection`); intersection
  inference cancels identical members whenever the target isn't only non-generic objects; `undefined & (A | B)` is
  undefined without strictNullChecks; apparent type re-applied after intersection reduction; `any.prop` not narrowed.
- nosnc: widening errors see the uninstantiated contextual signature (`KEY_CONTEXT_WIDENING`, a side key: hacks.md);
  class fields report widening like variables (TS7008/TS7018); `x!` on auto-typed lets; widening reports stop at the
  first union member; TS7024 for functions that are their own contextual signature; unknown is `{}` for element
  access without strictNullChecks; the comparable relation's readonly-array HACK narrowed; empty array literal is the
  widening `undefined[]` without strictNullChecks.
- Also: an unknown object in an element access is TS18046 whatever the index (it was skipped for generic keys).

### Open causes (ranked; the drain's queue). `./flagflip.py -k KEY` lists the instances
1. **TS2589 fidelity, valibot sloppy/nosnc (~60 diagnostics, ~30% of what's left): investigated 2026-10-06, judged
   plan-sized, not attempted.** tttt already counts like tsc: `ty_instantiate()` checks depth 100 / count 5M,
   `ty_enter_node()` resets the count where checkSourceElement/checkExpression do, and the limit hit gives the error
   type. What differs is how many instantiations happen. With a patched `_tsc.js` (log in instantiateTypeWithAlias at
   the limit) every one of tsc's valibot hits is the **count** limit (5,000,000) at depth 0-25, never depth 100.
   tsc makes ~25M instantiate calls in `required.test-d.ts` alone without strictNullChecks, and **none of them creates
   a new type**: all are cache hits that are counted anyway (with strict flags: 213k calls, 3k new types). tttt's
   worst node in the whole valibot run is ~140k. Where it happens: `toEqualTypeOf<Schema1>()`'s type-argument check
   (checkTypeArguments) relates expect-type's DeepBrand, whose mapped properties (getTypeOfMappedSymbol) hit nested
   conditional types that call isTypeAssignableTo, then RequiredEntries / InferEntriesOutput / anonymous object
   instantiations at depth ~20. The calls come from getObjectTypeInstantiation computing
   `map(typeParameters, t => getMappedType(t, combinedMapper))` *before* its cache lookup, and from tsc's composite and
   merged mappers instantiating on every getMappedType. Each lookup through a chain of composed mappers is counted
   again, so the count grows multiplicatively with nesting.
   To reproduce tsc we'd have to count the instantiations tsc would perform, which means mirroring its mapper
   representation (lazy composite/merged mappers, outer-type-parameter argument lists recomputed per instantiation)
   and its relation traversal order and caching (where tsc misses its caches, tttt doesn't). That's a big
   architectural change that only reproduces tsc's own blowup, and it costs performance. A "virtual count" (adding
   what tsc would have counted at tttt's cache hits) would need a model of tsc's mapper chains: also plan-sized, and
   fragile. Recommendation: accept this divergence and record valibot's sloppy/nosnc TS2589 cluster as a known flagflip
   difference. Only revisit if real projects show tsc's TS2589 silencing errors that tttt reports (that's the user-visible
   effect: TS2554/TS2344 where tsc gives one TS2589).
2. **Done (2026-10-06)**: union index signatures (`union_index_applies`, tsc's getUnionIndexInfos) back in `access()`'s
   union branch. The earlier valibot/xstate TS2344s came from `TEntries[symbol]` in a constraint: without an access
   node tsc's getPropertyTypeForIndexType falls back to the string index signature for a symbol key, which the union
   rule now does too (ACCESS_NO_NODE).
3. **Done (2026-10-06)**: `tuples_definitely_unrelated` gates tuple-to-tuple inference in `infer_from_objects` before
   the structure-matching shortcut, with tsc's minLength (required + variadic elements). The object half was already
   `types_definitely_unrelated`. The earlier ember breakage came from the old prototype's placement and min length.
4. **Overload/inference residue under nosft/sloppy**: vue `customElement.spec` TS2769 x10 (tsc infers
   `Promise.resolve`'s value from the return context as `Component`'s `(props: never, ctx) => RenderFunction` branch),
   vue `defineComponent.test-d.tsx:1615` TS2345, nest TS2345 x4 (fallback type after overload failure), TS7011 x4 in
   vscode (overloads whose first candidate gives no contextual signature; `mockObject<T>()({...})` with a `{}` default).
5. **Done (2026-10-06)**: xstate `State.ts:36`. An intersection source is related through tsc's
   getEffectiveConstraintOfIntersection (`effective_intersection_constraint`: one-step constraints, the members too
   against a union target, normalized) and not through base constraints; an intersection's inferable index looks at
   its members' apparent types (without strictNullChecks `T[K] & {}` is tsc's emptyObjectType there, which has none);
   a failing intersection source is retried normalized (`Partial<T>[K] & {}` is `T[K] & {}`). tsc normalizes every
   relation's intersection up front; doing that in `is_related_to` cost valibot 1.1% instructions, so it's only on failure.
6. **eopt residue (~25)**: webpack `SerializerMiddleware.js:82` TS2412 x4, `typeof import("fs")` vs
   `IntermediateFileSystem`, vscode `notebookEditorModelResolverServiceImpl`, `terminalInstance` 527-529,
   `extHostTestItem` 190-194, vue `apiAsyncComponent.ts:120-125` (TS2352, TS7006); xstate `input.test.ts:250` +1 (an
   intersection member tsc sees as identical to the target and tttt has two instantiations of).
7. **Union member order (not locally fixable)**: webpack `JavascriptModulesPlugin.js:2009` / `ConcatenatedModule.js`
   (key property chosen from the first member of estree's `Node`), vscode `urlFinder` (`RegExpMatchArray | []`).
8. Singletons: drizzle `mysql-proxy/migrator.ts:25-31` (tsc TS2532/TS2722, tttt TS2349), type-fest
   `IsLiteral<null>` (inferring `U` from `[boolean & infer U]`) and +TS2456, vue `Teleport.ts:562` -TS4114/TS4111,
   effect `Schema.ts:9044` -TS4114 (static `toString` over a base class of type any), vue `props.ts:18` +TS7030,
   webpack `deprecation.js:204` +TS6133 (`@template T` shadowed by an inner `<T = ...>`), vue `renderer.ts:2602`
   -TS2538 (auto-typed `let` assigned `p[v]` in a loop: error lost under the loop's fixed point), effect
   `context.ts:303` +TS2532 (generic variadic tuple element access stays deferred in tsc).

### Not done, and the decision
- The sweep with flips: tsc over DefinitelyTyped is ~2 h of CPU per flag set (`./sweep.py --tsc` is ~40 min wall on
  4 cores); DT packages set their own strict flags, so only nuia/eopt would be new. The corpora matrix found ~3000
  mismatches; when its queue is drained, one nuia sweep run is the cheap next widening (add a `--flags` to sweep.py
  with its own oracle file). Not before.

## Stage 2: mutation testing — built (todo entry 2 of the tenth window, 2026-10-06)
`./mutate.py` (docstring has the usage): 27 operators (24 regex ones plus delete-stmt, swap-stmts, delete-member) on
single-file suite tests that pass all three components; tsc baselines by one warm harness process per core (8000
mutants in ~70 s), cached in `/tmp/mutate/HASH` keyed by the mutants' contents and the tsc commit; `--recheck` reruns
only tttt on the last set. Mutants whose tsc baseline gains a TS1xxx code are separated (parser recovery, `--syntax`).
The report counts, per operator, the mutants that change tsc's codes at all ("productive": swap-stmts and
bool-literal-type rarely are, member-typo / never-const / add-arg / drop-type-arg almost always).

### The first run (seed 2, 8000 mutants of 4739 tests): 94 mismatches of 3744 productive mutants (2.5%), +53 syntax
Root causes, by count (`./mutate.py -k KEY` after a run of the same seed):
- **Crash** (2, fixed in the same session): `(function () {` or `(() => {` unterminated at EOF panicked in
  `expr_set_outer_span` (the parenthesized expression's whitespace trim walked into the inner expression).
- **TS2355 missing** (12): an accessor/function with a declared return type whose `return` was deleted.
- **Did-you-mean** (14 + 8 render): no TS2551 suggestion on the static side (`typeof C`, incl. `prototype`); TS2551
  where tsc says TS2339 for private members accessed from outside/subclass; TS2728 "declared here" related info added
  where tsc has none (TS2724 namespace members, object-literal types) or at the wrong column (TS2552 on a rest/binding
  parameter) or order (union member's chain).
- **`extends Object`** (~14): the TS2696 "'Object' type is assignable to very few" line placed where tsc doesn't or
  missing (TS2741->TS2696, elaboration TS2345/TS2416/TS2322); `T extends Object` assignable to the primitive union
  (missing TS2322); property access on classes/type parameters whose base became `Object` finds nothing to report
  (missing TS2339 x5); mixin bases with a non-constructor constraint TS2545 where tsc says TS2507 (+ its span).
- **Type text** (~12): TS2678 prints the case literal (`"1"`) where tsc widens (`string`) (5); TS2542 on `"0"` (tsc
  `String`), TS7053 enum member (`MyEmusEnum.emu`), `number & Tag2`, `never[keyof T]`, alias `Kind | undefined`,
  expando `extra: never`; elaboration: return type `X` vs `Y`, `Function` vs the type, a "no properties in common"
  line, a missing union step.
- **Computed property names** (3): TS2464 spurious for a void function call, missing for an `undefined`-initialised let.
- Singletons: TS2345 `any` to `never` in exhaustive switches (2), TS2304 on a decorator whose declaration was deleted
  (2, tsc says nothing), TS1162 on an optional member in an object literal (2), TS2349 vs TS2722 for a `void` callee,
  TS2310/TS2506 base recursion through a call, TS2484 vs TS2304, for-of destructuring TS2339, expando TS7053,
  TS7015 vs TS7053 on a tuple union, variadic tuple TS2322, a spurious TS2314/TS2347 after `extends Object`.

