# Plan 46: oracles for missing diagnostics

## Why
Every oracle we run at scale is clean under tsc: the corpora and the sweep can only show *spurious* diagnostics.
Missing checks are found by the suite (thin now: ~150 failing errors components) and by DefinitelyTyped's
`@ts-expect-error`s. A real user replacing tsc with tttt needs both directions to hold.

## Stage 1: flag flips — built and mostly drained (todo entry 1 of the tenth window, 2026-10-06)
`scripts/flagflip.py` (docstring has the usage): the 12 corpora tsc checks with `-p` (not tsc-src / typescript-eslint, which
are `-b`: `tsc -b` takes no compiler options; not zod-root, which isn't clean) x 13 flag sets (`FLAGS` in the script),
tttt against tsc 5.8 by (file, line, code) multiset. tsc's output is cached gzipped in `test/flagflip/CORPUS/FLAG.json.gz`
(1.1 MB) keyed by the corpus pin: `scripts/flagflip.py --tsc` regenerates stale or missing cells (~15 min on 4 cores for all
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

### Drain of 2026-10-06 (agent E, flagflip 67/57/11 -> 51/49/11)
Done, probes in `test/probes/plan46/`: noUncheckedIndexedAccess without strictNullChecks (an index signature's `never`
is undefined, an undefined callee is TS2722: drizzle proxy migrators); the comparable relation relates signature
returns source to target only and keeps strict parameter variance (inherited class constructors stay bivariant:
vscode extHostTestItem 191/192); TS7008 for a widening field initializer also when the constructor assigns it
(vscode outlineModel 265); JS this-properties judge constructor-only types unwidened and leave `F.prototype.x` out
(webpack FileSystemInfo 1319). Also from plan 19's effect pass: vue customElement.spec TS2769 x10 (item 4) is gone.
Looked at, not fixed:
- effect LayerMap 402 (sloppy/nosnc +TS2345): `make(options.lookup, options)` with options a NoExcessProperties
  intersection: tsc infers `PreloadKeys` (the call is `any` in tsc at every flag, `undefined` in tttt; probe shape in
  /tmp only: `make<K, P extends Iterable<K> | undefined = undefined>(lookup, options?: { preloadKeys?: P })` with
  `"lookup" in options` narrowing a union of two NoExcess<..., Options>). Where tsc's any comes from is unknown.
- valibot forwardAsync.ts 55 (nosnc -TS2578): tttt types `pathInput` (`let pathInput: unknown = dataset.value` after
  `dataset = await action['~run'](...)`) so that `pathInput[key]` is TS7053 on `{}`; the sync forward.ts is fine, so
  the awaited dataset's `value` differs (any in tsc?) without strictNullChecks.
- excalidraw bounds.ts 821 vs 883 (nuia TS2488): tsc caches a union's noIterationTypes also from a lookup that
  reports nothing, so its first report is elsewhere; order artefact.

### Open causes (ranked; the drain's queue). `scripts/flagflip.py -k KEY` lists the instances
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
4. **Overload/inference residue under nosft/sloppy (partly done 2026-10-06)**. Done: nest TS2345 x4 (a default saw an
   earlier inference that failed its constraint, `inference_mapper()`), vscode `debugModel.test.ts` TS7011 x2 (first
   check without a contextual signature because of TP's `{}` default, KEY_CONTEXT_WIDENING_ABSENT). Left: vscode
   TS7011 x2 through `instantiationService.stub()`'s overloads (`stub(INotificationService, { prompt: () => null! })`,
   the first candidate gives no contextual signature); vue `customElement.spec` TS2769 x10 (tsc infers
   `Promise.resolve`'s value from the return context as `Component`'s `(props: never, ctx) => RenderFunction` branch);
   vue `defineComponent.test-d.tsx:1615` TS2345.
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

### The sweep with flips (todo entry 6 of the tenth window, 2026-10-06)
`scripts/sweep.py --flags nuia|eopt` rechecks every DefinitelyTyped package with the flag on top of its tsconfig, against
its own tsc oracle (`test/sweep/DefinitelyTyped.FLAG.tsc.jsonl.gz`, ~40 min of tsc on 4 cores each; DT packages set
their own strict flags, so only these two are new). nuia: tsc reports in ~1800 more packages than at default flags;
8585 -> **8588 / 8595** match after three fixes (an any index into a nullable union, the quick-type callee check on
annotated declarations, tsc's createUnionOfSignaturesForOverloadFailure for the arguments). eopt: **8588** at the
first run. The mismatches left are the default run's own plus react-relay under nuia (triage.md). Both flags are
done at breadth; rerun after big changes to optional/undefined handling.

## Stage 2: mutation testing — built (todo entry 2 of the tenth window, 2026-10-06)
`scripts/mutate.py` (docstring has the usage): 27 operators (24 regex ones plus delete-stmt, swap-stmts, delete-member) on
single-file suite tests that pass all three components; tsc baselines by one warm harness process per core (8000
mutants in ~70 s), cached in `/tmp/mutate/HASH` keyed by the mutants' contents and the tsc commit; `--recheck` reruns
only tttt on the last set. Mutants whose tsc baseline gains a TS1xxx code are separated (parser recovery, `--syntax`).
The report counts, per operator, the mutants that change tsc's codes at all ("productive": swap-stmts and
bool-literal-type rarely are, member-typo / never-const / add-arg / drop-type-arg almost always).

### Drained the same session (five Opus sub-agents, ~70 commits)
Seed 2: 94 -> **6** mismatches. Seeds 3, 4, 5 (8000 each, `MUTATE_DIR=... scripts/mutate.py -n 8000 --seed N` reproduces):
67 -> 29, 50 -> 31, 43 -> 40 (seed 5 only got 30 minutes). Yield per fresh seed is ~1.2% of productive mutants and
falling; what's left is mostly one-mutant causes of type text / elaboration (union order, reduced annotations as
written, `as const` display), tsc evaluation-order artifacts (loop passes reporting twice, class-expression checks
inside base resolution) and parser recovery. Their notes are in triage.md (prefixed with the agent / seed). Real
gaps found and fixed include: template literal type placeholders never checked (TS2322), bare `return;` in async
functions/generators never checked, unions with a type-parameter member silently skipped in property access (any
missing property went unreported), mapped types over member-less type parameters likewise, decorators on
undecoratable nodes checked, TS2794 missing entirely, `&&=`/`||=`/`??=` checking the whole expression, a parser
crash on an unterminated `(function () {` and another on `else }` at a block end.

Next use: rerun a fresh seed after big checker changes (it's a cheap regression net for *missing* checks: ~3 min
including tsc); the per-seed yield no longer justifies a fan-out on its own. Widening idea not done: corpus files
(needs per-project tsc runs; flagflip's machinery could host it).

### Widened to multi-file and JS tests (todo entry 6 of the tenth window, 2026-10-06)
`scripts/mutate.py --tests multi,js` mutates the passing `@filename` (1436) and `@allowJs` (750) tests; diagnostics compare
by (file, line, code). Yield ~6.5% of productive mutants (seed 1: 92 of 1429, seed 2: 177 of 2371), against ~1% for
fresh single-file seeds: JS/JSDoc semantics, module resolution and the harness are less drilled than single-file TS.
Seed 2 found a hang (a function whose `@type` names itself). Drained by three sub-agents (modules / JS / text, 42
commits): seed 1 92 -> 12, seed 2 177 -> 40; a fresh seed 3 then had 46 of 2325 (2.0%), so the fixes generalised.
What's left is in triage.md (mostly tsc binder/resolution quirks of broken CommonJS, and printing).

### Fresh seeds after the suite-errors window (agent B, 2026-10-06)
`MUTATE_DIR=... scripts/mutate.py -n 5000 --seed 7`: 21 mismatches of 2323 productive mutants (0.9%, the single-file yield
keeps falling) -> 12. `--tests multi,js -n 3000 --seed 5`: 33 of 1407 (2.3%) -> 27. Fixed (8 commits): a returned
anonymous class expression's error span (`class`), TS1141 for a non-string module specifier only in the right context
(it also dropped the file's grammar errors as a parse error), `extends` a union without common construct signatures
(TS2507 on the whole type, error base), `new` of a union with only common call signatures (TS7009), a union's readonly
index signature on writes (TS2542), `T["1"]` on tuples as `T[1]`, never indexed by a non-key (error type), substitution
sources named as their intersection in heads, optional-vs-required (TS2327) only against class members, an export
specifier conflicting with its own export being its own alias target (TS2693), JS `@satisfies` ignored under an
annotation, TS2303 through a failed `export =` name's spelling suggestion. Left (one mutant each): type text of spread
properties and `typeof ns.K` in error messages (needs node reuse in error display, plan 34), elaboration of a
substitution intersection (tsc names its constraint), TS2574 through an error-typed conditional, TS2564 for a
`[k]` field whose key is `never`-annotated (tsc's tryGetNameFromEntityNameExpression falls back to the initializer),
JS `new C0()` arity against a .d.ts generic constructor, the JS-lane TS2303, CommonJS export quirks.

### The first run (seed 2, 8000 mutants of 4739 tests): 94 mismatches of 3744 productive mutants (2.5%), +53 syntax
Root causes, by count (`scripts/mutate.py -k KEY` after a run of the same seed):
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

