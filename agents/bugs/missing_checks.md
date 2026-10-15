# Missing and wrong checks (the suite's long tail)
The work queue for "suite checker bugs" sessions: diagnostics tttt doesn't report, or reports under another code, and
the failing tests behind them. Find fresh buckets with `scripts/failstats.py` (single-code tests: errors fail on one code
alone in `build/failstats/tests.tsv`) and `scripts/unimplemented_codes.sh` (codes `src/` never mentions). Every test named
here fails on the tsgo 7.1 suite (pruned 2026-10-09; harness-only causes are in `harness.md`, type-core causes in
`type_core.md`). Entries naming a mutant (`mutNNNN`, "X delete-stmt") describe a mutation of a test that itself passes; five of them rechecked as probes 2026-10-09
(satisfies getter, `extends Object?.Bar`, drop-as, spread loop, deferredLookupTypeResolution2) all still differ.

## Known missing checks
- `interface I<C> { a: StateNode<C, any> }` where StateNode requires `C extends MachineContext`: tsc's TS2344 on the
  member's type argument is missing (xstate).
- Missing TS2353 for `const x: Trans<E> = { NEXT: ... }` in a generic function with `Trans<E> = { [K in E['type'] |
  '*']?: ... }` (xstate; excess property checks against a generic mapped type).
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
  exempts any property whose parent is the class); `globalThis.globalThis = x` (TS2540). Private names: TS18015.
- **Declarations**: `export function g() {}` + `function g() {}` is TS2395 here, TS2393 in tsc. TS2300 for a second
  `export =`.
- **Operators**: `void` operands/objects (UndefinedFacts in tsc: `v.x` with `v: void` is TS18048) aren't checked like
  undefined. `for (x in o)` with a non-declaration target: TS2322 here, TS2405 in tsc.
- **Type predicates**: TS1228 (predicate in a constructor type, accessor, parameter type), TS2775/TS2776 (assertion
  callee without explicit types), a predicate function returning non-boolean.
- **Implicit any**: widening errors past `report_errors_from_widening` (binding.c; the tests plan 37 named for them
  pass since, re-check with a probe: TS7008 class fields, TS7018 through a spread) and of generator yield/next types; TS7017
  (`typeof globalThis` has no index signature).
- **Target-gated**: TS2705 comes only from an inferred return type's demand (tsc's createPromiseReturnType), and tttt
  infers more eagerly than tsc, so the two can disagree; `await` in a namespace body
  counts as top level (tsc: TS1308).
- **TS2538 with `any`**: `any[[]]` (a tuple as an index type on `any`): tsc's getPropertyTypeForIndexType reports past
  the `any` object shortcut.
- **Iteration**: TS2489/TS2490 (iterator without `next`/`value`), TS2763-2765 (iterator next-type mismatches);
  generator next types aren't inferred from `yield` uses (tsc's `nextTypes`).
- **Modules**: exports in non-module containers dropped silently (`scope_add_port()`, TODO TS1194); `import { type
  require }` isn't type-only for TS2441; a nested
  `declare module "m"` gets TS2435 but is still bound as a global ambient module (tsc: a local namespace).
- **Templates**: `${symbol}` not rejected.
- **Misc**: TS2353 for an object literal assigned to an assignment pattern (`({ a } = { a, b })`); TS2561/TS2820 want
  the spelling suggestion (`spelling_suggestion()` exists); TS2551 suggestions don't filter inaccessible members;
  TS2793 only for function declarations, not overloaded methods/constructors (`add_implementation_success`); TS2406
  (`for (#x in o)` says TS1451).
- **Variance annotations**: `type B<in T, in T> = { a: T }` misses tsgo's TS2636 at the second `in T` (the two are
  one type parameter symbol; why only the second check fails in tsgo wasn't looked at). Probe only, no test.
- nodeModulesJson (node16/node18/nodenext): symbols and types of JSON imports.
- The jsxImportSource runtime import (`jsx_runtime_module()`) reports TS2875/TS2792 and TS1479/TS1471, not the other
  resolveExternalModule diagnostics (TS7016 for a JavaScript runtime).
- `while (a) const x = 1;` still scopes x to the loop (tsc: the enclosing block).

## Mutation seed 2 leftovers (2026-10-07, `MUTATE_DIR=... scripts/mutate.py -n 8000 --seed 2`: 32 -> 13)
Each one mutant:
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
  tttt prints `ComponentOptionsProperties<Data>`.

## Triage drain 2026-10-07 (agent D): checker items re-checked, not fixed
- tsxStatelessFunctionComponentOverload1 (TS7031 for `declare function f({y1: string})`): tsc reports it when
  something computes the parameter's type (here overload resolution), since checkVariableLikeDeclaration returns early
  for renamed elements of bodiless signatures; tttt skips them eagerly (binding_add_implicit_any_type_errors'
  `bodiless && renaming`). Reporting from lazy type computation (ty_error_at) needs care with several checkers each
  computing it; one test. (2026-10-14 probe: `declare function f({y1: string})` never called, or only named in
  `typeof f`, has no TS7031 in tsgo; called once, it has.)
- awaitedTypeNoLib (2026-10-12): the narrowing matches (both keep the union); the difference is inference. With
  `PromiseLike` unresolved (noLib: the nested `T extends PromiseLike<unknown> ? never : T` extends an error type; the
  same with lib and `T extends any ? never : T`), tsgo infers `R = TResult` for `rp<R>(result: Thenable<R>, resolve:
  Receiver<R>)` called with `Thenable<NotPromise<T>> | NotPromise<T>` and `Receiver<T>`, tttt `NotPromise<T>` (no
  TS2345). Not when the argument is `Thenable<NotPromise<T>>` alone. Likely the union source's NotPromise<T> member
  adds a candidate (its apparent type through the any-extends conditional) or makes the covariant candidate fail
  isTypeAssignableTo(..., contra candidate). The TS2318 'Awaited' follows from the TS2345 (the missing-await
  suggestion's getAwaitedTypeOfPromise). Reduce with `type NotPromise<T> = T extends
  Thenable<unknown> ? T : T extends any ? never : T`.
- awaitedTypeNoLib (2026-10-14, agent B; the relation half is fixed by 5edf168b, the Awaited TS2318 by 88f22cc7): what's
  left is the base constraint of `NotPromise<TResult>`: tsgo simplifies `T extends PromiseLike<unknown> ? never : T`
  (PromiseLike an error type under noLib) to never before computing it (getResolvedBaseConstraint ->
  getSimplifiedConditionalType), so the base constraint is `Thenable<unknown>` and inference from the union argument
  adds `unknown`, making R = TResult and the TS2345. Commit c2aaa6c2 did that (resolved_base_constraint() simplifying
  the conditionals may_simplify_conditional() let through, mikro-orm/seeder types unchanged) and was reverted (277199cd):
  it lost tanstack-table/packages/angular-table (sourcesweep, +TS2345 at tests/test-utils.ts:20). The cause there is a
  separate bug: `ty_definitely_assignable(keyof M<T>, string)` is true for `M<T> = { [K in keyof T as T[K] extends
  Sig<any> ? K : never]?: ... }` because under the restrictive instantiation (conditional.c, restrictive_type_param())
  the TY_KEYOF relation instantiates the name type to never (`keys never` in structured_related's TY_KEYOF case), so
  `Extract<keyof M<T>, string>` simplified to `keyof M<T>`, whose base constraint is string | number | symbol. Probe:
  that M, `for (const k in inputs) { const s: string = k; }` with `inputs: M<T>`, `T extends NonNullable<unknown>`.
  Fix that, then re-apply c2aaa6c2.
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

## Mutation seed 17 leftovers (2026-10-07, `scripts/mutate.py --seed 17 -n 4000`: 13 -> 7)
- Known families: typeGuardsAsAssertions null-arg and arrayFrom null-arg (tsc's errors from loop-flow / early
  binding-element evaluation, see seed 2 above), noCircularitySelfReferentialGetter4 (circularity only an elaboration
  finds, TS7022/TS7023), derivedTypeDoesNotRequireExtendsClause (diagnostic order at one position).
- deferredLookupTypeResolution2 delete-stmt (StringContains deleted, so `ObjectHasKey<O, L>` is a named error type):
  tsc reports TS2538 for `(X & { [k: string]: 'true' })[ObjectHasKey<T, '1'>]` (an object with a string index) but not
  for `{ true: 'true' }[ObjectHasKey<T, '1'>]`; tttt reports both. A non-generic probe (`{ a: 1 }[G<1>]`,
  `{ a: 1 }[Nope]`) gives TS2538 on both sides, so it's how a generic instantiation of an alias over the error type
  is treated as an index (getPropertyTypeForIndexType's string-index branch vs the generic deferral).
- mappedTypeAsClauseRelationships drop-as (line 17's ` as P` deleted): tsc reports TS2313 "Type parameter 'P' has a
  circular constraint" at (16,33) plus two TS2322 that follow from it; tttt reports none. Not investigated (likely
  circular constraint detection for a mapped type's key parameter, plan 39 area).

## Mutation seed 18 leftovers (2026-10-08, `MUTATE_DIR=... scripts/mutate.py -n 2000 --seed 18`: 11 -> 5)
All four are check-order effects of tsc's checker reporting during type computation, which tttt's separate
type/typecheck walks don't reproduce; none has a cheap rule.
- **`satisfies` elaboration reaching a getter in the declaration's own initializer** (2 mutants of
  noCircularitySelfReferentialGetter3/4: `const a = { prop: 42, get self() { return a; } } satisfies { prop: string;
  self: any }`). tsc's checkSatisfiesExpression relates and elaborates while `a`'s type is being resolved;
  elaborateElementwise reads every target property of the source (`self` -> the getter's return type -> `a`), which
  is circular: TS7022 at `a`, TS7023 at `self`. tttt checks `satisfies` in the typecheck walk (expr.c EXPR_AS_TYPE),
  after `a`'s type is known. Fix would be doing the satisfies relation (and on failure, the property reads the
  elaboration does) in ty/expr.c's satisfies case while the declaration is in flight; only matters when it fails.
- **TS2304 inside a `with` body found through flow analysis** (mut1335, sourceMapValidationStatements: a deleted
  `var z` makes `z = 10` in `with (obj) { ... }` unresolvable). tsc never checks the body (checkWithStatement), but
  the flow walk of a later reference `x` passes the assignment `z = 10` and isMatchingReference calls
  getResolvedSymbol(z), which reports "Cannot find name 'z'". tttt's with-body scope (`Scope.with_body`) reports
  nothing. Reproducing it means reporting a with-body name's TS2304 when a flow query visits its assignment.
- **The loop's partial type reported too** (mut1660, selfReferencingSpreadInLoop with `let additional = undefined`,
  noImplicitAny, not strict): tsc says TS2488 twice at `...additional`, once for `undefined` (the auto type's first
  pass while the loop's flow type is computed: getAssignedType checks the assignment with the partial type and its
  diagnostics stay) and once for `any[] | undefined`. tttt reports the final one only. Same family as other
  "diagnostics of a flow-time check" divergences.
- **text TS2345, an inferred object literal printed without its computed-name syntax** (mut1928, symbolProperty22:
  `[Symbol.unscopablesX]`, an error-typed computed name): tsc prints the argument as `{ [x: number]: (s: any) => any;
  }` (the any-named key is a number index signature, and the instantiated type has no node to reuse the name from);
  tttt prints `{ [Symbol.unscopablesX]: ... }`. Printing only (the .types baseline reuses the syntax on both sides).

## Circularity tsc finds and tttt doesn't (the resolution stack's leftovers, ex plan 39 stage 3)
- **Base constraint circularity and base types resolved through constraints** (re-checked 2026-10-07, errors only).
  Port, plan-sized: a resolution-identity stack for base constraints (tsc's pushTypeResolution with
  ImmediateBaseConstraint plus getImmediateBaseConstraint's 5-deep identity check), TS2313 at the type node being
  checked, default type arguments instantiated during base type resolution. None of the corpora show these shapes.
  - circularConstraintYieldsAppropriateError: `class Foo extends NextType<Foo>` with `NextType<C extends { someProp:
    any }, T = C['someProp']> extends BaseType<T>`: tsc reports TS2415 and TS2339 'bar' (the base resolves circularly
    to an error: the cycle is entered from the default's instantiation, reading Foo's `someProp` while Foo's bases
    are in flight); tttt reports TS2310 and keeps `bar`.
- A parameter's default reaching the parameter through a call's return type (`(n: number | undefined = [1].map(() =>
  n)[0])`: TS2502 and TS7024 in tsc, nothing here; the direct read is done in `parameter_initial_type()`).
- recursiveMappedTypes against tsgo's CLI (the suite baseline is JS-emit order: tsgo_divergences.md): (1) no TS2502
  on ListWidget's `"each": Child<ListWidget>` (and its type stays `Child<ListWidget>`, tsc's `any`): tsgo's
  instantiateMappedType instantiates the non-homomorphic Child's constraint `NonOptionalKeys<ListWidget>` eagerly while
  `"each"`'s declared type is resolved (getTypeFromTypeNode of the reference in checkTypeReferenceNode), which indexes
  the inner mapped type's `each` (pushed, TS2615 at currentNode) and reaches `"each"` in flight; tttt instantiates
  the constraint only when the members are resolved (ty_mapped_info_instantiate()), later. Mirroring the eager
  instantiation in instantiate_anonymous() is the fix, but costs an instantiation per mapped type instance (the
  comments there on valibot's cost): measure. (2) A spurious TS2589 next to the TS5114 at `Circular<tup>`: tttt's
  recursion through a homomorphic mapped type over a tuple uses one instantiation-stack frame per level where tsgo uses
  several, so tttt's 4-ary recursion goes ~100 levels deep and runs into the 5M count (tsgo: 28k instantiations).
  Make the frames per level match (which of tsgo's instantiateType calls tttt skips). (3) TS2751 related info of the
  declared-key TS2313 (Recurse2's `K` at currentNode `Recurse2` in Recurse1's `keyof Recurse2`): tttt reports
  declared-key circularity at the member's check, never where it was found.
## Limits
- TS2589 by instantiation *count* (valibot under sloppy/nosnc flagflip cells, ~60 diagnostics; investigated
  2026-10-06 against tsc 6, judged plan-sized, not attempted): tsc's hits are the 5M count limit, from cache hits it
  counts anyway (getObjectTypeInstantiation maps type arguments before its cache lookup, composite/merged mappers
  re-instantiate per getMappedType: ~25M calls in valibot's required.test-d.ts, no new types). Reproducing it means
  mirroring tsc's mapper representation and cache misses, which only reproduces tsc's own blowup and costs perf.
  Accepted divergence; revisit only if real projects show tsc's TS2589 hiding errors tttt reports.

## Relations and type core (more in `type_core.md`)
- reactReduxLikeDeferredInferenceAllowsAssignment and
  circularlyConstrainedMappedTypeContainingConditionalNoInfiniteInstantiationDepth (TS2344 with long elaborations; agent
  B 2026-10-14): tsgo's mappedTypeRelatedTo instantiates the source template with the *target's* key type parameter,
  which for an instantiated mapped type is a fresh one constrained by the instantiated constraint (instantiateMappedType):
  `Extract<keyof TInjectedProps, keyof GetProps<C>>`. tttt's instances share the root's key parameter
  (ty_mapped_info_instantiate()), so the elaboration shows the declaration's `Extract<keyof InjectedProps, keyof
  DecorationTargetProps>`. Cloning it there (constraint = the instantiated one, P -> P' in the template mapper) gives
  tsgo's line but then the relation ends Maybe (`TD[keyof TD & string]` deeply nested 3 times) and the TS2344 is lost:
  tsgo's deeper lines also index through the true branch's substitution (`TI[keyof TI & Extract<string, keyof TD>]`,
  tttt `TI[Extract<string, keyof TD>]`), which keeps its chain shorter. Probe: Shared/Matching with `<TI, TD>` assigned
  (/tmp-style: `function f<TI, TD>(m: Matching<TI, TD>) { const s: Shared<TI, TD> = m; }`). Medium-large.
  Also: inferFromGenericFunctionReturnTypes3.
  Agent B 2026-10-15, with an instrumented tsgo (relater.go REL trace, see display.md's complicatedIndexed... fix):
  (1) the substitution: tsgo's newConditionalType stores `instantiateType(root.checkType, mapper)` (substitutions kept)
  while resolving with getActualTypeVariable; tttt's conditional_from_root() stores the stripped check type. Storing the
  substituted one (`if (root->conditional.check != ty_actual_type_variable(...)) check_type = ty_instantiate(root
  check, mapper)`) fixes the printing (`type C<T, P> = P extends keyof T ? { a: T[P] extends string ? 1 : 2 } : 3` at
  `C<TI, Extract<string, K>>` prints tsgo's `TI[keyof TI & string]`) but gives vue 3 spurious TS2345 in
  dts-test/setupHelpers.test-d.ts:320 (`IfAny<...>`-shaped InferPropType deferred conditionals no longer assignable to
  T): some code reading `conditional.check` expects the bare type variable (substitution types now appear there,
  `Constructable & T`); find which before retrying. (2) with the clone, tttt's chain is deeply nested where tsgo's isn't:
  tttt's target stack holds `undefined` (a member of the target union) three times, pushed at each conditional source
  level, where tsgo relates each conditional's constraints to the whole union (RecursionFlags.Source) and only pushes
  the member inside unionOrIntersectionRelatedTo's attempts (popped after). Find which tttt path pushes the target
  member again inside an attempt (relate.c's conditional-source handling vs `some_related`).
  mergedDeclarations7 (tsc relates the imported interface with an uninstantiated `this`),
  inferFromGenericFunctionReturnTypes3.
- vscode eopt `extHostTypeConverters.ts:647/662`: spurious TS2375 on the nested `options: { ...entry.options, contents
  }` and `textEdit: {...}` inside `result.edits.push({...})`, where tsc reports only TS2412 on `metadata`. Target
  `Array<IWorkspaceFileEditDto | IWorkspaceTextEditDto | IWorkspaceCellEditDto>`, `Dto<T>` a conditional over a
  homomorphic mapped type over `Omit<...> & {...}`. Three reductions all match tsc (which reports TS2375 there too).
  Lead: tsc's elaborateElementwise (getBestMatchIndexedAccessTypeOrUndefined, then getBestMatchingType for a union
  target member lacking the property may skip it). Next step: instrument tsc's elaborateElementwise on the real file.
- Seed-5 mutation leftovers (triage 2026-10-07, `MUTATE_DIR=/tmp/tb-mutate scripts/mutate.py -n 8000 --seed 5`; 25 left):
  `if (true) type s = string;` (TS1156) is bound into the enclosing block-scope container in tsc, so an earlier `"" as
  s` resolves (tttt: spurious TS2304; interfaces work); `[...x = a] = a` never checks a rest element's initializer
  (TS1186 only), tttt resolves `a` at bind time (TS2304), `export default v` in a block likewise; `interface C1 extends
  Object?.Bar` (TS2499) still resolves the name in tsc (TS2702 too); two `get [Symbol.hasInstance]()` (TS2355 from the
  merged accessor's type); `({ key }) => {}` under a `[testD()]` computed key with testD unresolved is TS7031 in tsc
  (reason not found); `Object<Initable> & Initable` constraint (TS2315) then a mixin: tsc says TS2507 on `{...} & K`;
  missing TS2313 (mut4105/mut3177, plan 39); 9 text/elaboration ones.
- Seed-4 mutation leftovers (`MUTATE_DIR=/tmp/tc-mutate4 scripts/mutate.py --last`, 21 left, lib DOM union order WONTFIX):
  missing TS2339 x3, spurious TS2304 x3, TS2420, TS1109, TS2347, TS2741, TS2403, TS2355, TS2359, TS2551->TS2339,
  spurious TS2322 (mut2988); elaboration paths mut4551/mut7482. Not looked at.
## Inference
tsxStatelessFunctionComponentsWithTypeArguments4 (overloaded components don't get tsc's TS2769 report), variadicTuples1
(types).

## Local vs export symbol split
tsc keeps one local symbol per block for a name's exported and local declarations, tttt two symbols
(function_local_symbol_decls() and check_export_merge() patch over it; hacks.md "Exported and local declarations").
Still wrong: checkerInitializationCrash's symbols (tsc keeps the two conflicting cross-file symbols apart).

## Parser recovery (each its own mechanism; `scripts/syntaxstats.py` ranks them)
- A NUL byte inside the file is an invalid character (at_eof() in parser.c, 2026-10-14) in the token scanner and the
  list loops; other `*p.pos` stops (keyword lookahead, JSON values) still take it as an end, harmlessly as far as
  probed (they see no token, the list skips it).
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
- typeofImportDefer (symbols/types): the missing identifier of `.defer` should print as `Symbol(unknown)` / `> : any`.
- Older parser-session leftovers (ex plan 27, 2026-09-29) are history now; `scripts/syntaxstats.py --list A` (B..E, M)
  shows the current state. The framework (lists, commit points, missing nodes) is documentation.md "Error recovery".

## JSX
- TS6229 isn't checked for union tag types or inside an overload chain.

## Messages: elaboration and printing (`scripts/errtextdiff.py`)
- Into a `NoInfer` member: against `NoInfer<{ type: 'A' }> | Expr` tsc elaborates to `'"B"' is not assignable to
  '"A"'`, tttt reports the whole argument (best_matching_type doesn't look through the substitution). tttt also adds
  TS6500 related info on `NoInfer<{...}>` targets, tsc doesn't.
- Union sources: for `Snap<...>` (a union alias) not assignable to `number`, tsc adds a line for the union's first
  member; tttt omits it.
- xstate meta.test.ts 270/434: elaboration stops one level too high (at `e2:`, tsc goes on to the `meta:` under the
  `@ts-expect-error`). Not reduced.
- Elaboration chains: recursiveConditionalTypes.
- tsxStatelessFunctionComponentsWithTypeArguments4: the overload in the message prints `{ b: number; a: unknown }` in
  tsc, `b: unknown` in tttt (inference for that overload).
- Lesson (ex plan 34): a message change can crash on real code the suite doesn't have (`ty_prop_written_name` on a
  mapped property, caught by type-fest only): run the corpora after printer/elaboration changes.
- **Node reuse in error messages**: tsc's typeToString reuses syntactic nodes even without an enclosing declaration
  (a single `return x as T` prints `() => T` as written; done only for a T that names nothing,
  `type_node_names_nothing()`). tttt reuses nodes only for the types baseline
  (`d.underline && d.enclosing`); error display would need the reuse path without name-accessibility checks.
## Declaration emit (`ty/declaration_diagnostics.c`; approximations in hacks.md "Declaration diagnostics")
- TS5088/TS7056 from the printer: conditional branches' unions printed inside themselves (display_branch(), tsgo's
  typeToTypeNodeOrCircularityElision) and anonymous/conditional types elided eleven deep (anonymous_elided(),
  conditional_elided(): visitAndTransformType's symbolDepth) report to the declaration tracker. Not ported: the
  general visitedTypes cycle check of every object type (tttt checks anonymous types only), type references by
  their node (a deferred reference's symbolDepth identity) and the reverse mapped depth of 100
  (reverseMappedDeclarationSerialization, which first needs its spurious TS5114 gone).
- TS9039 "Type containing private name '{0}' can't be used with --isolatedDeclarations" (tsc's
  trackExistingEntityName, when the builder reuses a written type naming an inaccessible symbol) isn't reported.

## From the 2026-10-08 hacks audit (parser, flow)
- The parser drops some syntax with its diagnostics: decorators before an import in `parse_decl_statement()`'s fallback
  (TS1206 lost); decorators without a declaration (tsc's MissingDeclaration: TS1128 at `@` instead of TS1146); index
  signature parameter initializers (`parse_index_signature()` has no `FunctionArg` list: TS2304 in `[a = b]` lost).
- `let o: { x?: number } | number = 1; ({ ...o } = { x: 1 });`: tsc reports TS2698 at the rest target `o` (narrowed
  to number), tttt nothing. Mechanism: the right side asks for its contextual type, which is getTypeOfExpression of
  the left pattern, i.e. checkObjectLiteral with its spread check (no TS2698 when the right asks for none: `= v as T`).
  tttt computes the assignment's context eagerly (`assignment_value_context`) and `expr_typecheck` skips the spread
  check for `assignment_pattern`, so a faithful fix needs "does this right side ask for its context" (a lazy context,
  or the list of expression kinds that ask).
- A declaration file in an export context (`declare function f(): void; export function f(x: number): void;`): tsc
  reports TS2383 (overloads must all be exported or non-exported) at the exported one, tttt nothing.

## Flagflip after agent D's distributed type parameters (2026-10-09, every cell but vscode: 453 -> 141 missing)
The 141 are almost all the corpora's own default-flag misses repeated in every cell:
- drizzle mysql2/session.ts:281, planetscale-serverless/session.ts:198, singlestore/session.ts:277 TS2416: tsgo finds
  `MySqlQueryResultHKT` not assignable to `MySql2QueryResultHKT` through `with(...).update(...).set(...)`'s
  MySqlUpdateBase<..., TQueryResult, ...>; tttt measures MySqlUpdateBase's TQueryResult variance as invariant +
  unreliable (TTTT_DUMP_VARIANCES: 8) and the structural fallback passes. Variance measurement.
- vue compiler-sfc/src/style/pluginScoped.ts:107 TS5115 ('Container', 'Diff' of postcss-selector-parser), and TS7006 at
  :190/:235 (`n.nodes[0].each(ss => ...)`: the callback parameter is any once the instantiation gave up). tttt never
  hits the instantiation-stack circularity there.
- vue runtime-dom/src/directives/vModel.ts:446 TS2345 (`DirectiveBinding<any, string>` against the intersection of a
  union's getSSRProps parameters): a reduced probe (DirectiveBinding/ObjectDirective/DirectiveHook, a union of three
  ObjectDirectives) reports it in tttt too, so it's something in vue's full types (HostElement intersections of
  ModelDirective, the contextually typed `vModelDynamic.getSSRProps = (binding, vnode) => ...`).
- vue pluginScoped.ts:190 and :235 TS7006 (`n.nodes[0].each(ss => ...)`, n a postcss-selector-parser Pseudo): what
  tsgo's eager-base runaway (TS5115 at :107, which tttt reports since 2026-10-15) leaves behind. tsgo resolves Pseudo's
  base `Container<string, Selector>`'s type arguments in its deepest round, at the depth limit, so `Selector` is the
  error type there for good and Pseudo's `nodes` is `any[]`. tttt only detects the runaway (members_reentered_unguarded()),
  its types are what the first round sees. Order-dependent in tsgo too: with the .d.ts checked first (no skipLibCheck,
  `--singleThreaded`) tsgo reports the TS5115 at the .d.ts's `interface Root extends Container<undefined, Selector>`
  instead and no TS7006. Repro: /tmp-style project with postcss-selector-parser.d.ts and `selector.each(n => { if
  (n.type === 'pseudo') n.nodes[0].each(ss => ss) })`. Emulating the poisoning means modelling which of tsgo's caches
  its deepest round fills: not planned.
- Flag-specific ones left (flagflip, 2026-10-14 agent D):
  - vue/nuia TS2538 renderer.ts:2633 and vue/npafis TS4111 Teleport.ts:562, an auto-typed `let v` assigned from itself
    in a loop (`while (u-- > 0) { result[u] = v; v = p[v] }`): tsgo's *final* type of v there is any (a probe's `const z:
    string = v` before `v = p[v]` is fine in tsc) but it reports TS2538 anyway: the error comes from an intermediate
    evaluation of `p[v]` during the loop analysis, with v's incomplete `number | undefined`, which tsgo's
    checkExpression reports as it goes. tttt's check walk only reports from final types. Reduced (tsc TS2538 at
    `p[v]`, tttt nothing): `function g(p: number[], result: number[], k: number) { let u, v; v = k; u = result.length;
    v = result[u - 1]; while (u-- > 0) { result[u] = v; v = p[v] } }` under strict + noUncheckedIndexedAccess (without
    the `v = k` both agree). Mirroring means reporting what the loop's incomplete evaluations report: architectural.
  - xstate/eopt +TS2322 input.test.ts:272: `spawn`'s `...[options]: SpawnOptions<TActor, TSrc>` compared between two
    instantiations ("Two different types with this name exist"): `{ input?: number } & { [K in "input"]: unknown }` is
    not assignable to itself structurally under exactOptionalPropertyTypes (the intersection's input keeps the missing
    type), tsgo never compares it (the same cached instantiation: identity). Small probes reuse the instantiation in
    tttt too; not reduced.
  - xstate/sloppy spurious TS5115 (type_core.md), webpack/nuia UmdLibraryPlugin.js (triage.md), the -TS7006 in vue's
    pluginScoped.ts (above).
  - vscode/eopt stateToProgressAdapter.ts:453 TS2412->TS2322: `invocation.toolSpecificData = ...` where the property
    is an accessor pair whose getter returns `this._toolSpecificData` (an optional property): tsgo's getter type keeps
    the missing type of the read, so isExactOptionalPropertyMismatch is true (TS2412); tttt only says TS2412 for a
    property declared optional (ty_check_assignable_to_access). Reduced: `class Inv { private _d?: D; get d() { return
    this._d } set d(v: typeof this._d) {...} }` and `inv.d = src` with src `{ kind: "s"; a: boolean | undefined } |
    undefined` against `D { kind: "s"; a?: boolean }`. Needs the missing type to survive reads (deep).
  - vscode/eopt extensionsActions.ts:578 extra TS2412 next to tsc's TS2769 (dialogService.prompt's generic overloads,
    `custom: cond ? undefined : {...}`), and chatGeneratedImages.fixture.ts:101/102 missing TS2322 (elements after
    spreads of an array literal against a union element type): neither reproduced in a probe.

## TS 7.1 window leftovers (triage 2026-10-09)
- **Suggestion diagnostics** (harness only, `@captureSuggestions`): TS6807 overshifts and TS6385 'x' is deprecated for
  identifier references (expr.c identifier_expr_typecheck(), 2026-10-14) are done. Not: TS6133 unused suggestions with
  noUnused* off (unused_enabled() is false then; plain JS too), TS6385 for property accesses, imports and JSX, TS6387 for
  deprecated signatures (a function's reference is decided by the call's signature in tsc: isUncalledFunctionReference),
  `@deprecated` on a declaration whose statement isn't right after the comment (declaration_deprecated() goes back over
  keywords only: the second declarator of `const a = 1, b = 2`). The CLI never prints suggestions.
- syntheticProtectedProperties (symbols only since 2026-10-12): tsgo's createUnionOrIntersectionProperty gives the
  synthetic property a parent when its members have one value declaration (`hasNonUniformValueDeclaration`, a
  synthetic member without one skipped): `w2.content` of `(Mock & Base) | (Mock & Dummy)` is `Mock.content` in the
  symbols baseline, tttt prints `content`. The accessibility itself is check.c's synthetic_access().
- types/ember/v2 (DT sweep): `import Ember from "ember"` in a test with `declare global { interface Function extends
  Ember.FunctionPrototypeExtensions {} }`, the package a script whose `declare module "ember"` is what the import
  means: tsgo reports TS2306 "is not a module" (then TS2339s), apparently because the alias is resolved while the
  global augmentation merges, before the script's ambient modules are in the globals (8-line repro: an @types/ember
  with `declare module "ember" { namespace Ember {...} export default Ember }`).

## Mutation seeds 61/62 leftovers (2026-10-14, agent C: `MUTATE_DIR=/tmp/X scripts/mutate.py -n 4000 --seed 61|62`)
Seed 61: 11 -> 5 differing, seed 62: 11 -> 8 (both before the last fixes' rechecks of each other). Left, each looked at:
- **Circularity met by a check tsgo runs inside a type computation** (seed 61 noCircularitySelfReferentialGetter3/4
  ann-number-string): `const a = { prop: 42, get self() { return a } } satisfies { prop: string; self: any }`: tsgo's
  checkSatisfiesExpression runs while a's type is computed, its failure elaborates every property
  (elaborateObjectLiteral reads `self`'s getter type) and that meets a in flight: TS7022 on a, TS7023 on self, a: any.
  tttt checks `satisfies` in the typecheck walk, after a's type is done. Same family, reversed (seed 62): spurious
  TS7022/TS7023/TS2502 in recursiveTypeInference add-optional (`_zod?:`), spurious TS2339 in
  noCircularitySelfReferentialGetter2 drop-as.
- **Lazy TS2339 of a callback's binding element** (seed 61 destructuringWithGenericParameter null-arg):
  `f(null, ({ a }) => { a.x() })` with `f<T>(o: G<T>, cb: (p: T) => void)` failing on the null: tsgo reports
  "Property 'a' does not exist on type 'unknown'" only when the body uses `a` (a's type computed while the parameter
  has the unknown contextual type), not for `({ a }) => {}`. Check order of a failed call; not attempted.
- seed 61 restElementWithInitializer2 delete-stmt (x2): the seeds 41/42 "Lazy name resolution" entry below.
- seed 62 controlFlowForStatement delete-stmt: `for (x = 5; cond; x = x.length)`: tsgo's message has the loop's
  first-iteration type `number`, tttt the final `string | number | boolean` (with the `number` elaboration): the
  seeds 41/42 "Flow loop partial types in messages" entry.
- seed 62 contextualTypesNegatedTypeLikeConstraintInGenericMappedType1 undefined-init (missing TS7034): the seeds 41/42
  `typeof x` implicit-any entry.
- seed 62 conditionalContextualReturnSubstitutionCache drop-as (render): tttt adds the related TS2208 "This type
  parameter might need an `extends C` constraint" for `T` against `null extends T ? K | null : K`; tsgo doesn't when a
  branch mentions another type parameter (K, constrained or not), but does for `null extends T ? string : number`.
  hasNonCircularBaseConstraint must find the synthetic parameter's constraint circular through K somehow; not found.
  relate.c report_missing_constraint().
- seed 62 localTypeParameterInferencePriority delete-stmt and extends-object (spurious TS2322): without the
  `__schema!: S` property, `typeof ColumnSelectViewImp` to `new <S>() => Table<UnrollOnHover<S>>` fails in tttt
  (S' inferred as Schema, the generic `getRows<C extends keyof S>` the only mention of S); tsgo infers through it.
  Inference, not reduced further.
- seed 62 recursiveTypeInference drop-prop (TS2741 text): the source object literal prints `posts: []` in tsgo,
  `posts: never[]` in tttt (an empty array literal against a circular `any[]` target); not looked into.

## Mutation seeds 41/42 leftovers (2026-10-12, agent C: `MUTATE_DIR=/tmp/X scripts/mutate.py -n 4000 --seed 41|42`)
Seed 41: 10 -> 7 differing, seed 42: 13 -> 5. Left, each looked at:
- **Flow loop partial types in messages** (seed 41 typeGuardsAsAssertions null-arg and str-to-num): an error inside
  a loop whose expression is first checked during the loop's flow analysis is reported by tsgo with the incomplete
  (first-iteration) type: `Property 'some' does not exist on type 'None'` where tttt says `Optional<r>` (with the
  `None` elaboration). tttt checks once with the final type. Modelling it means reporting from the flow-loop
  evaluation (tsgo's checkExpressionCached during getTypeAtFlowLoopLabel); plan-sized, deterministic in tsgo.
- **TS7034/TS7005 for an auto-typed `let x = undefined` read through `typeof x` in some type parameter positions**
  (seed 41 contextualTypesNegatedTypeLikeConstraintInGenericMappedType1): tsgo says implicit any (no flow node there)
  for a type alias's constraint and an interface call signature's default, but not for an alias's default or body
  (probes in the commit message's spirit: `type T2<X extends typeof a> = X` errors, `type T1<X = typeof a> = X`
  doesn't). tttt types them all by the flow (`undefined`). Find which identifiers get no flow node in tsgo's binder
  before touching it.
- **Lazy name resolution** (seed 41 restElementWithInitializer2 delete-stmt): `[...x = a] = a` with x undeclared: tsgo
  only reports TS2304 for x when something resolves it (it's TS1186 and unchecked; the post-emit pass or a later flow
  walk through the assignment reports it); tttt reports every unresolved name at bind time. Suppressing it at the
  rest element makes `[...x = a] = a; [...a = y] = a;` wrong instead (tsgo reports x there, reached by the flow walk
  of the second statement). Order-dependent in tsgo.
- **TS2590 where an intersection is first normalized** (seed 41 normalizedIntersectionTooComplex ann-string-number):
  tttt also reports it at the call (36,14), tsgo only at the callback parameter. Which node is current when the
  too-complex intersection is first built; tttt may build it twice.
- seed 41 moduleElementsInWrongContext delete-stmt: TS2305 for `baz` of a `declare module "ambient"` inside a block
  appears in tsgo only once `export default v` has nothing to resolve; check order.
- seed 41 noCircularitySelfReferentialGetter2 str-to-num: a getter's circular type prints `any` in tttt's message,
  `ZodOptional<ZodType>` in tsgo (circularity resolution order).
- **Missing circularity errors** (seed 42): TS4109 "Type arguments for 'Array' circularly reference themselves" when a
  conditional `P extends Object` (was `void`) relates `ReducerAction[]` while ReducerAction's alias is being resolved
  (recursiveArrayNotCircular extends-object); TS7022/TS7024 when a reduce callback returns the variable it
  initializes (unknownSymbolOffContextualType1 swap-stmts). See "Circularity tsc finds and tttt doesn't".
- seed 42 narrowingDestructuring literal-type: `(({ kind, ...rest }) => rest)(t)` with t narrowed to never: tttt says
  TS2700 for the rest of never, tsgo nothing (`rest: any`); the parameter's contextual never isn't the binding
  element's parent type there. Not found where.
- seed 42 objectRestNegative undefined-init (strictNullChecks off): `let o = undefined; var { ...m, a } = o` is TS2700
  in tsgo when the pattern has another element (the initializer isn't widened to any under the pattern's contextual
  type), not with the rest alone; tttt reports neither and then TS2322 instead of TS2741 for `({ b, ...na } = o)`.
- seed 42 signatureCombiningRestParameters1 add-optional: a union of signatures one of which has a rest parameter of
  type `[string] | undefined` (TS2370): tsgo's combined signature reports a spread argument as TS2556, tttt combines
  the rest parameter to never and says TS2345.
