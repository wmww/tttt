# Missing and wrong checks (the suite's long tail)
The work queue for "suite checker bugs" sessions: diagnostics tttt doesn't report, or reports under another code, and
the failing tests behind them. Find fresh buckets with `./failstats.py` (single-code tests: errors fail on one code
alone in `build/failstats/tests.tsv`) and `./unimplemented_codes.sh` (codes `src/` never mentions). Every test named
here was failing on 2026-10-06 (suite 13299; harness-only causes are in `harness.md`, type-core causes in
`type_core.md`).

## Buckets (2026-10-07, suite 13303)
Errors component: 85 failing at 13299 (127 at the start of the 2026-10-06 suite-tail fan-out). What's left, by
mechanism (re-checked by the 2026-10-07 triage): text only (conditionalTypes1, tsxUnionElementType3/4,
genericCallWithGenericSignatureArguments3: plan 34); circularity / depth (circularBaseTypes, circularConstraintYields
AppropriateError, recursiveMappedTypes, recursivelyExpandingUnionNoStackoverflow, limitDeepInstantiations,
recursiveConditionalCrash4, circularIndexedAccessErrors, infiniteConstraints, the "NoCrash" ones: plan 39 and the
Circularity section below); limits (TS2563, TS2590, TS2859, TS2799's position); inference
(contravariantOnlyInferenceFromAnnotatedFunction, inferFromGenericFunctionReturnTypes3); JS (plan 26), declaration
emit (plan 40), `@typescript/lib-*` resolution (libTypeScriptOverride*/SubfileResolving*), noLib tests, parser
recovery (parser0_004152, jsxInvalidEsprimaTestSuite, varianceAnnotationsWithCircularlyReferencesError,
thisTypeInFunctionsNegative's `...this`). Single-code tests: missing TS2589 4, missing TS1005 3, missing TS2344 2,
spurious TS2321 2, missing/misplaced TS2304 2+2, then single tests.

## Known missing checks
- TS4109/TS4110 (type arguments circularly referencing themselves: circularlyReferentialInterfaceAccessNoCrash,
  circularTypeArgumentsLocalAndOuterNoCrash1).
- `interface I<C> { a: StateNode<C, any> }` where StateNode requires `C extends MachineContext`: tsc's TS2344 on the
  member's type argument is missing (xstate).
- Missing TS2353 for `const x: Trans<E> = { NEXT: ... }` in a generic function with `Trans<E> = { [K in E['type'] |
  '*']?: ... }` (xstate; excess property checks against a generic mapped type).
- TS2712 (a dynamic `import()` in ES5 without a global `Promise` value, with TS2468): tsc reports it unconditionally in
  checkImportCallExpression (createPromiseReturnType).
- TS2344 on type references with generic arguments: one missing each in instantiationExpressions:163 and
  typeGuardFunctionErrors:152 (`U`, `T | "d"`): the HACK in `ty_check_type_reference_constraints` skips them.
- `declare const B = -1n` under a pre-ES2020 target lacks TS2737.
- TS2507 "Type 'X' is not a constructor function type" for an `extends` expression (only `new` reports it), and its
  related TS2735 for a type parameter ("Did you mean for 'T' to be constrained to type 'new (...args: any[]) =>
  ...'?"). Plan 32 leftover.
- **JS**: TS2565 for a property declared by a JS expression statement (`/** @type {F} */ C.prototype.m;`) read in the
  same flow container (tsc's getFlowTypeOfAccessExpression with assumeUninitialized); TS7032 for a setter whose type
  comes from `@type` (probe plan19/webpack/jsdoc_accessor_type.ts).
- **Decorators**: TS2449 for a class used in the decorators of its own methods' parameters (tsc counts them; tttt's
  check only knows class and member decorators).
- **Classes**: TS2386 across merged interface declarations (interface.c checks per body); TS2687 skips statics and
  method/accessor pairs (hacks.md).
- **Accessibility**: `this.x = v` in the constructor of a class merged with an interface declaring `readonly x` (tttt
  exempts any property whose parent is the class); `globalThis.globalThis = x` (TS2540). Private names: TS18015,
  duplicates across kinds (TS2804/TS18010/TS2300/TS2717: privateNameDuplicateField).
- **Declarations**: `export function g() {}` + `function g() {}` is TS2395 here, TS2393 in tsc. TS2300 for a second
  `export =`.
- **Operators**: `void` operands/objects (UndefinedFacts in tsc: `v.x` with `v: void` is TS18048) aren't checked like
  undefined. `for (x in o)` with a non-declaration target: TS2322 here, TS2405 in tsc.
- **Type predicates**: TS1228 (predicate in a constructor type, accessor, parameter type), TS2775/TS2776 (assertion
  callee without explicit types: assertionTypePredicates1), a predicate function returning non-boolean.
- **Implicit any**: widening errors past `report_errors_from_widening` (binding.c; plan 37 A stage 2: TS7010/7011/7025
  on widened inferred returns, TS7008 class fields, TS7018 through a spread) and of generator yield/next types; TS7017
  (`typeof globalThis` has no index signature).
- **Target-gated**: TS2705 comes only from an inferred return type's demand (tsc's createPromiseReturnType), and tttt
  infers more eagerly than tsc, so the two can disagree; TS1056 (accessors below ES5); `await` in a namespace body
  counts as top level (tsc: TS1308).
- **TS2538 with `any`**: `any[[]]` (a tuple as an index type on `any`): tsc's getPropertyTypeForIndexType reports past
  the `any` object shortcut.
- **Iteration**: TS2489/TS2490 (iterator without `next`/`value`), TS2763-2765 (iterator next-type mismatches);
  generator next types aren't inferred from `yield` uses (tsc's `nextTypes`).
- **Modules**: exports in non-module containers dropped silently (`scope_add_port()`, TODO TS1194); `import { type
  require }` isn't type-only for TS2441; duplicatePackage_globalMerge prints TS2306 paths as `/node_modules/...` (tsc `/tests/node_modules/...`); a nested
  `declare module "m"` gets TS2435 but is still bound as a global ambient module (tsc: a local namespace).
- **TS2303**: pushTypeGetTypeOfAlias (`exports.blah = exports.someProp`, see JavaScript below; the TS2303 there is
  getTypeOfAlias's type circularity through the module's type, not alias resolution).
- **Templates**: `${symbol}` not rejected, TS2590 at the type node (templateLiteralTypes1/3; reporting it from
  `intersect_unions()`/`ty_template_literal()` at the current node put it at the wrong node and fired where tsc
  doesn't).
- **Misc**: TS2353 for an object literal assigned to an assignment pattern (`({ a } = { a, b })`); TS2561/TS2820 want
  the spelling suggestion (`spelling_suggestion()` exists); TS2551 suggestions don't filter inaccessible members;
  TS2793 only for function declarations, not overloaded methods/constructors (`add_implementation_success`); TS2563
  (flow depth 2000 silently gives the error type: largeControlFlowGraph); TS2406 (`for (#x in o)` says TS1451).
- **Bigint property names**: TS1539 and `{ 1n: x }` as the property "1n" (TS2538 for bigint keys): bigintPropertyName,
  which also has a parse failure.
- doYouNeedToChangeYourTargetLibraryES2016Plus: spurious TS1503 (regex flags checked against the target where tsc
  doesn't there), a few TS2550 missing.
- **Regular expression patterns** (TS1487, TS1508, TS1535, TS1530, TS1517, TS1518, TS1507...): tsc's
  scanRegularExpressionWorker, ~1000 lines; only the flags are checked (`regex_check_flags`). Plan 27 stage 3.
- **Variance annotations**: varianceAnnotationsWithCircularlyReferencesError (tsc's recovery for `type T1<in in>`).
- duplicatePackage: the default-exported class prints `X`, not `default`, in `import("...").default` (display:
  tsc's getNameOfSymbolAsWritten for a `default` symbol in a qualified import type).
- nodeModulesJson (node16/node18/nodenext): symbols and types of JSON imports.
- The jsxImportSource runtime import (`jsx_runtime_module()`) reports TS2875/TS2792 and TS1479/TS1471, not the other
  resolveExternalModule diagnostics (TS7016 for a JavaScript runtime).
- declarationEmitBindingPatternsUnused: an interface setter `set x({ name: alias }: Named & { o: typeof alias })`
  keeps only its parameter's type (interface_member.h: `setter_param`), so its binding pattern gets no TS2842 and
  `typeof alias` in the parameter type is TS2304. Needs the setter's parameter as a parameter.
- `while (a) const x = 1;` still scopes x to the loop (tsc: the enclosing block).

## Mutation seed 2 leftovers (2026-10-07, `MUTATE_DIR=... ./mutate.py -n 8000 --seed 2`: 32 -> 13)
Known elsewhere: spreadUnion (TS2403 text), indexedAccessNormalization, cyclicGenericTypeInstantiationInference,
recursiveClassBaseType (TS2310/TS2506 once `root()` returns void) are in triage's seeds 11/13/14 list; TS4109
(recursiveArrayNotCircular extends-object) is the TS4109/TS4110 item above. New ones, each one mutant:
- tsc evaluation-order quirks, both "an error reported while a type is computed early": (1) typeGuardsAsAssertions
  null-arg: `result = someFrom(isSome(null) ? result.some : makeSome())` in a `while` loop is TS2339 'some' on 'None'
  in tsc, from the loop's flow evaluation seeing `result` as only its initial `None` while the assignment's type is in
  flight (same family as the vue loop-flow TS2538/TS4111 note); (2) arrayFrom return-void: after a failed overload
  `Array.from(v, ({ a }): B => ({ b: a }))` with `v: void`, tsc reports TS2339 'a' on 'unknown' only when the
  body uses `a`: the binding element's type is computed (with error) from the contextual `unknown` before
  assignContextualParameterTypes replaces unknown by the pattern's type (`f(v, ({ b }) => { b; })` errors,
  `f(v, ({ b }) => {})` doesn't). tttt types elements once, lazily, from the final parameter type.
- expandoFunctionSymbolProperty delete-stmt: an interface member with an unresolved (any-typed) computed key,
  `readonly [symb]: boolean`, makes a number index signature in tsc (getIndexInfosOfIndexSymbol with
  hasLateBindableIndexSignature: any is assignable to number), so `() => void` isn't assignable (TS2322 "Index
  signature for type 'number' is missing"). tttt's late-bound index signatures (builder_add_computed_index_infos) exist
  for classes only. Odd tsc detail when extending to interfaces: `interface A { [s]: boolean }` with `s: string` gives
  A a string index whose reads are `any` (keyof A is string | number), while an any key's number index reads boolean.
- contextualTypeFunctionObjectPropertyIntersection extends-object: `[K in TEvent["type"] as K extends Object<string> ?
  K : never]?: Action<TEvent extends { type: K } ? TEvent : never>` (Object<string> is TS2315, errorType, so the
  conditional is errorType and the key an any-keyed index signature) still gives `FOO: (ev) => ...` its contextual
  type in tsc; tttt says TS7006. Repro: the interface MC/cm pair in the mutant (a reduced mapped type without the
  `TEvent extends { type: K }` value type works).
- interfaceExtendingOptionalChain extends-object: `interface C1 extends Object?.Bar {}` is TS2499 *and* the name
  resolved as a type reference (TS2702 'Object' only refers to a type...; TS2503/TS2694 for others): tsc's
  getTypeReferenceName takes the optional chain as an entity name (isEntityNameExpression ignores `?.`). tttt keeps
  it as an invalid expression (HeritageClauses.invalid) without a TypeNode, so nothing resolves it.
- infinitelyExpandingTypes1 add-optional: `owner?: List<List<T>>` against `owner: MyList<MyList<T>>` reaches tsc's
  depth limit (TS2321 "Excessive stack depth comparing List<number> and MyList<number>" on `l == m`); tttt's
  deeply-nested check stops it first. Needs tsc's exact isDeeplyNestedType accounting through optional properties.
- twiceNestedKeyofIndexInference add-optional: an overload's signature in TS2769 prints
  `Required<Pick<State, "d">>` in full in tsc, `Required<...>` in tttt (the elision budget of a signature in an
  overload list).
- multipleInferenceContexts delete-stmt (`& ThisType<Instance<Data>>` deleted): `this` in an object literal method
  is `ComponentOptionsProperties<{ msg: string; }>` in tsc (the inferred instantiation of the contextual type) where
  tttt prints `ComponentOptionsProperties<Data>`; tsc's errors baseline also has its TS-1 "pre-emit/post-emit
  diagnostic counts" harness note from that.

## Triage drain 2026-10-07 (agent D): checker items re-checked, not fixed
- jsxComplexSignatureHasApplicabilityError: spurious TS2769 with no elaboration. Narrowed: both overloads (Component's
  two constructors) fail in the assignable pass on tsc's extra whole-intersection property check (target
  `IntrinsicAttributes & IntrinsicClassAttributes<..> & Readonly<{children?}> & Readonly<ReactSelectProps<X>>` is no
  generic object type), property `onChange`: source `Props<X>.onChange` (a method, param `Option<X> | undefined`;
  the attribute's anyFunctionType stand-in is absorbed by the intersection as tsc's empty anonymous type would be)
  against `OnChangeHandler<X>` (param `Option<X> | Options<X> | null`), which fails both ways even bivariantly. Each
  target member alone relates (the third source member, the attributes object, against Readonly<ReactSelectProps>
  too). tsc reports nothing at all (not even the TS2345 one would expect inside the arrow's body), so some
  mechanism makes tsc's relation succeed there; ruled out: the Omit<...> conditional member's constraint (a reduced
  `omit.ts`-style probe fails in tsc too). Probe: copy the test to a project with react16.d.ts as
  node_modules/react/index.d.ts; `TTTT_DEBUG_RELATE=1 TTTT_DEBUG_RELATE_NESTED=1`. Dropping the `onChange` attribute
  or the spread makes it pass. Next: instrument tsc (memory: instrument-tsc-copy) on the same relation.
- tsxStatelessFunctionComponentOverload1 (TS7031 for `declare function f({y1: string})`): tsc reports it when
  something computes the parameter's type (here overload resolution), since checkVariableLikeDeclaration returns early
  for renamed elements of bodiless signatures; tttt skips them eagerly (binding_add_implicit_any_type_errors'
  `bodiless && renaming`). Reporting from lazy type computation (ty_error_at) needs care with several checkers each
  computing it; one test.
- awaitedTypeNoLib: unchanged (without lib, tsc keeps NotPromise<TResult> through `instanceof Thenable` narrowing,
  TS2345, and TS2318 'Awaited' from an unlocated lookup).
- declarationEmitBindingPatternsUnused: unchanged; needs the interface accessor's FunctionImpl in InterfaceMember
  (parse_interface keeps only the setter's parameter type node: spurious TS2304, missing TS2842).
- parenthesizedContexualTyping2: tsc puts TS2345 at the argument's outer parentheses for the two-argument calls
  (lines 26-28: `fun((cond ? f : g), 10)`) but inside them for the three-argument one (line 29); and the third
  argument of `fun(..., x => {...}, 10)` isn't contextually typed in tttt (TS7006 + TS2347 where tsc says TS2345 on
  `undefined`). Two mechanisms; unexplored beyond that.
- templateLiteralTypes1: TS2590 position (tsc's currentNode per checked type node) and no TS2590 from template literal
  / tuple-spread cross products (ty/template.c TODO), unchanged.
- Circularity only an elaboration finds: `const a = { prop: 42, get self() { return a } } satisfies { prop: string;
  self: any }` is TS7022 + TS7023 in tsc (elaborateError types the getter while `a` is resolving), only the TS2322 in
  tttt. Still so.
- Diagnostic order at one position (TS2741 before TS2454 at `[d1, d2]` against `Base[]`,
  derivedTypeDoesNotRequireExtendsClause with `extends Object`): tsc's check order; not looked into further.
- webpack flagflip leftovers (unused imports only in a JSDoc `{T=}`; TS2488 once per union in nuia, also excalidraw)
  and vue's loop-flow TS2538/TS4111 (errors tsc reports from inside loop flow evaluations, like the
  typeGuardsAsAssertions mutant above): order-dependent laziness in tsc, see triage's plan 48 notes; no cheap tttt
  emulation. The TS7022 `labels` note (class extending any, `const old = this.labels; this.labels = []; this.labels =
  old;` in two methods: tsc TS7022 on the first `old` and on `labels`, tttt on both `old`s) is the same family:
  which declaration's resolution meets the cycle first.
- Default lib files are never checked (react-intl-redux: `lib: [..., "esnext.intl"]` without es2021.intl, TS2552
  DateTimeRangeFormatPart in lib.esnext.intl.d.ts). Decision: don't check lib files in full; tsc spends 2.67 s of a
  3.73 s run on an empty es2022+dom project checking them (skipLibCheck false), which would be most of tttt's time on
  small projects. The faithful-enough cheap form, if wanted: when lib_files_checked(), resolve every type reference of
  the loaded lib files eagerly and report what doesn't resolve (TS2304/TS2552/TS2694); error_ctx_error_s() currently
  drops lib-file errors because names there resolve lazily. Worth noting for benchmarks: on corpora without
  skipLibCheck tsc pays this lib check and tttt doesn't.
- libTypeScriptOverride*/libTypeScriptSubfileResolving* (4 tests): libReplacement (`/// <reference lib="dom">` and
  the default libs resolve `@typescript/lib-dom` from node_modules) isn't implemented at all; the harness's shared lib
  program would also have to be replaced per test. The option defaults off in 6.0; low value.

## Mutation seed 17 leftovers (2026-10-07, `./mutate.py --seed 17 -n 4000`: 13 -> 7)
- Known families: typeGuardsAsAssertions null-arg and arrayFrom null-arg (tsc's errors from loop-flow / early
  binding-element evaluation, see seed 2 above), noCircularitySelfReferentialGetter4 (circularity only an elaboration
  finds, TS7022/TS7023), derivedTypeDoesNotRequireExtendsClause (diagnostic order at one position),
  intersectionSatisfiesConstraint (union origin lost after Object.assign: display, agent C's area).
- deferredLookupTypeResolution2 delete-stmt (StringContains deleted, so `ObjectHasKey<O, L>` is a named error type):
  tsc reports TS2538 for `(X & { [k: string]: 'true' })[ObjectHasKey<T, '1'>]` (an object with a string index) but not
  for `{ true: 'true' }[ObjectHasKey<T, '1'>]`; tttt reports both. A non-generic probe (`{ a: 1 }[G<1>]`,
  `{ a: 1 }[Nope]`) gives TS2538 on both sides, so it's how a generic instantiation of an alias over the error type
  is treated as an index (getPropertyTypeForIndexType's string-index branch vs the generic deferral).
- mappedTypeAsClauseRelationships drop-as (line 17's ` as P` deleted): tsc reports TS2313 "Type parameter 'P' has a
  circular constraint" at (16,33) plus two TS2322 that follow from it; tttt reports none. Not investigated (likely
  circular constraint detection for a mapped type's key parameter, plan 39 area).

## Circularity tsc finds and tttt doesn't (plan 39 stage 3)
- TS2310/TS2313 for cycles found while resolving: circularBaseTypes (TS2313 for a mapped type's `K in keyof T` with T
  the interface whose base is being resolved), circularConstraintYieldsAppropriateError (TS2310).
- A parameter's default reaching the parameter through a call's return type (`(n: number | undefined = [1].map(() =>
  n)[0])`: TS2502 and TS7024 in tsc, nothing here; the direct read is done in `parameter_initial_type()`).
- limitDeepInstantiations (TS2589 at the reference, we say TS2502), circularInlineMappedGenericTupleTypeNoCrash
  (TS2589), recursiveMappedTypes (TS2502/TS2615 in a mapped type over keyof).
- exportAssignmentImportMergeNoCrash (`import Obj from "./a"; export const Obj = void Obj`: tsc's merged local symbol
  makes the `Obj` read the exported const, TS2448 and `any`; tttt reads the import),
  ensureNoCrashExportAssignmentDefineProperrtyPotentialMerge.
- Resolution-order artifacts tsc reproduces on purpose: mixinWithBaseDependingOnSelfNoCrash1 (TS2345: BaseItem has no
  construct signature while Item's base resolves), selfReferentialDefaultNoStackOverflow (TS7022 on `export default`
  reaching itself through a self-import: the default export's type has no RESOLVING_DECLARATION frame),
  functionImplementations (TS2403: `f()` inside f's own return makes tsc's return type Base, not any).
- yieldExpressionInControlFlow: `o = yield* o` with an evolving `[]` isn't found circular (TS7034/TS7005).

## Limits
- excessivelyLargeTupleSpread (TS2799 for a tuple type alias body: TypeNodes have no span, so no current node).
- TS2589: recursiveConditionalCrash4, circularIndexedAccessErrors, recursivelyExpandingUnionNoStackoverflow; spurious
  in infiniteConstraints.
- normalizedIntersectionTooComplex (TS2590).
- relationComplexityError (TS2859: tsc counts each intersection-member/literal pair as a recursive relation, 20002 x
  10002 pairs; tttt short-circuits them).

## Relations and type core (more in `type_core.md`)
- mappedTypeRelationships (tsc substitutes `Partial<T>[K]` with `T[K] | undefined`, and TS2542 for writes through a
  `Readonly<T>` index), mappedTypeConstraints2 (indexed accesses of mapped types with template-literal keys: spurious
  TS2536), mappedTypeErrors (TS2339 on `Pick<T, K>.foo`: `check_access()` skips generic mapped types),
  controlFlowGenericTypes (`Box<T>` vs `Box<unknown>` display, a TS2536 at line 218), variadicTuples2 (spread arguments
  against variadic rest tuples), interfaceExtendsObjectIntersectionErrors (heritage errors name the alias, not the
  resolved base).
- deeplyNestedMappedTypes (`Id2<T> = { [K in keyof T]: Id2<Id2<T[K]>> }` still ends in a Maybe),
  reactReduxLikeDeferredInferenceAllowsAssignment and
  circularlyConstrainedMappedTypeContainingConditionalNoInfiniteInstantiationDepth (TS2344 with long elaborations),
  mergedDeclarations7 (tsc relates the imported interface with an uninstantiated `this`), awaitedTypeNoLib.
- distributiveConditionalTypeConstraints (constraint of a distributive conditional type is `boolean`, we print
  `IsArray<T>`), mappedTypeNotMistakenlyHomomorphic (Gen2<ABC.A> vs B relation), mappedTypeRecursiveInference (we hit
  TS2589), inferFromGenericFunctionReturnTypes3, baseConstraintOfDecorator.
- tsxUnionElementType3/4: the props intersection prints `{ children?: ReactNode; }` once where tsc has it twice.
  Mechanism (2026-10-06): react.d.ts's `props: P & { children?: ReactNode }` is in `class Component<P, S>`, which is
  merged with `interface Component<P, S>`; tsc's isTypeParameterPossiblyReferenced says yes for any type parameter
  whose symbol has more than one declaration (the merged P, S and the class's `this`), so the type literal is
  instantiated per instance (RC1's and RC2's are two types). tttt's collect_free_params() is purely syntactic. A fix
  needs the owner's merged type parameters at ty_anonymous_from_node() (LowerCtx carries no owner).
- vscode eopt `extHostTypeConverters.ts:647/662`: spurious TS2375 on the nested `options: { ...entry.options, contents
  }` and `textEdit: {...}` inside `result.edits.push({...})`, where tsc reports only TS2412 on `metadata`. Target
  `Array<IWorkspaceFileEditDto | IWorkspaceTextEditDto | IWorkspaceCellEditDto>`, `Dto<T>` a conditional over a
  homomorphic mapped type over `Omit<...> & {...}`. Three reductions all match tsc (which reports TS2375 there too).
  Lead: tsc's elaborateElementwise (getBestMatchIndexedAccessTypeOrUndefined, then getBestMatchingType for a union
  target member lacking the property may skip it). Next step: instrument tsc's elaborateElementwise on the real file.
- Seed-5 mutation leftovers (triage 2026-10-07, `MUTATE_DIR=/tmp/tb-mutate ./mutate.py -n 8000 --seed 5`; 25 left):
  `if (true) type s = string;` (TS1156) is bound into the enclosing block-scope container in tsc, so an earlier `"" as
  s` resolves (tttt: spurious TS2304; interfaces work); `[...x = a] = a` never checks a rest element's initializer
  (TS1186 only), tttt resolves `a` at bind time (TS2304), `export default v` in a block likewise; `interface C1 extends
  Object?.Bar` (TS2499) still resolves the name in tsc (TS2702 too); two `get [Symbol.hasInstance]()` (TS2355 from the
  merged accessor's type); `({ key }) => {}` under a `[testD()]` computed key with testD unresolved is TS7031 in tsc
  (reason not found); `Object<Initable> & Initable` constraint (TS2315) then a mixin: tsc says TS2507 on `{...} & K`;
  missing TS2313 (mut4105/mut3177, plan 39); 9 text/elaboration ones.
- Seed-4 mutation leftovers (`MUTATE_DIR=/tmp/tc-mutate4 ./mutate.py --last`, 21 left, lib DOM union order WONTFIX):
  missing TS2339 x3, spurious TS2304 x3, TS2420, TS1109, TS2347, TS2741, TS2403, TS2355, TS2359, TS2551->TS2339,
  spurious TS2322 (mut2988); elaboration paths mut4551/mut7482 (plan 34). Not looked at.
## Inference
contravariantOnlyInferenceFromAnnotatedFunction (A from an annotated arrow inside a reverse mapped type's source: tsc
infers string, tttt unknown; mechanism not found), tsxStatelessFunctionComponentsWithTypeArguments4 (overloaded components don't get
tsc's TS2769 report), intraExpressionInferencesJsx, unionAndIntersectionInference3, mappedTypeRecursiveInference2,
variadicTuples1 (types), genericCallWithGenericSignatureArguments3 (`(n: Object) => 1` returned where the contextual type is
the outer call's U keeps its literal return type in tsc, `=> 1`; tttt widens to number).

## Local vs export symbol split
tsc keeps one local symbol per block for a name's exported and local declarations, tttt two symbols
(function_local_symbol_decls() and check_export_merge() patch over it; hacks.md "Exported and local declarations").
Still wrong: multipleExports (`export {x}` in a namespace whose other block exports x: TS2484),
conflictingDeclarationsImportFromNamespace1/2 (`import * as pick` + `export const pick` merge, tsc's TS7023
circularity), jsExportMemberMergedWithModuleAugmentation2 (duplicates across an import and a declaration: TS2300 vs
our TS2671; checkerInitializationCrash's symbols: tsc keeps the two conflicting cross-file symbols apart), overloadModifiersMustAgree/functionOverloadErrors types (tsc's local
symbol has all of a block's overloads).

## JavaScript (plan 26)
Binder gaps (spurious TS2339 and friends):
- **Class members aren't binder symbols**, so `Clazz.method.prop = 5` (a static method as an expando host, tsc's
  bindPotentiallyMissingNamespaces giving the method symbol a namespace declaration) can't declare prop:
  jsDeclarationsClassStaticMethodAugmentation (TS2339 on `() => void`).
- **Chained expando initializers** `var G = m.G = {}` (typeFromPropertyAssignment18): tsc types G as `typeof m.G` and
  merges the exports of the variable and of the member (getTypeOfFuncClassEnumModule -> getSymbolOfExpando ->
  mergeJSSymbols), so `G.q` (declared on m.G) and `m.G.p` (declared on G) resolve. An approach: one exports table for
  both hosts, and the variable's type being the member's.
- **Entity-name CommonJS exports aren't aliases**: tsc's exportAssignmentIsAlias makes `exports.blah =
  exports.someProp` an alias (SymbolFlags.Alias), whose resolution under a `module.exports = function` reaches itself
  through the function's expando exports: TS2303 at `exports.blah` (pushTypeGetTypeOfAlias). tttt binds a property of
  type any. Likewise `exports.x = undefined` then `= a` (moduleExportDuplicateAlias, moduleExportDuplicateAlias3:
  every one an alias in tsc, TS2722; hacks.md "CommonJS").
- `exports.x =` next to `module.exports = {...}` (typedefCrossModule2: tsc merges them).
- **CommonJS exports inside functions**: tsc binds `exports.x = v` / `module.exports = v` from any container
  (bindExportsPropertyAssignment has no container check); `add_nested_commonjs_exports()` only walks top-level
  statement lists. Needs a declare-step walk into function bodies (the link step must see them).
- **`this.x = v` at the top level of a CommonJS module** should declare an export (bindThisPropertyAssignment,
  SourceFile container with commonJsModuleIndicator); only the script (global) case is done (`add_global_this_chain()`).
- destructuringParameterDeclaration9(strict=false) types: `json = []` under a typed parent prints `any[] | string[]`
  (tsc `string[]`).
Other JS tests: jsDeclarationsImportAliasExposedWithinNamespace (a `@typedef ns.x` namespace merged with an exported
const: TS18042), moduleExportWithExportPropertyAssignment4, templateInsideCallback, fixSignatureCaching (types; an
expando-heavy JS-like file), jsDocDeclarationEmitDoesNotUseNodeModulesPathWithoutError (the @ts-expect-error covers a declaration-emit error, plan
40). Declaration emit (plan 40): jsDeclarationsCrossfileMerge (TS6232),
jsDeclarationsExportAssignedConstructorFunctionWithSub (TS9005), jsDeclarationsTypeReassignmentFromDeclaration2
(TS9006).
- CommonJS leftovers: `exports.Date = Date; exports.Date = 1` also gets TS2322; `function f() { var z = 1; exports.z =
  z; }` gets TS2339 (exports inside functions).
## Parser recovery (plan 27 territory, each its own mechanism)
- Error dedupe order at the end of the file: tsc reports two errors at the same EOF position because another error
  came in between (labeledStatementDeclarationListInLoopNoCrash4: '}' expected + TS1160; jsxAndTypeAssertion: ':'
  and '</' expected); tttt's last-error dedupe drops the second.
- tsxAttributeInvalidNames: `<test1 32data={32} />` wants ';' expected and TS1351 at the same position (scanner
  error order).
- jsdocFunctionTypeFalsePositive (`@param {<} x`), scannerUnexpectedNullCharacter1 (a NUL outside a string ends
  tttt's parse: `*p.pos` is the EOF test everywhere), parser0_004152, jsxInvalidEsprimaTestSuite,
  thisTypeInFunctionsNegative, manyCompilerErrorsInTheTwoFiles (scanner recovery after `#!` mid-line).
- A class member starting with `<` (`class D { <T>() {} }`): tsc says '=>' expected at the `{`, tttt TS2304/TS1109.
- Object literal grammar errors in destructuring assignment targets (TS1255 `!`, TS1042 modifiers, TS1162 `?`): tsc
  reports checkGrammarObjectLiteralExpression's errors for a target exactly when checkObjectLiteral runs on it, i.e.
  when the right side asks for its contextual type: `({ a!: a } = { a: 1 })` reports, `= x` and `for ({ a!: a } of
  ...)` don't, nested targets of a reporting assignment do. tttt reports TS1255/TS1042 in the parser always (silent
  in for-of) and TS1162 (`object_check_grammar`) never in targets. Fix: move them to the checker's grammar check, run
  for a target only when the right side is contextually sensitive. Low value.
- importTag14 (`/** @import * from "./foo" with */`): tsc says `'as' expected` at `from`, `'from' expected`, TS1464 and
  TS2823 on `with`, `'{' expected`; tttt only `'from' expected` at the wrong token. `jsdoc_import_statement()`'s
  pre-scan bails at the first problem instead of recovering like parseImportTag/parseNamespaceImport. Low value.
## JSX
- TS6229 isn't checked for union tag types or inside an overload chain.

## Messages: elaboration and printing (plan 34)
- Into a `NoInfer` member: against `NoInfer<{ type: 'A' }> | Expr` tsc elaborates to `'"B"' is not assignable to
  '"A"'`, tttt reports the whole argument (best_matching_type doesn't look through the substitution). tttt also adds
  TS6500 related info on `NoInfer<{...}>` targets, tsc doesn't.
- Union sources: for `Snap<...>` (a union alias) not assignable to `number`, tsc adds a line for the union's first
  member; tttt omits it.
- xstate meta.test.ts 270/434: elaboration stops one level too high (at `e2:`, tsc goes on to the `meta:` under the
  `@ts-expect-error`). Not reduced.
- Elaboration chains: mappedTypes6 (`-?` template elaboration), indexedAccessRelation, jsxExcessPropsAndAssignability
  (an intersection target `T["x"] & string` elaborated through the intersection), invariantGenericErrorElaboration,
  keyofAndIndexedAccessErrors, conditionalTypes1/2, bigintWithLib,
  recursiveConditionalTypes, mappedTypeInferenceErrors.
- **Node reuse in error messages**: tsc's typeToString reuses syntactic nodes even without an enclosing declaration
  (a single `return x as T` prints `() => T` as written; done only for a T that names nothing,
  `type_node_names_nothing()`; an arrow's return type from a JSDoc `@type`'s signature: propertyAssignmentUseParentType2). tttt reuses nodes only for the types baseline
  (`d.underline && d.enclosing`); error display would need the reuse path without name-accessibility checks.
- **Alias/origin preservation and literal order**: `keyof Props` printed as `"when" | "error"`
  (reactDefaultPropsInferenceSuccess); literal creation order puts `1 | 4` where tsc says `4 | 1`
  (assignmentCompatWithDiscriminatedUnion).

## Declaration emit (plan 40)
arrayFakeFlatNoCrashInferenceDeclarations (TS5088), declarationEmitPrivatePromiseLikeInterface (TS7056).
