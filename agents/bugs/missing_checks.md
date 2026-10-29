# Missing and wrong checks (the suite's long tail)
The work queue for the suite checker bugs: diagnostics tttt doesn't report, or reports under another code, and
the failing tests behind them. Find fresh buckets with `scripts/failstats.py` (single-code tests: errors fail on one code
alone in `build/failstats/tests.tsv`) and `scripts/unimplemented_codes.sh` (codes `src/` never mentions). Every test named
here fails on the tsgo 7.1 suite (pruned 2026-10-09; harness-only causes are in `harness.md`, type-core causes in
`type_core.md`). To see what tsgo does inside a check, instrument it: `agents/notes/instrumenting_tsgo.md`. Entries naming a mutant (`mutNNNN`, "X delete-stmt") describe a mutation of a test that itself passes (pruned again 2026-10-16 by probes and hand-made mutants).

## Known missing checks
- **JS**: TS2565 for a property declared by a JS expression statement (`/** @type {F} */ C.prototype.m;`) read in the
  same flow container (tsc's getFlowTypeOfAccessExpression with assumeUninitialized); TS7032 for a setter whose type
  comes from `@type` (probe plan19/webpack/jsdoc_accessor_type.ts). [open]
- **Decorators**: TS2449 for a class used in the decorators of its own methods' parameters (tsc counts them; tttt's
  check only knows class and member decorators). In tsgo it comes from the emit resolver only (`m(@dec(C.s) x)`: the
  suite's TS-1 pre/post-emit count note), so it shows in baselines, not on the CLI. [tsgo]
- A class method merged with an interface's method signature of its name is one symbol in tsc, whose overload flag
  agreement covers both (checkFunctionOrConstructorSymbol: the signature makes it overloaded): `interface I { m():
  void } class I { private m() {} }` is TS2385 at the class's `m` (TS2386/TS2512 likewise, and the signatures are
  overloads the implementation must fit, TS2394): class_check_overload_agreement() and class_check_overloads() take the
  same-file interfaces' signatures (class_merged_signatures()), interface_check_overload_optionality() leaves those
  names to the class. Across files each side still checks alone. Test ttttClassMergedInterfaceOverloads. [fixed cd5e4b4e9]
- **Accessibility**: `this.x = v` in the constructor of a class merged with an interface declaring `readonly x` (tttt
  exempted any property whose parent is the class; tsc only one whose value declaration, the first, is in the class:
  check.c readonly_assignment()). Test ttttReadonlyFromMergedInterface [fixed db2e41209].
- **Declarations**: `export function g() {}` + `function g() {}` is TS2393 in tsc (one local symbol with two implementations; tttt said nothing). Test ttttExportedAndLocalFunctionDuplicate. [fixed 17d5794b5]
- **Target-gated**: TS2705 comes only from an inferred return type's demand (tsc's createPromiseReturnType), and tttt
  infers more eagerly than tsc, so the two can disagree. [open]
- **Variance annotations**: `type B<in T, in T> = { a: T }` misses tsgo's TS2636 at the second `in T` (the two are
  one type parameter symbol, the first's type parameter, which each declaration checks again: tttt checked the second,
  unused, type parameter). ty_check_variance_annotations(). Test ttttVarianceRepeatedTypeParameter [fixed e814fb8b6].
- `while (a) const x = 1;` still scopes x to the loop (tsc: the enclosing block; a do statement too, a for statement is a block scope container). Test ttttWhileBodyDeclarationScope. [fixed 2a4819ab1]
- Known-failing tests of hacks.md's audit (2026-10-23; each hack's entry there has the mechanism):
  - ttttHackJsdocUniqueSymbolLet: a JSDoc `@type {unique symbol}` on a `let` misses TS1332. [fixed e04477695]
  - ttttHackAugmentationAliasConflict: `export { x as y }` in a module augmentation misses TS2323/TS2484. [open]
  - ttttHackJsdocLinkShadowed: a JSDoc `{@link}` to a shadowed name keeps the import used (TS6133 missing). [open]

## Mutation seed 2 leftovers (2026-10-07, `MUTATE_DIR=... scripts/mutate.py -n 8000 --seed 2`: 32 -> 13)
Each one mutant:
- arrayFrom return-void: "Binding elements read before the contextual assignment" (seeds 61/62 below). [dup]
- interfaceExtendingOptionalChain extends-object: `interface C1 extends Object?.Bar {}` is TS2499 *and* the name
  resolved as a type reference (TS2702; TS2503/TS2694 for others; the interface extends it): the parser makes a heritage
  `A?.B` the type reference it would be, flagged `heritage_optional_chain` (TS2499/TS2500 in the checker, value types for
  its qualifiers in the types baseline, `(A?.B)<T>` in declaration emit). [fixed 4b4195aad]
- twiceNestedKeyofIndexInference add-optional: an overload's signature in TS2769 prints
  `Required<Pick<State, "d">>` in full in tsc, `Required<...>` in tttt (the elision budget of a signature in an
  overload list). [text]
- multipleInferenceContexts delete-stmt (`& ThisType<Instance<Data>>` deleted): `this` in an object literal method
  is `ComponentOptionsProperties<{ msg: string; }>` in tsc (the inferred instantiation of the contextual type) where
  tttt prints `ComponentOptionsProperties<Data>`. [open]

- flagflip vscode/exactOptionalPropertyTypes, mcpServerRequestHandler.ts:270 (-TS2322): `{ ...initialParams, cursor }`
  with `initialParams?: Omit<T['params'], ...>` against `T['params']`. A generic mapped type is no empty object (tsc's
  isEmptyObjectType: !isGenericMappedType), so tryMergeUnionOfObjectTypeAndEmptyObject makes the union with undefined
  an optional copy of its known properties (none): the literal is `{ cursor: ... }`, not `Omit<...> & {...}`.
  spreads_into_nothing() took the mapped type for empty (ttttSpreadGenericMappedOptional). [fixed 361e741de] The other
  flagflip cells: vue pluginScoped.ts (TS7006 x8 cells, tsgo_divergences.md's runaway), zod sloppy/nosnc TS2344->TS2589
  (depth-limit noise, tsgo_divergences.md). [design]

## Triage drain 2026-10-07: checker items re-checked, not fixed
- Diagnostic order at one position (TS2741 before TS2454 at `[d1, d2]` against `Base[]`,
  derivedTypeDoesNotRequireExtendsClause with `extends Object`): tsc's check order; not looked into further. [open]
- webpack flagflip leftovers (unused imports only in a JSDoc `{T=}`; TS2488 once per union in nuia, also excalidraw):
  order-dependent laziness in tsc, see the TS 6.0 retarget notes in triage. (vue's loop-flow TS2538/TS4111 are fixed:
  d218d7336 reports a loop's partial evaluations.) The TS7022 `labels` note (class extending any, `const old = this.labels; this.labels = []; this.labels =
  old;` in two methods: tsc TS7022 on the first `old` and on `labels`, tttt on both `old`s) is the same family:
  which declaration's resolution meets the cycle first. [open]

## Mutation seed 17 leftovers (2026-10-07, `scripts/mutate.py --seed 17 -n 4000`: 13 -> 7)
- Known families: arrayFrom null-arg (binding elements read before the contextual assignment, seeds 61/62),
  derivedTypeDoesNotRequireExtendsClause (diagnostic order at one position). [dup]

## Mutation seed 18 leftovers (2026-10-08, `MUTATE_DIR=... scripts/mutate.py -n 2000 --seed 18`: 11 -> 5)
Both are check-order effects of tsc's checker reporting during type computation, which tttt's separate
type/typecheck walks don't reproduce; none has a cheap rule.
- **TS2304 inside a `with` body found through flow analysis** (mut1335, sourceMapValidationStatements: a deleted
  `var z` makes `z = 10` in `with (obj) { ... }` unresolvable). tsc never checks the body (checkWithStatement), but
  the flow walk of a later reference `x` passes the assignment `z = 10` and isMatchingReference calls
  getResolvedSymbol(z), which reports "Cannot find name 'z'". tttt's with-body scope (`Scope.with_body`) reports
  nothing. Reproducing it means reporting a with-body name's TS2304 when a flow query visits its assignment. [open]
- **text TS2345, an inferred object literal printed without its computed-name syntax** (mut1928, symbolProperty22:
  `[Symbol.unscopablesX]`, an error-typed computed name): tsc prints the argument as `{ [x: number]: (s: any) => any;
  }` (the any-named key is a number index signature, and the instantiated type has no node to reuse the name from);
  tttt prints `{ [Symbol.unscopablesX]: ... }`. Printing only (the .types baseline reuses the syntax on both sides). [text]

## Circularity tsc finds and tttt doesn't (the resolution stack's leftovers)
- (cor, 2026-10-28) `const [d, e]: [number, (n: number) => number] = [1, (n) => e(n) + d]`: tsgo reports TS7022 on
  `d` (an element of an annotated tuple pattern read inside a function of its own initializer: the element's type goes
  through getTypeForBindingElement, whose parent type asks the initializer for undefined-removal), tttt nothing.
  Two tsc orders were missing: checkVariableLikeDeclaration resolves the pattern's first name before the initializer
  (initialized_binding_typecheck()), and a function with a contextual signature infers its return type at its first
  contextual check outside calls too (ty_check_expr's EXPR_FUNCTION); a variable's pattern root asked again with a
  name since is evaluated again (lazy_type.c: tsc has no entry for it). Test ttttTuplePatternCircularElement. [fixed e77cf389d]
- (cor2, 2026-10-28) A function expression's parameter defaults weren't resolved at its first check (tsc's
  checkSignatureDeclaration at the end of contextuallyCheckFunctionExpressionOrObjectLiteralMethod), so cycles through
  them went unseen: `const pick = (from: Item[] = children) => from[0]; current = pick()` in a loop over `{ children } =
  current` (TS7022 on children and pick), `done((p = f()) => {})` inside f (TS7022 on p). Now check_function_parameters_first()
  (expr.c) settles each default with its parameter's context. With the parameterInitializerContainsUndefined entry
  (9d2091d15), bun 99-V4 has 17 fewer missing diagnostics. Left: in the loop case the while condition's `current` prints
  `Item` where tsc's is `Item | undefined` (types only, the loop's flow result from inside the cycle is kept), and 99-V4's
  checkObjectLiteralMethod__03 / checkPropertyAssignment__07 report their spurious TS7023 on the first method instead of
  the second (both wrong before and after: tsc has TS2683 there). Test ttttFunctionExpressionDefaultsAtFirstCheck. [fixed ffba37893]
- (cor4, 2026-10-28, bun 99-V4 checkReturnStatement__01/02) Return statements were checked against the annotation as
  lowered after a circular signature return (`[any]`), not against tsc's getReturnTypeOfSignature result (any, TS2577):
  a spurious TS2322. KEY_RETURN_ANNOTATION_CIRCULAR. Test ttttReturnCheckedAgainstCircularAnnotation. [fixed 1429b874a]
- (cor4, 2026-10-28) A variable initialized by a function expression wasn't typed before its initializer was checked
  (tsc's checkVariableLikeDeclaration asks getTypeOfSymbol first), so the function's first check ran with the variable
  settled and `const h = (a = h) => 1` missed TS7022. initialized_binding_typecheck() now asks the type first for
  EXPR_FUNCTION initializers as it did for calls. Test ttttFunctionInitializerTypedFirst. [fixed 764ad7436]
- (cor4, 2026-10-28) The return annotation of a function expression isn't lowered at its first check (tsc's
  checkSignatureDeclaration checks the return type node there): `const f = (): [ReturnType<typeof f>] => 1` is TS7022
  on f and TS2322 `number` to `[any]` in tsc, TS2577 alone in tttt. Lowering it in check_function_parameters_first()
  (expr.c) fixes this but breaks ttttAliasReentersBelowCallBarrier (an arrow's `: Part` return inside a call reaches the
  alias in flight: tsc resolves it again below the call's barrier, the lowering made it circular for good): it needs
  the reentrancy of notes/reentrancy.md for a variable's type, not only the alias's. [open]
- (cor4, 2026-10-28, bun 99-V4 getTypeAtPosition__01/08, instantiateContextualSignature__01,
  assignContextualParameterTypes__03) An earlier parameter's default reading a later unannotated parameter of a
  contextually typed function, `pair(1, (a = b, b) => ...)`: tsc reads b's type in the contextual signature before the
  inference fixed it (`T`), uncached (isParameterOfContextSensitiveSignature), and assigns its instantiated type after;
  tttt typed b any (TS7006) for good. lower.c's parameter_while_assigning() (during assign_contextual_signature_as(),
  Checker.assigning_context_function/signature) for a plain name without default or rest. Left: the types baseline
  of that `b` (tsc's typeWriter rechecks it, number; tttt keeps the checked expression's T), and a later parameter with
  a default or `...` (getTypeForVariableLikeDeclaration's optionality and rest type) still goes through its lazy type.
  Test ttttUnassignedContextualParameterRead (types component fails, that baseline). [fixed bc0424527]
- (cor4, 2026-10-30, bun 99-V4 checkForOfStatement__05) A for-of's declared variable was typed after its expression
  (tsc: the declaration list first), so an evolving array pushed with the variable on the loop's back edge wasn't
  circular (TS7022 missing). Names only (a pattern's computed names go first). Test ttttForOfVariableTypedFirst.
  [fixed 869bb9349]
- (cor4, 2026-10-30, bun 99-V4 checkVariableLikeDeclaration__01, getTypeFromBindingPattern__04/05) A default in an
  unannotated variable pattern reading a sibling through an IIFE, `const [a, b = (() => a)()] = [2]`: tsc checks the
  elements first (checkVariableLikeDeclaration -> checkBindingElement -> getTypeOfSymbol(a)), so a's type is in flight
  while the initializer is checked with the pattern as contextual type, whose defaults are evaluated
  (getTypeFromBindingElement): a is circular there, any (TS7011 on the arrow, TS7005 on a local copying it); tttt checks
  the initializer first and reports TS2454 on `a`. Asking the first name's type first in initialized_binding_typecheck()
  (as it does for annotated patterns under strictNullChecks) fixes the three but broke ttttIifeDestructuredTuple (the
  root pattern's lazy type doesn't give an IIFE initializer the pattern as its return context, the check path does)
  and bun82-R02 (spurious TS7022 on `const [m, n] = [mode, 1]` in a loop that narrows on m): the lazy root type has to
  agree with the checked initializer first. [open]
- (cor4, 2026-10-30, bun 99-V4 getBaseTypes__03, getDeclaredTypeOfClassOrInterface__01/02) Interface base cycles:
  tsc's getBaseTypes pushes ResolvedBaseTypes per type and, re-entered, returns the bases appended so far;
  resolveBaseTypesOfInterface drops a base only if hasBaseType sees the type through those (partial) lists. So with
  `interface A extends X, B` and `interface B extends A` checked A first, A keeps [X] and B keeps [A] (b.x, b.a exist);
  the result depends on which type is asked first. tttt's has_base_type() (members.c) walks the extends clauses
  syntactically and drops every base that leads back, so B has no members (spurious TS2339) and TS2430 is missing. A
  fix: a memoized, resolution-ordered base list per class/interface (partial while in flight) that members.c's base
  loop and has_base_type() both read, with TS2310 where tsc reports it; order-dependent like tsc's, so kdiff it.
  Not built (cor4): tttt's whole-cycle-has-no-bases rule (ty_interface_in_base_cycle(), a shared word of the graph) is
  deliberate, deterministic across checkers; tsc's result depends on which interface a checker asks first, so across
  files it would be K-dependent in tttt (and in tsgo), and base cycles are compile errors real code doesn't keep.
  [design]
- (cor4, 2026-10-30, bun 99-V4 getTypeOfInstantiatedSymbol__01/04/05, +TS2347) A generic class getter returning the
  same getter of another instantiation (`get a() { return bs.a }` in `Box<T>`): tsc's in-flight read of the target
  accessor is errorType (getTypeOfAccessors' failing push), which getTypeOfInstantiatedSymbol instantiates and keeps,
  so `bs.a<boolean>()` is no untyped call (no TS2347) while `bn.a` (computed after, any) is. tttt's prop_type() gives
  TY_CIRCULAR both for the in-flight read and as the target's final value (the getter's circular return), so
  instantiated_prop_type() can't tell them apart: mapping CIRCULAR to the error type there drops both TS2347s. Needs
  the in-flight read to return something distinct (or the final value any). [open]
- (cor2, 2026-10-28, bun 99-V4) **Accessor write-type circularity**: tsc pushes an accessor's write type
  (getWriteTypeOfAccessors, TypeSystemPropertyNameWriteType) and reports a failing pop as TS2502 at the setter's name,
  and a union or intersection property asks each member's write type when it is made (createUnionOrIntersectionProperty).
  prop_write_type() now pushes RESOLVING_PROP_WRITE, find_prop_in_union()/find_prop_in_intersection()/access()'s union
  loop touch the members' write types (prop_write_type_touch()); a class getter's circularity through its annotated
  setter is reported at the setter, and an annotation that lowered to another declaration's circularity is any (no
  TS2502 at `v` in `set x(v: C["x"])`). getWriteTypeOfAccessors__01-03/06, getWriteTypeOfSymbol__01/02/04 match. Test
  ttttAccessorWriteTypeCircularity. [fixed e254bf88c]
- (cor3, 2026-10-29, bun 99-V4, looked at, not fixed) **Accessor and loop leftovers**: (1) checkAccessorDeclaration__04/05:
  tsc checks a getter's code paths against getTypeOfAccessors (any after its circularity), so a `get x(): [C["x"]]`
  whose body can fall off the end has no TS2366; function_impl_check_end() uses the annotation (or the setter's) and
  can't tell a class getter from an object literal's to ask the accessor's type. (2) __01-03: an unannotated setter
  parameter is the getter's *signature* return type, which tsc infers apart from the accessor symbol's type
  (getReturnTypeOfSignature vs getTypeOfAccessors' getReturnTypeFromBody), so it stays `number` where the accessor is
  circular; tttt's one function-return memo makes both any (types only). (3) loop__through_await / checkForOfStatement:
  `for (const child of f(children))` in a loop whose `const { children } = d` reads the loop's variable: tsc re-enters
  `child` below the call's barrier and the second round's push of `children` fails (TS7022 on both, reported at the
  failing push); tttt sees the cycle on the pattern's LazyType (TTTT_DEBUG_CYCLE) but the pattern retry
  (pattern_retried) settles it without a report. A plain `for (const child of children)` matches. (4)
  getTypeOfInitializer__07: an annotated pattern initialized by a generic call in a loop, spurious TS7022 on its name.
  [open]
- (cor3, 2026-10-29, bun 99-V4 getWriteTypeOfSymbol__03) An interface property merged with class accessors of its name
  (`get x(): string; set x(v: string | number)` plus `interface C { x: string }`) is a SymbolFlags.Property in tsc,
  written as it is read (getWriteTypeOfSymbol): `c.x = 1` is TS2322 against string; tttt takes the setter's type. Test ttttWriteTypeMergedAndIndexed. [fixed edb8f216e]
- (cor3, 2026-10-29, bun 99-V4 getWriteTypeOfSymbol__05) A write through a union of a class with a wider setter and a
  type with a string index signature (`u.x = true`, `A | { [k: string]: boolean }`): tsc checks against the union of the
  write types `string | number` (TS2322 at the assignment); tttt reports nothing there. Test ttttWriteTypeMergedAndIndexed. [fixed edb8f216e]
- **Base constraint circularity and base types resolved through constraints** (re-checked 2026-10-07, errors only).
  Port, plan-sized: a resolution-identity stack for base constraints (tsc's pushTypeResolution with
  ImmediateBaseConstraint plus getImmediateBaseConstraint's 5-deep identity check), TS2313 at the type node being
  checked, default type arguments instantiated during base type resolution. None of the corpora show these shapes
  (circularConstraintYieldsAppropriateError, its suite test, passes since 2026-10-14). [design]
- A parameter's default reaching the parameter through a call's return type (`(n: number | undefined = [1].map(() =>
  n)[0])`: TS2502 and TS7024 in tsc, nothing here; the direct read is done in `parameter_initial_type()`). Now
  parameter_initializer_contains_undefined() (expr.c) is tsc's parameterInitializerContainsUndefined: a resolution
  entry of its own (RESOLVING_INITIALIZER_UNDEFINED), the quick type first, memoized when not circular; the cycle
  marks the arrow's nested return inference (TS7024). Left: the arrow's `n` prints `number | undefined` where tsc's
  later body check asks afresh and prints `number` (types only; test ttttParameterDefaultCycleThroughCall fails its
  types component). [fixed 9d2091d15]
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
  declared-key circularity at the member's check, never where it was found. [open]
## Limits
- TS2589 by instantiation *count* (valibot under sloppy/nosnc flagflip cells, ~60 diagnostics; investigated
  2026-10-06 against tsc 6, judged plan-sized, not attempted): tsc's hits are the 5M count limit, from cache hits it
  counts anyway (getObjectTypeInstantiation maps type arguments before its cache lookup, composite/merged mappers
  re-instantiate per getMappedType: ~25M calls in valibot's required.test-d.ts, no new types). Reproducing it means
  mirroring tsc's mapper representation and cache misses, which only reproduces tsc's own blowup and costs perf.
  Accepted divergence; revisit only if real projects show tsc's TS2589 hiding errors tttt reports. [tsgo]

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
  2026-10-15, with an instrumented tsgo (relater.go REL trace, see display.md's complicatedIndexed... fix):
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
  member again inside an attempt (relate.c's conditional-source handling vs `some_related`). [text]
- Seed-5 mutation leftovers (triage 2026-10-07, `MUTATE_DIR=/tmp/tb-mutate scripts/mutate.py -n 8000 --seed 5`; 25 left):
  `[...x = a] = a` never checks a rest element's initializer
  (TS1186 only), tttt resolves `a` at bind time (TS2304), `export default v` in a block likewise; `interface C1 extends
  Object?.Bar` (TS2499) still resolves the name in tsc (TS2702 too); `({ key }) => {}` under a `[testD()]` computed key with testD unresolved is TS7031 in tsc
  (reason not found); `Object<Initable> & Initable` constraint (TS2315) then a mixin: tsc says TS2507 on `{...} & K`;
  missing TS2313 (mut4105/mut3177); 9 text/elaboration ones. [open]
- Seed-4 mutation leftovers (rerun 2026-10-16, `scripts/mutate.py -n 8000 --seed 4`: 12 differ, the suite's passing set
  has moved so the mutants aren't the original 21's): missing TS2589 x2 (esNextWeakRefs_IterableWeakMap member-typo),
  missing TS2430 (jsonRawJSON2: harness only, see seed 2026102101), spurious TS2693 (reachabilityChecksNoCrash1 never-const), missing TS2698
  (nestedSpreadsAndWidening), text TS2322 (templateLiteralTypeSurrogatePairConcatenation), and known families
  (TS2310, TS2590, TS7022, localTypeParameterInferencePriority, signatureCombiningRestParameters1). Not looked at. [open]
### This-instantiated members (design, not built; hono request.ts TS2769, 2026-10-22)
Repro: `test/cases/ttttThisInstantiatedMethodErased.ts` (failing, 15 lines). tsgo: a class (and a non-thisless
interface) is its own type reference, and resolveTypeReferenceMembers pads its type arguments with the type itself for
`this` (`[I]` -> `[I, HonoRequest<I>]`), which differs from `[I, thisType]`, so the declared type's members are an
instantiation with `this -> HonoRequest<I>`. A method's type is really instantiated (getObjectTypeInstantiation) only
when `this` is among its outer type parameters after the isTypeParameterPossiblyReferenced filter over *all* its
declarations: a declaration with a body and no return annotation counts, as does `this` in a signature (the overloaded
`valid` qualifies through its implementation; the same method with only an annotated implementation doesn't, and tsc
then fails like tttt). Through a receiver of type `this` (getTypeWithThisArgument(C, this): arguments equal the
parameters) the members are the raw declared ones. signaturesRelatedTo's "instantiations of the same anonymous type"
case (source and target Instantiated, same symbol) then compares pairwise with type parameters erased: `this` related
to `HonoRequest | R` through its constraint compares `valid` of `HonoRequest<I>` (instance) to `valid` of
`HonoRequest<{}>` (instance) erased, passing on `T extends keyof I & ...` vs `T extends never & ...`. tttt reads the raw
method type off the declared type, so `same_signature_origin()` (relate.c: both TY_ANONYMOUS with one target) fails and
the single-signature path compares unerased. What tttt would change: members resolution of a class/interface's own
declared type gives a qualifying method (tsgo's possibly-referenced rule for `this`) an instance with `this -> the
declared type`, while `this`'s apparent type keeps the raw members; same_signature_origin then works unchanged. Not the
relation-only patch (treating a raw method type as an instance of itself): the relation can't tell a declared-type read
from a `this` read, and tsc compares the latter unerased. Fixed (det3, 2026-10-24) far more narrowly: ty_function_free_params() counts the
overload's implementation, so the declared type's member is an instance (agents/notes/this_instantiated_members.md). [fixed det3]

## Local vs export symbol split
tsc keeps one local symbol per block for a name's exported and local declarations, tttt two symbols
(function_local_symbol_decls() and check_export_merge() patch over it; hacks.md "Exported and local declarations").

## Parser recovery (each its own mechanism; `scripts/syntaxstats.py` ranks them)
- A NUL byte inside the file is an invalid character (at_eof() in parser.c, 2026-10-14) in the token scanner and the
  list loops; other `*p.pos` stops (keyword lookahead, JSON values) still take it as an end, harmlessly as far as
  probed (they see no token, the list skips it). [fixed 2026-10-14]
- A class member starting with `<` (`class D { <T>() {} }`): tsc says '=>' expected at the `{`, tttt TS2304/TS1109. [open]
- Object literal grammar errors in destructuring assignment targets (TS1255 `!`, TS1042 modifiers, TS1162 `?`): tsc
  reports checkGrammarObjectLiteralExpression's errors for a target exactly when checkObjectLiteral runs on it, i.e.
  when the right side asks for its contextual type: `({ a!: a } = { a: 1 })` reports, `= x` and `for ({ a!: a } of
  ...)` don't, nested targets of a reporting assignment do. tttt reports TS1255/TS1042 in the parser always (silent
  in for-of) and TS1162 (`object_check_grammar`) never in targets. Fix: move them to the checker's grammar check, run
  for a target only when the right side is contextually sensitive. Low value. [open]
- `<Component<<T>(v: T) => void> />` (babel's type-arguments-bit-shift-left-like/jsx-opening-element): a JSX opening
  element's type arguments want a `<` token (tsgo's tryParseTypeArguments), so tsgo parses `<Component` then recovers
  from `<<T>(v: T) => void> />` as expressions (TS1003, TS1005, TS1434, TS1128 x2, TS1109, TS1161 plus checker errors);
  tttt takes the `<<` as type arguments. The heritage form is fixed (ParserShared.heritage_type_args); the JSX tag's
  type arguments need the same and then tsgo's recovery of the leftover (ttttJsxShiftTypeArguments). [fixed 817676058]
- `(a.b: any)(...args)` (babel's transform-spread call-context/flow-type-cast, a Flow cast): tttt adds TS1200 "Line
  terminator not permitted before arrow" at the end of the file to tsgo's recovery through an arrow function missing
  its `=>`: a missing `=>` is tsgo's zero-width missing token, whose text checkGrammarArrowFunction finds no line break
  in (also mutation seed 20261101's declarationEmitOptionalMethod). ttttFlowTypeCastRecovery. [fixed 37ca1cf0e]
- `interface I extends f<<T>(v: T) => void> {}`: errors match tsgo since 38655f2c9, the types baseline doesn't (tsgo
  parses the leftover `<<T>(v` as a shift with a missing left operand, tttt drops the `<<`). [open]
- Older parser leftovers (2026-09-29; see documentation.md "Error recovery") are history now; `scripts/syntaxstats.py --list A` (B..E, M)
  shows the current state. The framework (lists, commit points, missing nodes) is documentation.md "Error recovery". [note]

## JSX
- TS6229 isn't checked for union tag types or inside an overload chain: now a union tag's union signatures
  (ty_jsx_tag_arity_fits) and CallInfo.jsx_arity_unfit failing every candidate (an overloaded component is TS2769,
  the last overload's TS6229). Test ttttJsxTagArityUnionOverloads. [fixed 4c3613770]

## Messages: elaboration and printing (`scripts/errtextdiff.py`)
- Union sources: for `Snap<...>` (a union alias) not assignable to `number`, tsc adds a line for the union's first
  member; tttt omits it. [text]
- xstate meta.test.ts 270/434: elaboration stops one level too high (at `e2:`, tsc goes on to the `meta:` under the
  `@ts-expect-error`). Not reduced. [text]
- Lesson (from the error-text work, errtextdiff): a message change can crash on real code the suite doesn't have (`ty_prop_written_name` on a
  mapped property, caught by type-fest only): run the corpora after printer/elaboration changes. [note]
- **Node reuse in error messages**: tsc's typeToString reuses syntactic nodes even without an enclosing declaration
  (a single `return x as T` prints `() => T` as written; done only for a T that names nothing,
  `type_node_names_nothing()`). tttt reuses nodes only for the types baseline
  (`d.underline && d.enclosing`); error display would need the reuse path without name-accessibility checks. [text]
## Declaration emit (`ty/declaration_diagnostics.c`; approximations in hacks.md "Declaration diagnostics")
- TS5088/TS7056 from the printer: conditional branches' unions printed inside themselves (display_branch(), tsgo's
  typeToTypeNodeOrCircularityElision) and anonymous/conditional types elided eleven deep (anonymous_elided(),
  conditional_elided(): visitAndTransformType's symbolDepth) report to the declaration tracker. Not ported: the
  general visitedTypes cycle check of every object type (tttt checks anonymous types only), type references by
  their node (a deferred reference's symbolDepth identity). Reverse mapped types print without placeholders in
  declaration emit (cycles TS5088, and the display depth limit stands for TS7056 there: hacks.md). [open]
- TS9039 "Type containing private name '{0}' can't be used with --isolatedDeclarations" (tsc's
  trackExistingEntityName, when the builder reuses a written type naming an inaccessible symbol) isn't reported. [open]

## From the 2026-10-08 hacks audit (parser, flow)
- The parser drops index signature parameter initializers (`parse_index_signature()` has no `FunctionArg` list: TS2304
  in `[a: string = b]` lost). [tsgo]
- `let o: { x?: number } | number = 1; ({ ...o } = { x: 1 });`: tsc reports TS2698 at the rest target `o` (narrowed
  to number), tttt nothing. Mechanism: the right side asks for its contextual type, which is getTypeOfExpression of
  the left pattern, i.e. checkObjectLiteral with its spread check (no TS2698 when the right asks for none: `= v as T`).
  tttt computes the assignment's context eagerly (`assignment_value_context`) and `expr_typecheck` skips the spread
  check for `assignment_pattern`, so a faithful fix needs "does this right side ask for its context" (a lazy context,
  or the list of expression kinds that ask). [open]
- A declaration file in an export context (`declare function f(): void; export function f(x: number): void;`): tsc
  reports TS2383 (overloads must all be exported or non-exported) at the exported one, tttt nothing: an export context
  exports an ambient declaration only without its own `declare` (getEffectiveDeclarationFlags), tttt's
  function_local_symbol_decls() counted every one as exported (FunctionImpl.declared_unexported). Test
  ttttDeclaredOverloadNotExported [fixed e7e95d5e7].

## Flagflip after the distributed type parameters (2026-10-09, every cell but vscode: 453 -> 141 missing)
The 141 are almost all the corpora's own default-flag misses repeated in every cell:
- vue pluginScoped.ts:190 and :235 TS7006 (`n.nodes[0].each(ss => ...)`, n a postcss-selector-parser Pseudo): what
  tsgo's eager-base runaway (TS5115 at :107, which tttt reports since 2026-10-15) leaves behind. tsgo resolves Pseudo's
  base `Container<string, Selector>`'s type arguments in its deepest round, at the depth limit, so `Selector` is the
  error type there for good and Pseudo's `nodes` is `any[]`. tttt only detects the runaway (members_reentered_unguarded()),
  its types are what the first round sees. Order-dependent in tsgo too: with the .d.ts checked first (no skipLibCheck,
  `--singleThreaded`) tsgo reports the TS5115 at the .d.ts's `interface Root extends Container<undefined, Selector>`
  instead and no TS7006. Repro: /tmp-style project with postcss-selector-parser.d.ts and `selector.each(n => { if
  (n.type === 'pseudo') n.nodes[0].each(ss => ss) })`. Emulating the poisoning means modelling which of tsgo's caches
  its deepest round fills: not planned. (vue's sloppy/nosnc flagflip cells match since 2026-10-15: a nullable
  related to a union asks for its key property first, and a function declaration's return type after its body.) [dup]
- Other diagnostics tsc reports from an evaluation order tttt doesn't have (mutation testing seed 1021, 2026-10-21,
  not attempted):
  - destructuringWithGenericParameter null-arg: "Binding elements read before the contextual assignment" (seeds 61/62). [dup]
  - recursiveTypeInference with `def?:` (zod getters, #64192): tsc's TS2345 prints `$ZodNullable<$ZodType<...>>` where
    tttt has `any` in the object's shape: the re-entrant getter inference of re-entrancy stage D (notes/reentrancy.md). [design]
  - destructuringWithGenericParameter with its `genericObject` declaration deleted: tsc's harness reports that the
    emit resolver added a TS2339 (its "pre-emit / post-emit diagnostic counts" TS-1 line); a baseline-harness artifact. [tsgo]
  - xstate/sloppy and nosnc +TS5115: gone since flagflip.py runs tttt at --checkers 1 like its --singleThreaded tsc
    cache (2026-10-20: tsgo's own TS5115 places change with --checkers). webpack/nuia UmdLibraryPlugin.js (triage.md), the -TS7006 in vue's
    pluginScoped.ts (above). [tsgo]

## TS 7.1 retarget leftovers (2026-10-09)
- **Suggestion diagnostics** (harness only, `@captureSuggestions`): TS6807 overshifts and TS6385 'x' is deprecated for
  identifier references (expr.c identifier_expr_typecheck(), 2026-10-14) are done. Not: TS6133 unused suggestions with
  noUnused* off (unused_enabled() is false then; plain JS too), TS6385 for property accesses, imports and JSX, TS6387 for
  deprecated signatures (a function's reference is decided by the call's signature in tsc: isUncalledFunctionReference),
  `@deprecated` on a declaration whose statement isn't right after the comment (declaration_deprecated() goes back over
  keywords only: the second declarator of `const a = 1, b = 2`). The CLI never prints suggestions. [open]

## Mutation seed 81 leftovers (2026-10-16, `MUTATE_DIR=/tmp/X scripts/mutate.py -n 6000 --seed 81`)
16 -> 7 differing (fixed: indexed access type's missing key resolving the object's members, never's property the
error type, mixin check without a constructor base, combined union parameters' optionality, tuple elaboration,
instanceof without a lib, function type parameters in the function's flow container, circular mapped key
constraint, TS2208 for a mapped key). Left, each looked at:
- **Binding elements read before the contextual assignment** (arrayFrom return-void, destructuringWithGenericParameter
  null-arg; known failing: test/cases/ttttDeferredBodyBeforeContextualParameters.ts; 2026-10-22 confirmed with an
  instrumented tsgo, the stack at addDiagnostic). `f(v, ({ b }) => { b; })` with `f<T>(x: Iterable<T>, cb: (t: T) =>
  void)` and `v: void` is TS2345 + TS2339 "Property 'b' does not exist on type 'unknown'" in tsgo, TS2345 alone in tttt.
  Mechanism: a generic candidate's inference pass checks the arrow with SkipContextSensitive, and
  checkFunctionExpressionOrObjectLiteralMethod's first statement queues its deferred body check (checkNodeDeferred).
  Every candidate then fails at an earlier argument, so nothing context-checks the arrow (no second inference pass;
  the reporting pass in Normal mode stops at the bad argument), and getCandidateForOverloadFailure queues the call
  (resolveUntypedCall) *after* the arrow. checkDeferredNodes runs the body first: the read of `b` asks
  getTypeForBindingElement -> the parameter's lazy type, getContextuallyTypedParameterType through the failure
  candidate (`unknown`), without assignParameterType's pattern fallback -> TS2339 (TS2488 for an array pattern, TS2700
  for a rest; for `{ g: { h } }` read through `h`, TS2339 on `g`). Then resolveUntypedCall assigns `{ b: any }`
  (assignBindingElementTypes overwrites the element: the types baseline's `b: any`, which tttt matches). Probes:
  - errors: the callback inside an object literal argument (`k(v, { cb: ({ b }) => { b; } })`), not the first
    parameter (`(a, { b }) => ...`), a `function` expression, an expression body, an element with a default, a read in a
    nested *function declaration* (checked with the body) or through `typeof b` in the body, an overload set all of
    whose candidates fail at the first argument, and `cb: (t: unknown) => void` in a *generic* function (a type
    literal there could contain type variables, so inference visits the argument).
  - no error: an element the body doesn't read, a read only inside a nested arrow (queued after the call), a
    non-generic callee (single candidate, Normal mode: the arrow is never visited before the call's deferred check),
    the arrow before the failing argument (the reporting pass context-checks it: TS2345 on the arrow), an arity failure
    (TS2554: no inference pass).
  tttt's assign_contextual_signature_as() (expr.c) always takes the `unknown` -> pattern fallback before any element is
  typed. A fix needs "inference-visited but never context-checked" for a function argument of a failed call
  (which candidates ran inference, where each failed, F's argument index) plus "elements its own body reads":
  a reconstruction of tsc's deferred-check order, declined 2026-10-22 (two mutants, no real-code sighting). Do it
  when a real project shows it. [tsgo]
- **TS2589 through a constraint check** (infiniteConstraints2 extends-object: `T extends Object[]` instead of
  `unknown[]` in Prepend): tsgo reports instantiation depth at `Prepend<T, Conv<...>>` (line 13), tttt nothing. The
  constraint check of `Conv<ExactExtract<U, T>, {a: number}>` against `Object[]` expands Conv recursively in tsgo;
  `unknown[]` (and `any[]`) short-circuit. Not looked into further: which relation path recurses in tsgo. [open]
- **TS2310/TS2506 from a class expression base checked during base resolution** (recursiveClassBaseType
  return-void): `class Derived1 extends class extends Base1 { root() { return; } } {}` with `abstract root():
  Derived1` in Base1: tsgo's checkClassExpression checks the expression's members (root's `void` against
  `Derived1`, which needs Derived1's structure) while Derived1's base types resolve: circular. `return undefined as
  any` relates to anything without resolving Derived1. tttt checks the class expression later, outside the
  resolution. Architectural (eager checks inside type resolution). [design]
- **Instantiated type parameter constraint printed through its declaration node** (text TS2322,
  contextualTypesNegatedTypeLikeConstraintInGenericMappedType2 never-const): `const matcher: never = typeTags<Value>()`
  prints P's constraint as `{ readonly [Tag in Tags<"_tag", Value> & string]: (_: Extract<Value, ...>) => any; }` in
  tsgo (the written node reused with I mapped to Value), resolved members in tttt. Printer node reuse, the type-display work
  territory (displaydiff's buckets). [text]
- **Spurious TS2208 for `null extends T ? K | null : K`** (render, conditionalContextualReturnSubstitutionCache
  drop-as): `function g<T, K>(value: T): null extends T ? K | null : K { return value; }`: tsgo gives no "This type
  parameter might need an `extends ...` constraint", tttt does. `string extends T ? K | null : K` and `null extends T ?
  K : K` have it in both, so tsgo's hasNonCircularBaseConstraint of the synthetic parameter must find a circularity
  only with a null check type and null in the true branch. Not found where (getConditionalType's `null extends T'`
  with T' unconstrained while its constraint is assigned?). [open]

## Mutation seeds 61/62 leftovers (2026-10-14: `MUTATE_DIR=/tmp/X scripts/mutate.py -n 4000 --seed 61|62`)
Seed 61: 11 -> 5 differing, seed 62: 11 -> 8 (both before the last fixes' rechecks of each other). Left, each looked at:
- (cor8 2026-10-28, seed 62 rerun: 4 differ) missing TS7005 in noImplicitAnyUnionNormalizedObjectLiteral1 return-void:
  `{ p: null, s: null, ...f() }` (strictNullChecks off) with f's object replacing both properties is TS7005 with the
  whole type in tsgo (checkObjectLiteral passes the written members' ContainsWideningType to getSpreadType; nothing in
  the result reports TS7018). tttt took the flags of the final properties only. SpreadAcc.own_widening ->
  OBJECT_CONTAINS_WIDENING -> TyAnonymous.spread_widening. Test ttttSpreadOverwrittenWidening. [fixed 1faa449eb] The
  other three: unicodeLineSeparatorInMultilineComment never-const (the errors.txt rendering of a U+2028 line, harness
  text), restElementWithInitializer2 (the lazy name resolution entry below), classVarianceResolveCircularity2
  swap-stmts (missing TS7022, the circularity family). [text]
- **Spurious circularity on getters** (seed 62, not the satisfies one, which is fixed): TS7022/TS7023/TS2502 in
  recursiveTypeInference add-optional (`_zod?:` on line 28, `$ZodString`'s), TS2339 in
  noCircularitySelfReferentialGetter2 drop-as. Not looked into; not the expression-memo bail (unchanged with
  b2c2a214c's fix, 2026-10-22). [open]
- seed 61 destructuringWithGenericParameter null-arg: "Binding elements read before the contextual assignment" above. [dup]
- seed 61 restElementWithInitializer2 delete-stmt (x2): the seeds 41/42 "Lazy name resolution" entry below. [dup]
- seed 62 contextualTypesNegatedTypeLikeConstraintInGenericMappedType1 undefined-init (missing TS7034): the seeds 41/42
  `typeof x` implicit-any entry. [dup]
- seed 62 conditionalContextualReturnSubstitutionCache drop-as (render): tttt adds the related TS2208 "This type
  parameter might need an `extends C` constraint" for `T` against `null extends T ? K | null : K`; tsgo doesn't when a
  branch mentions another type parameter (K, constrained or not), but does for `null extends T ? string : number`.
  hasNonCircularBaseConstraint must find the synthetic parameter's constraint circular through K somehow; not found.
  relate.c report_missing_constraint(). [dup]
- seed 62 localTypeParameterInferencePriority delete-stmt and extends-object (spurious TS2322): without the
  `__schema!: S` property, `typeof ColumnSelectViewImp` to `new <S>() => Table<UnrollOnHover<S>>` fails in tttt
  (S' inferred as Schema, the generic `getRows<C extends keyof S>` the only mention of S); tsgo infers through it.
  Inference, not reduced further. (cor8 2026-10-28: the delete-stmt mutant and every `Object` constraint variant
  match tsgo now.) [fixed]
- seed 62 recursiveTypeInference drop-prop (TS2741 text): the source object literal prints `posts: []` in tsgo,
  `posts: never[]` in tttt (an empty array literal against a circular `any[]` target); not looked into. [text]

## Mutation seeds 41/42 leftovers (2026-10-12: `MUTATE_DIR=/tmp/X scripts/mutate.py -n 4000 --seed 41|42`)
Seed 41: 10 -> 7 differing, seed 42: 13 -> 5. Left, each looked at:
- (cor8 2026-10-28, seed 42 rerun: 4 differ) escapedIdentifiers member-typo: a missing property written with escapes
  (`o.argX\u0033`) is printed as written in TS2551/TS2339 (reportNonexistentProperty's declarationNameToString),
  tttt printed the cooked name (report_no_property). Test ttttMissingPropertyEscapedName. [fixed ee2218c06] The other
  three are the entries below (TS2310 design, lazy name resolution, TS7022 circularity). [note]
- **TS7034/TS7005 for an auto-typed `let x = undefined` read through `typeof x` in type parameter positions**:
  fixed for a function's (signature's) type parameters by seed 81 (they're in its flow container); a type alias's
  `<X extends typeof a>` and `<X = typeof a>` say nothing in tsgo nor tttt (2026-10-16 probe). [fixed 2026-10-16]
- **Lazy name resolution** (seed 41 restElementWithInitializer2 delete-stmt): `[...x = a] = a` with x undeclared: tsgo
  only reports TS2304 for x when something resolves it (it's TS1186 and unchecked; the post-emit pass or a later flow
  walk through the assignment reports it); tttt reports every unresolved name at bind time. Suppressing it at the
  rest element makes `[...x = a] = a; [...a = y] = a;` wrong instead (tsgo reports x there, reached by the flow walk
  of the second statement). Order-dependent in tsgo. [open]
- **TS2590 where an intersection is first normalized** (seed 41 normalizedIntersectionTooComplex ann-string-number):
  tttt also reports it at the call (36,14), tsgo only at the callback parameter. Which node is current when the
  too-complex intersection is first built; tttt may build it twice. [open]
- seed 41 noCircularitySelfReferentialGetter2 str-to-num: a getter's circular type prints `any` in tttt's message,
  `ZodOptional<ZodType>` in tsgo (circularity resolution order). [text]
- **Missing circularity errors** (seed 42): TS4109 "Type arguments for 'Array' circularly reference themselves" when a
  conditional `P extends Object` (was `void`) relates `ReducerAction[]` while ReducerAction's alias is being resolved
  (recursiveArrayNotCircular extends-object); TS7022/TS7024 when a reduce callback returns the variable it
  initializes (unknownSymbolOffContextualType1 swap-stmts). See "Circularity tsc finds and tttt doesn't". [open]
- seed 42 objectRestNegative undefined-init (strictNullChecks off): `let o = undefined; var { ...m, a } = o` is TS2700
  in tsgo when the pattern has another element (the initializer isn't widened to any under the pattern's contextual
  type), not with the rest alone; tttt reports neither and then TS2322 instead of TS2741 for `({ b, ...na } = o)`.
  Re-probed 2026-10-28: TS2700 now goes with TS2462/TS1186 on the element (binding.c checked it only when the grammar
  was fine), and the rest of null or undefined alone is `{}` (lower.c object_rest_type(), tsc's getRestType filter):
  TS2739 at the assignment. Tests ttttObjectRestNotLastNonObject, ttttObjectRestOfUndefined. [fixed ebd4af3a4]
- seed 42 signatureCombiningRestParameters1 add-optional: a union of signatures one of which has a rest parameter of
  type `[string] | undefined` (TS2370): tsgo reports a spread argument as TS2556, tttt said TS2345 to never. Not the
  combination: tsgo's findMatchingSignatures matches `(...args: [number])` with it (tryGetTypeAtPosition indexes the
  rest with getIndexedAccessType, no property in undefined: unknown, of which number is a subtype), so the union's
  signature is the `[number]` one's (TS2556 by hasCorrectArity, TS2554, `number`); tttt's param_type_at indexed the
  union with ty_access (string). Now ty_indexed_or_null. Test ttttUnionSignatureNonTupleRest. Left: `f2(1, 2)` on the
  `[string] | undefined` rest alone prints `[1, number]` where tsc has `[1, 2]` (getSpreadArgumentType's contextual
  type of each argument, getIndexedAccessType(rest, i, Contextual), keeps the literals: what tsc's contextual type is
  there wasn't traced). [fixed da53edfc0]

## Mutation seed 2026102001 (2026-10-20, -n 3000: 5 / 1552 differ)
Fixed: a computed key naming no property against a generic mapped contextual type, recursive-return skip with type
arguments / through exported variables. Left:
- arrayFrom return-void (the binding-element entry above). [dup]

## Mutation seed 2026102101 (2026-10-20, -n 8000: 10 / 4102 differ)
Fixed: a property symbol an accessor joins (merged interfaces/classes, an auto accessor after an interface property)
typed by its accessors without the property's optionality (TS2717/TS2687/TS2564 against them; TS2687 compares with an
accessor as the first declaration); a known-key object with a generic property no longer relates to a generic mapped
type through the member fallback (`{ type: T }` to `Partial<X>`, `Partial<C<T>>` of a deferred conditional). Left:
- declarationEmitNestedAnonymousMappedType extends-object: `M[k] extends Object ? M[k] : never` related to `string |
  number | symbol` by the deep-nesting cut-off of `conditional_source_related()`'s default constraint step (hacks.md:
  removing it costs react +82% instructions and gives largeTupleTypes a spurious TS2799). [open]
- complicatedIndexedAccessKeyofReliesOnKeyofNeverUpperBound add-optional: the TS2322 now comes, at the `return` where
  tsgo puts it at the shorthand `type` of the returned literal (tsgo's hasExcessProperties moves r.errorNode to the
  excess property during a reporting pass, then a later failure reports there; not reproduced). [dup]
- jsonRawJSON2 ann-string-number (also seed 4's missing TS2430): harness only, the shared lib program's declaration
  order (harness.md "Shared lib programs"); the CLI reports it. [dup]
- Known families: unknownSymbolOffContextualType1 (TS7022 circularity), conditionalContextualReturnSubstitutionCache
  (TS2208 render), restElementWithInitializer2 (lazy TS2304). [dup]

## Mutation seed 2026102102 (2026-10-20, -n 8000: 10 -> 8 / 4003 differ)
Fixed: TS2678/TS2352 heads name the normalized pair like TS2322's (`1` against `string | undefined` is `number` to
`string`); a union's combined signature is declared by the member signature it was made from (TS2771 "The last overload
is declared here" for `(string[] | number[]).splice`). Left, each looked at:
- **A const's name from its initializer when its annotation gives none** (typeGuardNarrowsIndexedAccessOfKnownProperty7
  and unionTypeWithIndexSignature never-const: `export const key: never = Symbol()` then `[Foo.key]: string` /
  `this[Foo.key] = ...`): tsgo's tryGetNameFromEntityNameExpression falls back to the initializer's type (a unique
  symbol) when the annotation (never) isn't usable as a name, so `this[Foo.key]` still matches the member for
  TS2564's flow check and `both[sym]` reads the `[sym]` property. tttt goes by the declared type only
  (field_initialized_in_constructor()). Only with a wrong annotation; not done. [open]
- generatorTypeCheck46 member-typo: TS2488 twice in tsgo, once for the yield* operand typed in the context-sensitive
  pass (`{ [x: number]: {} }`, the method skipped) and once final; tttt reports the final one (the "errors of an early
  pass stay" family). [design] (cor10: presence matches, only tsgo's second text differs; a per-diagnostic link like
  65e561e0b's TS2536 would be a point hack, the general fix is reporting what inference passes evaluate)
- escapedIdentifiers member-typo: tsgo prints `argX\u0031` (escaped) in TS2551 only when another escaped identifier with
  the same unescaped text appears earlier in the file (`arg\u0031 = 1` in a method); alone it prints `argX1` like tttt.
  A tsgo text quirk, not mirrored. [tsgo]
- unicodeLineSeparatorInMultilineComment never-const: tsgo's CLI reports the TS2322 like tttt; its test harness writes
  no errors baseline for that file. Not a tttt bug. [tsgo]
- Known: conditionalContextualReturnSubstitutionCache
  (TS2208), complicatedIndexedAccess... (position, seed 2026102101). [dup]

## Mutation seed 1022 (2026-10-21, cor2, -n 8000: 8 -> 6 / 4185 differ)
Fixed: misplaced imports/exports' alias errors (TS2305/TS2307) reported when a spelling suggestion resolves them
(SymbolRare.unreported_errs); lone surrogates and line-breaking characters escaped in template literal type text. Left,
all known: arrayFrom return-void, recursiveClassBaseType return-void (TS2310), unicodeLineSeparatorInMultilineComment
never-const (harness writes no errors baseline).

## Mutation seed 2310 leftovers (2026-10-23, `MUTATE_DIR=/tmp/X scripts/mutate.py -n 8000 --seed 2310`: 10 -> 5 / 4008)
- **missing TS2339 through deferred-check order** (arrayFrom delete-stmt and return-void: `getEither` returns void, so
  `Array.from(inputARand, ({ a }): B => ...)` fails on the first argument). tsgo's only report is the deferred one: the
  arrow is queued by checkNodeDeferred during the skip-context-sensitive inference pass, ahead of the failed call (queued
  by getCandidateForOverloadFailure). Its body check asks for `a` before resolveUntypedCall context-checks the arrow,
  so getTypeForBindingElement reads the parent from getContextuallyTypedParameterType, the failure candidate's
  `unknown`, and reports `Property 'a' does not exist on type 'unknown'`. Once context-checked, assignParameterType's
  unknown fallback gives `{ a: any }`, which is what both print. The non-generic `n(v, ({ d }) => d)` has no error in
  tsgo (no inference pass, so the call is queued first). Cut down: `declare const v: void; declare function k<T>(x:
  ArrayLike<T>, g: (t: T) => void): void; k(v, ({ d }) => d);`. Matching it means modelling tsgo's deferred order.
  It's an extra error on an already-failing call. [dup]
- **missing TS2536 from declaration emit** (arrayFakeFlatNoCrashInferenceDeclarations str-to-num, `@declaration`):
  tsgo reports two TS2536 at the alias's type literals (positions inside `BadFlatArray`) while serializing `foo`'s
  inferred type, in place of the original TS5088. [open]
- **position TS5115** (recursiveConditionalCrash3 extends-object): the "infinitely circular" error is one line off
  (where the runaway is first met). [open]
- **position TS2322 from a sticky errorNode** (complicatedIndexedAccessKeyofReliesOnKeyofNeverUpperBound add-optional):
  tsc reports the whole-object message at the `type` shorthand property. An excess-property check on a member of the
  target intersection (`type` is excess for `Partial<Omit<..., "id" | "type">>`) moved the relater's errorNode there;
  that message was later replaced by the outer chain but the node wasn't restored. tttt models the moved node only when
  the excess-property message itself is reported. [open]
- **elaboration TS2344** (distributiveConditionalBaseConstraint drop-optional): the chain differs. [text]

## Mutation seeds 20261106 and 20261107 (2026-10-28, cor9: `MUTATE_DIR=... scripts/mutate.py -n 8000 --seed S`)
8 and 9 of 8000 differ (21 and 24 more with a new syntax error, `--syntax`: bun10's). 3 `render` each.
- constEnumErrors num-to-str: `E2["1"]` on a const enum is a string (the reverse mapping's index signature, const
  enums too). Test ttttConstEnumNumericIndex. [fixed b1ed44b9e]
- distributiveConditionalBaseConstraint extends-object: spurious TS2344, tail recursion's fresh deferred type
  (type_core.md). [design]
- generatorTypeCheck46 member-typo: tsgo reports TS2488 twice at one `yield*`, once from the inference pass (the
  object literal's method typed `{}` while context-sensitive) and once finally; tttt only the final one. The family of
  diagnostics made by an inference pass (like c3ffa3e8f's TS2536, rebased 65e561e0b), here with another type text.
  [design] (see the seed entry above)
- symbolProperty18 member-typo: `var str = i[Symbol.toStringTagX]()` is TS7022 for str in tsgo because printing the
  object type for TS7053 runs getSymbolChain's getWithAlternativeContainers, which types every value symbol in scope
  (looking for a variable of the container's type, `Symbol` for SymbolConstructor) and reaches str in flight. tttt's
  printer knows only the `XConstructor` pattern (out_unique_symbol_name_impl's HACK, hacks.md) and types nothing.
  Emulating it means the printer resolving declarations' types. [design]
- distributiveConditionalTypeConstraints ann-string-number (both seeds): tsgo's elaboration repeats the conditional
  source's line before `Type 'string' is not assignable to type 'number'`. [text]
- With `--syntax` (new parse errors in tsc's baseline, but checker divergences): fixed nonPrimitiveConstraintOfIndexAccessType
  (never[Object] has no write constraint, d9db3cc43), bigIntWithTargetES2016 (compound assignment to an only-nullable
  operand, f22a892bf), genericCallInferenceConditionalType2 (a failed call's extra arguments aren't typed while it
  resolves, e4270d220), newTargetNarrowing (meta-property names, 5806eba5e), controlFlowForStatementContinueIntoIncrementor1
  (arithmetic on a loop's partial types, 0fb0bee9d, 800f0c685), importCallExpressionGrammarError (spread specifier, 1847baef9),
  contextualReturnArrayPatternCallback (TS1338 is a grammar error, 0540d6753), propertiesAndIndexersForNumericNames
  (implicit any only at the first declaration, c40370dd6), nestedExcessPropertyChecking (a shorthand initializer is
  typechecked, 355bf63a4), recursiveTypeInference (a shorthand name with an initializer isn't a use, d9f492fd3).
  [fixed] Left: recursiveTypeInferenceReturnTypeConstraint (missing TS2615, the history family),
  substitutionTypesInIndexedAccessTypes (text TS2322), octalIntegerLiteralES6 (TS2551->TS7053), parser-side ones for
  bun. [open]
- exportSpecifierReferencingOuterDeclaration3 str-to-num (`declare module 42 {` parses as a block of a script, whose
  `export { X }` aliases X to itself): tsgo's errors baseline lacks the TS2694 because tsgo dies of a stack overflow
  (getDeclaredTypeOfAlias <-> tryGetDeclaredTypeOfSymbol on the self-referential alias) right after. [tsgo]
- recursiveClassBaseType delete-stmt: missing TS2310 (the base cycle design). noCircularitySelfReferentialGetter2
  (TS2345 text), unionTypeWithIndexSignature never-const (TS2322 text): not looked at. [open]

## Mutation seed 20261108 (2026-10-29, cor10: `scripts/mutate.py -n 8000 --seed 20261108`, 3 of 8000 differ)
- varianceAnnotationValidation delete-stmt: a wrongly annotated `Controller<out T>` met while measuring
  AnimalContainer's variance was taken as a marker type (is_marker_reference recognized any single-marker
  instantiation), related structurally, AnimalContainer came out bivariant and `cd = ca` lost its TS2322. tsgo makes no
  marker for an annotated parameter. Test ttttAnnotatedVarianceMarker. [fixed eab70b013]
- duplicatePropertyAndAccessor add-optional: `y?: number = 2; accessor y: number = 3;` is one symbol in TS 7, typed by
  its accessors (getTypeOfSymbol), so the auto-accessor isn't TS2717; tttt compared it with the first field's type.
  ty_check_class_duplicate_members. Test ttttAccessorTypedPropertySymbol. [fixed c61457fba]
- controlFlowGenericTypes swap-args (text TS2345): a union-of-tuples rest parameter's argument contextual type is
  mapped without reductions (`string | unknown`), so the literal 0 stays `0` in the spread tuple. rest_arg_contextual.
  Test ttttRestUnionTupleLiteralArg. [fixed b31156189]

## Mutation seed 2811 (2026-10-28, cor8: `scripts/mutate.py -n 8000 --seed 2811`, 9 of 8000 differ)
- classDoesNotDependOnBaseTypes ann-number-string: writing `x[0]` on `string | StringTreeCollection` (a string index
  signature) is TS7053 in tsgo: the union has an index signature only for the key types its first member has and every
  member has exactly (getUnionIndexInfos), none here; tttt's write check took any member's applicable readonly one
  (String's) and said TS2542. ty_union_index_info (access.c) now. Test ttttUnionWriteIndexExactKeys. Left: `string |
  T` with T constrained to a string index misses that TS7053 (generic member). [fixed c84360cab]
- reachabilityChecksNoCrash1 never-const: parser recovery turns `out: never = []` into a shorthand `never = []` in an
  object literal; tsgo's checkShorthandPropertyAssignment checks only the initializer of such a shorthand outside a
  destructuring target, so its name, a type keyword, is no TS2693 (nor a namespace TS2708). Test
  ttttShorthandDefaultNameUnchecked. [fixed 3e1405428]
- renamingDestructuredPropertyInFunctionType3 delete-stmt: TS2304 for a computed name in a function type's parameter
  pattern is found only by tsgo's emit resolver (the harness's pre/post-emit count mismatch); tsgo's CLI reports it
  like tttt. [tsgo]
- assignmentGenericLookupTypeNarrowing str-to-num: `foo(mappedObject[key])` with `K extends 42` is TS2536 from the
  generic call's inference pass (no constraint substitution in an inferential check); the final check's type is the
  constraint. The walk now also reports an access any evaluation found invalid (KEY_INVALID_GENERIC_ACCESS). Test
  ttttInvalidGenericAccessInInference. [fixed 65e561e0b]
- contextualTypeWithUnionTypeIndexSignatures ann-number-string: findMostOverlappyType keys `{ 1: x }` by the number
  literal 1, which a number index overlaps; tttt used the string "1". Test ttttOverlappyNumericKeys. [fixed dd9e251d5]
- Not looked at: recursiveTypeInference add-optional (TS2345 text), circularBaseTypes delete-stmt
  (missing TS2310, the base cycle design), nonPrimitiveAndEmptyObject drop-optional (TS2339 names the narrowed `{}`
  in tsgo, the union in tttt). [open]
- ambientExternalModuleWithRelativeExternalImportDeclaration delete-stmt: `declare module "M" { import m2 =
  require("./Sub"); }` with m2 unused is only TS2439 in tsgo: the program doesn't resolve a relative name in an ambient
  module (collectExternalModuleReferences) and checkExternalImportOrExportDeclaration returns after TS2439, so TS2307
  comes only from resolving the alias on a use. Imports now go by PortItem.resolve_on_use (the namespace TS1147
  mechanism: reported on a use, in the import's own file). Test ttttAmbientRelativeImportOnUse. [fixed bf2032fbe]
- Left from it: a re-export of a relative name in an ambient module (`export { b } from "./x"`, `export *`) is TS2307
  in tsgo only when something resolves the module's exports (checkExternalModuleExports' hasExportedMembersOfKind
  under an `export =`, an importer); tttt reports it eagerly (binder_link resolves every alias). Rare: needs a kept
  error flushed by those resolutions, like a misplaced import's SymbolRare.unreported_errs. cor10 (2026-10-29): tsgo's
  report is history-dependent across files (diagnostics are collected per file right after its check, so an importer
  checked before the declaring file loses it, one checked after reports it), and per name (`export *` on any walk of
  the module's exports, `export { r }` only when r itself resolves). Eager matches the importer-after case; anything
  K-stable differs somewhere. [design]

## Mutation seed 2810 (2026-10-28, cor8: `MUTATE_DIR=/tmp/X scripts/mutate.py -n 8000 --seed 2810 --tests multi,js`)
27 of 8000 differ: 9 render, the rest below or known (commonJSAliasedExport/moduleExportAlias5 never-const, importTag14,
umd7, typeTagNoErasure, importHelpersES6: the seeds 2311/2312 entries).
- augmentExportEquals3_1/4/4_1 delete-stmt (text TS2339): an augmented `export =` printed in a message: through a
  namespace import's clone tsc names it by the augmentation as written (`typeof import("./file1")`: the clone isn't
  its file's export=, getSpecifierForModuleSymbol falls to IsModuleWithStringLiteralName), and an ambient module's
  `export =` likewise (`import("file1")`, its export= container is no file); tttt printed the first declaration's
  file. display_module_clone, out_symbol_name_impl. Tests ttttAugmentedExportEqualsModuleName,
  ttttAugmentedExportEqualsAmbientName. [fixed c6ddc2f74]
- jsdocParamTagTypeLiteral member-typo: `@param {object[]} opts5` with children `opts5[].what`, `opts5[].whatX.bad`
  (a grandchild under a wrong middle name), `opts5[].unnest`: tsgo's parseChildParameterOrPropertyTag accepts a child
  only when its name's left is exactly the parent's, so the wrong grandchild ends `what`'s and `opts5`'s nesting both:
  every later `@param` is top level (TS8032 for each, names printed without `[]`), and `opts5[0].unnest` is TS2339.
  tttt keeps nesting and prints the names with `[]`. JSDoc parser (bun's area). jsdoc_nested_count(),
  jsdoc_entity_name_text() [fixed 9bb350a0d]
- jsdocTypeTagFunctionTypePredicate delete-stmt: TS2355 for a JSDoc `@type {(e) => e is X}` function expression
  without a return is at the `@type` tag's type (8,27) in tsgo, at its return type (8,43) in tttt: tsgo's errorNode is
  the FullSignature the reparser set; function_impl_check_end now takes jsdoc_signature_span when the return type
  came from it. Test ttttJsdocSignatureMissingReturn. [fixed 87797caaa]
- bigintArbirtraryIdentifier drop-as: TS2460's "exported as" name is symbolToString's, quoted when it isn't an
  identifier (`'"0n"'`). Test ttttExportedAsStringName. [fixed 93f98625d]
- libTypeScriptSubfileResolvingConfig ann-string-number: a spurious related TS6500 into a libReplacement's file
  (`@typescript/lib-dom`), which tsgo's IsSourceFileDefaultLibrary counts as a lib (relater.go skips the related info).
  SourceFile.is_lib_replacement, check.c in_lib(). Test ttttLibReplacementNoRelatedInfo. [fixed 30d3f5e30]
- Text leftovers of the seed, not looked into further: checkJsdocTypeTag6 never-const (a JS `: never` annotation next
  to `@type {() => void}`: tsgo gives the function expression the tag's signature then, `() => void`);
  declarationsForIndirectTypeAliasReference ann-string-number (TS2353 names `Hash<string>` for an empty interface
  `StringHash2 extends Hash<string>` with a number index); nodeNextCjsNamespaceImportDefault1 member-typo (`typeof
  import("src/a")` against tttt's `typeof ns`); declarationEmitWithInvalidPackageJsonTypings never-const (`typeof
  import("csv-parse")` against a path); importTypeLocal (a TS6500's position and `typeof import("/.src/foo2")`). [text]

## Mutation seeds 2311/2312 leftovers (2026-10-23, `MUTATE_DIR=/tmp/X scripts/mutate.py -n 8000 --seed 2311|2312 --tests multi,js`)
The multi-file and JS mix finds far more than the single-file one (51 and 37 against 10). Seed 2311: 51 -> 37 -> 30
after the second batch, seed 2312: 37 -> 28. What is left is mostly text, render, position and elaboration buckets.
Diagnostics-class leftovers, not fixed:
- missing TS2339 on `module.exports.funky = ...` after `module.exports = donkey` with `donkey: never`
  (commonJSAliasedExport never-const; moduleExportAlias5 likewise). [open]
- missing TS2775 only when the asserting call also has an arity error (assertionTypePredicates2 add-arg): the
  assertion check read only a fitting resolution, tsc's resolvedSignature is the failure candidate then
  (ty_call_failure_signature()). Test ttttAssertionCallArityError [fixed 7e073fa17]. Left: several overloads, whose
  failure candidate is createUnionOfSignaturesForOverloadFailure's (the first overload's declaration, so its
  predicate, and the intersection of the returns), which tttt doesn't keep as the call's failure_sig. [open]
- TS2303 around an unresolved UMD `export = Thing` + `export as namespace Foo` (umd7): whether tsc sees the cycle
  depends on check order (the spelling suggestion for Thing resolves Foo, in flight, only if no file used Foo first);
  tttt resolves aliases eagerly at link and always reports it. Its position is fixed. [open]
- spurious TS2451 in crashDeclareGlobalTypeofExport delete-stmt (tsgo reports nothing at all there, not even for the
  unresolved `export = root`); TS2502 -> TS2451 in umdNamespaceMergedWithGlobalAugmentationIsNotCircular delete-stmt. [open]
- missing TS2322 in moduleResolutionWithSymlinks_notInNodeModules never-const: the same file under two symlinked paths. [open]
- missing TS1005/TS2306 for a broken JSDoc `@import * from "./foo" with` (importTag14 drop-as): tttt's pre-scan of the
  tag (jsdoc_import_statement, parser.c) bails out at the first error where tsgo's parser recovers and resolves. [open]
- missing TS1005 at the same position as a TS1124 (parser.numericSeparators.decmialNegative): tsgo's dedupe compares
  with the last error, which depends on emission order. [open]
- spurious TS2354 position (importHelpersES6 delete-stmt): which syntax asks first for the missing tslib. [open]
- missing TS2719 when a declaration has both a TS annotation and a JSDoc @type (typeTagNoErasure never-const): tsgo
  still contextually types the initializer by the @type. [dup]
- With a non-string import attribute value, tsc resolves (and reports) the bindings only when something uses them;
  tttt approximates by a text scan for the binding names (hacks.md). [open]
- Seen while fixing: `/** @type {} */ var w = 1` types w as any in tsgo (TS1110 there), number in tttt. The tag now
  has tsc's missing type (parse_jsdoc_type_expression), and a missing type node lowers to the error type (so `x
  satisfies <missing>` is any, TS and JSDoc), a type running into the comment's end is kept. Test ttttJsdocEmptyType.
  [fixed 8fbeb9b4d] Left, JSDoc parser (bun's area): `/** @type {\n */` and `/** @type { */` miss tsgo's TS1110 at the
  newline/whitespace token after `{` (tsgo's JSDoc scanner makes whitespace a token); `/** @type {\n * number} */` is
  `*` (JSDocAllType) then '}' expected at `number` in tsgo, number without errors in tttt (the continuation's `*`
  skipped) [fixed 25b84631b: parse_jsdoc_type_expression, test ttttJsdocTypeBraceAtEnd]. Text: `{ x: 1 } satisfies ;`'s types line prints `satisfies ` with a trailing space (the missing type's span
  starts after the whitespace; tsc's ends at the keyword). [open]
- Seed 2313 (multi/js, 40 / 3977): the stop seed, two new diagnostics-class divergences. The harness's TS5024 location
  in an extended config is fixed. Not fixed: tsgo's doubled TS2304 next to its TS2552 for `export { StringHash }` of a
  missing name under `@declaration` (declarationsForIndirectTypeAliasReference delete-stmt), most likely the
  declaration emitter resolving the name again without a suggestion (dts_emit.md's area). [open]
- **For-initializer DisallowIn leaks into nested constructs in tsgo** (corF 2026-10-23, bun 83-R01c,
  test/cases/ttttForInitializerDisallowInLeak.ts). tsgo's `for (` initializer is parsed in the DisallowIn context, and
  the flag stays set inside arrow parameters, array literals and nested function and class bodies: only
  parseExpressionAllowIn's sites, arguments, object literal values, conditional true branches, binding patterns,
  property and enum initializers and nested variable declaration lists clear it. Now ParserShared.disallow_in, cleared
  with allow_in_enter() at those sites; function bodies aren't deferred under it. [fixed 13c2bc9bf]
- **A for-in/of declaration's initializer is never checked** (bun9 2026-10-28, bun 83-R01c's remaining errors). tsgo's
  checkVariableLikeDeclaration checks it: a for-of declaration's initializer against the declaration's type, a for-in
  one's only when the declaration is a secondary declaration of the variable (against `string`, at the name), and
  checkExpressionCached runs then (TS7006 in an arrow initializer). `var i = ""; for (var i = [] in b) {}` is TS1189 and
  TS2322 there, tttt has only TS1189; `for (var k = (a) => 1 of b) {}` misses TS7006. statement.c's for-each check
  (FOR_EACH_LOOP_STATEMENT) never looks at the initializers. Only reachable after a grammar error (TS1189/TS1190) or a
  parse error. for_each_loop_check_initializer(), test ttttForEachInitializerChecked [fixed 5a52516f7]

## Lib files conflicting with each other (2026-10-23, corF: an old-tsconfig probe, `lib: ["dom", "webworker"]`)
- **Conflicts between two lib files went unreported on the later lib's side** (tsgo reports 514 diagnostics on a
  `dom` + `webworker` config, tttt reported 269 of them, so a project tsgo fails still failed, but with half of the
  diagnostics). There were two mechanisms, both treating a lib file as never checked:
  - report_file_conflict() (scope.c) reported the merge's source side (the later file's declarations) without
    `lib_too`. TS2300/TS2451 for lib.webworker.d.ts's declarations were then dropped, and only lib.dom.d.ts's
    were kept.
  - interface_decl.c's once-per-symbol member check (ty_check_object_type_duplicate_members: TS2717 for a property
    declared again with another type) chose the first non-lib declaration. So an interface declared only in libs
    (GPUExternalTextureDescriptor, RTCDataChannel...) was never checked.

  Both now count a lib file as checked when lib files are (lib_files_checked(): neither skipLibCheck nor
  skipDefaultLibCheck), as tsgo does. Repro: test/cases/ttttLibDomWebworkerConflicts.ts. [fixed 2026-10-23]
