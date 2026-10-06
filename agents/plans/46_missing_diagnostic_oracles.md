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

## Stage 2: mutation testing — worth a tool (decided 2026-10-06)
Prototype in `46_prototypes/` (README has the commands): 18 regex mutation operators on single-file suite tests that
pass all three components, tsc baselines produced in one warm harness run (400 mutants in ~1.5 min), tttt through
`fasttest.sh --dir`. Yield: 150 mutants gave 7 errors-component mismatches, 400 gave 12: ~3-5% of mutants, and about
half are distinct real bugs, e.g. missing TS2377 when `super()` becomes `superX()` (computedPropertyNames28_ES6), the
TS2728 related position on mixin members (mixinClassesAnonymous `.args` -> `.argsX`), TS2339 where tsc says TS2551
"did you mean 'prototype'", a spurious TS2403 after `return;` (arrowFunctionExpressions), missing TS7053
(mappedTypeGenericIndexedAccess with `extends Object`), the TS2420 elaboration line "The 'Object' type is assignable to
very few other types", inference candidate order (iterableArrayPattern26 with `"0"`), type-argument display
(nonPrimitiveAndTypeVariables). The other half are parser recovery (an argument replaced by `null` in a parameter
list) and duplicates.

The tool (`./mutate.py`, the execution session builds it from the prototype):
- Mutants from the passing suite tests *and* corpus files would be better, but corpora need whole-project tsc runs:
  start with suite tests (fast, single-file, harness options already right).
- Deterministic seeds; a run of N mutants caches tsc's baselines under /tmp keyed by (seed, N, tsc commit) so reruns
  only rerun tttt; operators tagged in the name; a summary that buckets failures by operator and by the first
  differing diagnostic (code, missing/spurious/position/text), like failstats.
- Drop or separate the operators that produce syntax garbage (null-arg into parameter lists) into a parser-recovery
  bucket. Add operators: delete a statement, swap two statements, remove a type argument, change `extends` targets
  (already there), drop an `override`/`readonly`/`abstract`, change a literal type, delete an `implements` member.
- Validation: the tool in CLAUDE.md's list; one run of ~2000 mutants triaged into root causes written into this
  plan; the top causes fixed.
