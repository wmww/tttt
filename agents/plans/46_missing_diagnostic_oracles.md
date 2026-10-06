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
1. **TS2589 fidelity, valibot sloppy/nosnc (~60 diagnostics, ~30% of what's left)**: without strictNullChecks
   `required(wrapped, undefined)` infers `TMessage = any`; tsc then hits the instantiation depth limit inside
   expect-type's comparison and its cached error type silences later assertions; tttt computes on and reports
   TS2554/TS2344. Not a null-handling bug: tsc's instantiation depth/count accounting (`instantiationDepth === 100 ||
   instantiationCount >= 5000000`) per statement. Probably plan-sized; check what tttt counts first.
2. **Union index signatures (nuia cause F, 6 webpack, taken out again)**: tsc's `getUnionIndexInfos` keeps only the
   first member's index infos whose key every member has, so `C[] | Record<string, C>` has none (`cs[0]` is TS7053).
   The prototype (`union_index_applies` in `access()`'s union branch) made 4 TS2344 on valibot (`TEntries[TKey]`
   against its constraint, `types/object.ts:141`) and 1 on xstate (`setup.ts:276`) at default flags once combined
   with the eopt changes: the constraint computation must not use it, or it must be applied where tsc applies it
   (property access of a union, not the indexed-access type).
3. **`typesDefinitelyUnrelated` in inference (non-strict cause 4, 4 webpack, taken out again)**: tsc skips inferring
   from members when source and target are definitely unrelated (tuples of incompatible arity, or object types each
   with a property the other lacks: `inferFromObjectTypes`). A tuple-only port inside `infer_to_tuple` broke
   types/ember (computed properties became unknown). Port the whole gate where tsc has it. Repro: `type AsArray<T> =
   T extends any[] ? T : [T]; declare class H<T> { m: (...a: AsArray<T>) => void }` and `const c: { m: (a: P1, b: O1)
   => void } = make();` with strictFunctionTypes off infers `H<P1 | [P1, O1]>` (tsc `H<[P1, O1]>`).
4. **Overload/inference residue under nosft/sloppy**: vue `customElement.spec` TS2769 x10 (tsc infers
   `Promise.resolve`'s value from the return context as `Component`'s `(props: never, ctx) => RenderFunction` branch),
   vue `defineComponent.test-d.tsx:1615` TS2345, nest TS2345 x4 (fallback type after overload failure), TS7011 x4 in
   vscode (overloads whose first candidate gives no contextual signature; `mockObject<T>()({...})` with a `{}` default).
5. **xstate `State.ts:36` TS2344 x8 (sloppy/nosnc)**: `type T<V extends SV> = V extends string ? V : {[K in keyof
   V]?: T<NonNullable<V[K]>>}` with `SV = string | {[k: string]: SV | undefined}`: tsc reports the constraint, tttt
   doesn't.
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

