# Missing and wrong checks (the suite's long tail)
The work queue for the suite checker bugs: diagnostics tttt doesn't report, or reports under another code, and
the failing tests behind them. Find fresh buckets with `scripts/failstats.py` (single-code tests: errors fail on one code
alone in `build/failstats/tests.tsv`) and `scripts/unimplemented_codes.sh` (codes `src/` never mentions). Every test named
here fails on the tsgo 7.1 suite (pruned 2026-10-09; harness-only causes are in `harness.md`, type-core causes in
`type_core.md`). To see what tsgo does inside a check, instrument it: `agents/notes/instrumenting_tsgo.md`. Entries naming a mutant (`mutNNNN`, "X delete-stmt") describe a mutation of a test that itself passes (pruned again 2026-10-16 by probes and hand-made mutants).

## Known missing checks
- **JS**: TS2565 for a property declared by a JS expression statement (`/** @type {F} */ C.prototype.m;`) read in the
  same flow container (tsc's getFlowTypeOfAccessExpression with assumeUninitialized); TS7032 for a setter whose type
  comes from `@type` (probe plan19/webpack/jsdoc_accessor_type.ts).
- **Decorators**: TS2449 for a class used in the decorators of its own methods' parameters (tsc counts them; tttt's
  check only knows class and member decorators). In tsgo it comes from the emit resolver only (`m(@dec(C.s) x)`: the
  suite's TS-1 pre/post-emit count note), so it shows in baselines, not on the CLI.
- **Classes**: TS2687 skips statics and method/accessor pairs (hacks.md).
- **Accessibility**: `this.x = v` in the constructor of a class merged with an interface declaring `readonly x` (tttt
  exempts any property whose parent is the class). Private names: TS18015.
- **Declarations**: `export function g() {}` + `function g() {}` is TS2395 here, TS2393 in tsc.
- **Implicit any**: widening errors past `report_errors_from_widening` (binding.c; the tests the tsc mechanism ports named for them
  pass since, re-check with a probe: TS7008 class fields, TS7018 through a spread) and of generator yield/next types.
- **Target-gated**: TS2705 comes only from an inferred return type's demand (tsc's createPromiseReturnType), and tttt
  infers more eagerly than tsc, so the two can disagree.
- **Iteration**: TS2489 (iterator without `next`), TS2763-2765 (iterator next-type mismatches);
  generator next types aren't inferred from `yield` uses (tsc's `nextTypes`).
- **Modules**: exports in non-module containers dropped silently (`scope_add_port()`, TODO TS1194); `import { type
  require }` isn't type-only for TS2441; a nested
  `declare module "m"` gets TS2435 but is still bound as a global ambient module (tsc: a local namespace).
- **Variance annotations**: `type B<in T, in T> = { a: T }` misses tsgo's TS2636 at the second `in T` (the two are
  one type parameter symbol; why only the second check fails in tsgo wasn't looked at). Probe only, no test.
- The jsxImportSource runtime import (`jsx_runtime_module()`) reports TS2875/TS2792 and TS1479/TS1471, not the other
  resolveExternalModule diagnostics (TS7016 for a JavaScript runtime).
- `while (a) const x = 1;` still scopes x to the loop (tsc: the enclosing block).

## Mutation seed 2 leftovers (2026-10-07, `MUTATE_DIR=... scripts/mutate.py -n 8000 --seed 2`: 32 -> 13)
Each one mutant:
- arrayFrom return-void: "Binding elements read before the contextual assignment" (seeds 61/62 below).
- interfaceExtendingOptionalChain extends-object: `interface C1 extends Object?.Bar {}` is TS2499 *and* the name
  resolved as a type reference (TS2702 'Object' only refers to a type...; TS2503/TS2694 for others): tsc's
  getTypeReferenceName takes the optional chain as an entity name (isEntityNameExpression ignores `?.`). tttt keeps
  it as an invalid expression (HeritageClauses.invalid) without a TypeNode, so nothing resolves it.
- twiceNestedKeyofIndexInference add-optional: an overload's signature in TS2769 prints
  `Required<Pick<State, "d">>` in full in tsc, `Required<...>` in tttt (the elision budget of a signature in an
  overload list).
- multipleInferenceContexts delete-stmt (`& ThisType<Instance<Data>>` deleted): `this` in an object literal method
  is `ComponentOptionsProperties<{ msg: string; }>` in tsc (the inferred instantiation of the contextual type) where
  tttt prints `ComponentOptionsProperties<Data>`.

## Triage drain 2026-10-07: checker items re-checked, not fixed
- Diagnostic order at one position (TS2741 before TS2454 at `[d1, d2]` against `Base[]`,
  derivedTypeDoesNotRequireExtendsClause with `extends Object`): tsc's check order; not looked into further.
- webpack flagflip leftovers (unused imports only in a JSDoc `{T=}`; TS2488 once per union in nuia, also excalidraw):
  order-dependent laziness in tsc, see the TS 6.0 retarget notes in triage. (vue's loop-flow TS2538/TS4111 are fixed:
  d218d7336 reports a loop's partial evaluations.) The TS7022 `labels` note (class extending any, `const old = this.labels; this.labels = []; this.labels =
  old;` in two methods: tsc TS7022 on the first `old` and on `labels`, tttt on both `old`s) is the same family:
  which declaration's resolution meets the cycle first.

## Mutation seed 17 leftovers (2026-10-07, `scripts/mutate.py --seed 17 -n 4000`: 13 -> 7)
- Known families: arrayFrom null-arg (binding elements read before the contextual assignment, seeds 61/62),
  derivedTypeDoesNotRequireExtendsClause (diagnostic order at one position).

## Mutation seed 18 leftovers (2026-10-08, `MUTATE_DIR=... scripts/mutate.py -n 2000 --seed 18`: 11 -> 5)
Both are check-order effects of tsc's checker reporting during type computation, which tttt's separate
type/typecheck walks don't reproduce; none has a cheap rule.
- **TS2304 inside a `with` body found through flow analysis** (mut1335, sourceMapValidationStatements: a deleted
  `var z` makes `z = 10` in `with (obj) { ... }` unresolvable). tsc never checks the body (checkWithStatement), but
  the flow walk of a later reference `x` passes the assignment `z = 10` and isMatchingReference calls
  getResolvedSymbol(z), which reports "Cannot find name 'z'". tttt's with-body scope (`Scope.with_body`) reports
  nothing. Reproducing it means reporting a with-body name's TS2304 when a flow query visits its assignment.
- **text TS2345, an inferred object literal printed without its computed-name syntax** (mut1928, symbolProperty22:
  `[Symbol.unscopablesX]`, an error-typed computed name): tsc prints the argument as `{ [x: number]: (s: any) => any;
  }` (the any-named key is a number index signature, and the instantiated type has no node to reuse the name from);
  tttt prints `{ [Symbol.unscopablesX]: ... }`. Printing only (the .types baseline reuses the syntax on both sides).

## Circularity tsc finds and tttt doesn't (the resolution stack's leftovers)
- **Base constraint circularity and base types resolved through constraints** (re-checked 2026-10-07, errors only).
  Port, plan-sized: a resolution-identity stack for base constraints (tsc's pushTypeResolution with
  ImmediateBaseConstraint plus getImmediateBaseConstraint's 5-deep identity check), TS2313 at the type node being
  checked, default type arguments instantiated during base type resolution. None of the corpora show these shapes
  (circularConstraintYieldsAppropriateError, its suite test, passes since 2026-10-14).
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
  member again inside an attempt (relate.c's conditional-source handling vs `some_related`).
- Seed-5 mutation leftovers (triage 2026-10-07, `MUTATE_DIR=/tmp/tb-mutate scripts/mutate.py -n 8000 --seed 5`; 25 left):
  `[...x = a] = a` never checks a rest element's initializer
  (TS1186 only), tttt resolves `a` at bind time (TS2304), `export default v` in a block likewise; `interface C1 extends
  Object?.Bar` (TS2499) still resolves the name in tsc (TS2702 too); `({ key }) => {}` under a `[testD()]` computed key with testD unresolved is TS7031 in tsc
  (reason not found); `Object<Initable> & Initable` constraint (TS2315) then a mixin: tsc says TS2507 on `{...} & K`;
  missing TS2313 (mut4105/mut3177); 9 text/elaboration ones.
- Seed-4 mutation leftovers (rerun 2026-10-16, `scripts/mutate.py -n 8000 --seed 4`: 12 differ, the suite's passing set
  has moved so the mutants aren't the original 21's): missing TS2589 x2 (esNextWeakRefs_IterableWeakMap member-typo),
  missing TS2430 (jsonRawJSON2: harness only, see seed 2026102101), spurious TS2693 (reachabilityChecksNoCrash1 never-const), missing TS2698
  (nestedSpreadsAndWidening), text TS2322 (templateLiteralTypeSurrogatePairConcatenation), and known families
  (TS2310, TS2590, TS7022, localTypeParameterInferencePriority, signatureCombiningRestParameters1). Not looked at.
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
from a `this` read, and tsc compares the latter unerased. Owner: whoever owns members resolution (hot path; an extra
instance per qualifying method).

## Local vs export symbol split
tsc keeps one local symbol per block for a name's exported and local declarations, tttt two symbols
(function_local_symbol_decls() and check_export_merge() patch over it; hacks.md "Exported and local declarations").

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
- Older parser leftovers (2026-09-29; see documentation.md "Error recovery") are history now; `scripts/syntaxstats.py --list A` (B..E, M)
  shows the current state. The framework (lists, commit points, missing nodes) is documentation.md "Error recovery".

## JSX
- TS6229 isn't checked for union tag types or inside an overload chain.

## Messages: elaboration and printing (`scripts/errtextdiff.py`)
- Union sources: for `Snap<...>` (a union alias) not assignable to `number`, tsc adds a line for the union's first
  member; tttt omits it.
- xstate meta.test.ts 270/434: elaboration stops one level too high (at `e2:`, tsc goes on to the `meta:` under the
  `@ts-expect-error`). Not reduced.
- Lesson (from the error-text work, errtextdiff): a message change can crash on real code the suite doesn't have (`ty_prop_written_name` on a
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
  their node (a deferred reference's symbolDepth identity). Reverse mapped types print without placeholders in
  declaration emit (cycles TS5088, and the display depth limit stands for TS7056 there: hacks.md).
- TS9039 "Type containing private name '{0}' can't be used with --isolatedDeclarations" (tsc's
  trackExistingEntityName, when the builder reuses a written type naming an inaccessible symbol) isn't reported.

## From the 2026-10-08 hacks audit (parser, flow)
- The parser drops index signature parameter initializers (`parse_index_signature()` has no `FunctionArg` list: TS2304
  in `[a: string = b]` lost).
- `let o: { x?: number } | number = 1; ({ ...o } = { x: 1 });`: tsc reports TS2698 at the rest target `o` (narrowed
  to number), tttt nothing. Mechanism: the right side asks for its contextual type, which is getTypeOfExpression of
  the left pattern, i.e. checkObjectLiteral with its spread check (no TS2698 when the right asks for none: `= v as T`).
  tttt computes the assignment's context eagerly (`assignment_value_context`) and `expr_typecheck` skips the spread
  check for `assignment_pattern`, so a faithful fix needs "does this right side ask for its context" (a lazy context,
  or the list of expression kinds that ask).
- A declaration file in an export context (`declare function f(): void; export function f(x: number): void;`): tsc
  reports TS2383 (overloads must all be exported or non-exported) at the exported one, tttt nothing.

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
  related to a union asks for its key property first, and a function declaration's return type after its body.)
- Other diagnostics tsc reports from an evaluation order tttt doesn't have (mutation testing seed 1021, 2026-10-21,
  not attempted):
  - destructuringWithGenericParameter null-arg: "Binding elements read before the contextual assignment" (seeds 61/62).
  - recursiveTypeInference with `def?:` (zod getters, #64192): tsc's TS2345 prints `$ZodNullable<$ZodType<...>>` where
    tttt has `any` in the object's shape: the re-entrant getter inference of re-entrancy stage D (notes/reentrancy.md).
  - destructuringWithGenericParameter with its `genericObject` declaration deleted: tsc's harness reports that the
    emit resolver added a TS2339 (its "pre-emit / post-emit diagnostic counts" TS-1 line); a baseline-harness artifact.
  - xstate/sloppy and nosnc +TS5115: gone since flagflip.py runs tttt at --checkers 1 like its --singleThreaded tsc
    cache (2026-10-20: tsgo's own TS5115 places change with --checkers). webpack/nuia UmdLibraryPlugin.js (triage.md), the -TS7006 in vue's
    pluginScoped.ts (above).

## TS 7.1 retarget leftovers (2026-10-09)
- **Suggestion diagnostics** (harness only, `@captureSuggestions`): TS6807 overshifts and TS6385 'x' is deprecated for
  identifier references (expr.c identifier_expr_typecheck(), 2026-10-14) are done. Not: TS6133 unused suggestions with
  noUnused* off (unused_enabled() is false then; plain JS too), TS6385 for property accesses, imports and JSX, TS6387 for
  deprecated signatures (a function's reference is decided by the call's signature in tsc: isUncalledFunctionReference),
  `@deprecated` on a declaration whose statement isn't right after the comment (declaration_deprecated() goes back over
  keywords only: the second declarator of `const a = 1, b = 2`). The CLI never prints suggestions.

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
  when a real project shows it.
- **TS2589 through a constraint check** (infiniteConstraints2 extends-object: `T extends Object[]` instead of
  `unknown[]` in Prepend): tsgo reports instantiation depth at `Prepend<T, Conv<...>>` (line 13), tttt nothing. The
  constraint check of `Conv<ExactExtract<U, T>, {a: number}>` against `Object[]` expands Conv recursively in tsgo;
  `unknown[]` (and `any[]`) short-circuit. Not looked into further: which relation path recurses in tsgo.
- **TS2310/TS2506 from a class expression base checked during base resolution** (recursiveClassBaseType
  return-void): `class Derived1 extends class extends Base1 { root() { return; } } {}` with `abstract root():
  Derived1` in Base1: tsgo's checkClassExpression checks the expression's members (root's `void` against
  `Derived1`, which needs Derived1's structure) while Derived1's base types resolve: circular. `return undefined as
  any` relates to anything without resolving Derived1. tttt checks the class expression later, outside the
  resolution. Architectural (eager checks inside type resolution).
- **Instantiated type parameter constraint printed through its declaration node** (text TS2322,
  contextualTypesNegatedTypeLikeConstraintInGenericMappedType2 never-const): `const matcher: never = typeTags<Value>()`
  prints P's constraint as `{ readonly [Tag in Tags<"_tag", Value> & string]: (_: Extract<Value, ...>) => any; }` in
  tsgo (the written node reused with I mapped to Value), resolved members in tttt. Printer node reuse, the type-display work
  territory (displaydiff's buckets).
- **Spurious TS2208 for `null extends T ? K | null : K`** (render, conditionalContextualReturnSubstitutionCache
  drop-as): `function g<T, K>(value: T): null extends T ? K | null : K { return value; }`: tsgo gives no "This type
  parameter might need an `extends ...` constraint", tttt does. `string extends T ? K | null : K` and `null extends T ?
  K : K` have it in both, so tsgo's hasNonCircularBaseConstraint of the synthetic parameter must find a circularity
  only with a null check type and null in the true branch. Not found where (getConditionalType's `null extends T'`
  with T' unconstrained while its constraint is assigned?).

## Mutation seeds 61/62 leftovers (2026-10-14: `MUTATE_DIR=/tmp/X scripts/mutate.py -n 4000 --seed 61|62`)
Seed 61: 11 -> 5 differing, seed 62: 11 -> 8 (both before the last fixes' rechecks of each other). Left, each looked at:
- **Spurious circularity on getters** (seed 62, not the satisfies one, which is fixed): TS7022/TS7023/TS2502 in
  recursiveTypeInference add-optional (`_zod?:` on line 28, `$ZodString`'s), TS2339 in
  noCircularitySelfReferentialGetter2 drop-as. Not looked into; not the expression-memo bail (unchanged with
  b2c2a214c's fix, 2026-10-22).
- seed 61 destructuringWithGenericParameter null-arg: "Binding elements read before the contextual assignment" above.
- seed 61 restElementWithInitializer2 delete-stmt (x2): the seeds 41/42 "Lazy name resolution" entry below.
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

## Mutation seeds 41/42 leftovers (2026-10-12: `MUTATE_DIR=/tmp/X scripts/mutate.py -n 4000 --seed 41|42`)
Seed 41: 10 -> 7 differing, seed 42: 13 -> 5. Left, each looked at:
- **TS7034/TS7005 for an auto-typed `let x = undefined` read through `typeof x` in type parameter positions**:
  fixed for a function's (signature's) type parameters by seed 81 (they're in its flow container); a type alias's
  `<X extends typeof a>` and `<X = typeof a>` say nothing in tsgo nor tttt (2026-10-16 probe).
- **Lazy name resolution** (seed 41 restElementWithInitializer2 delete-stmt): `[...x = a] = a` with x undeclared: tsgo
  only reports TS2304 for x when something resolves it (it's TS1186 and unchecked; the post-emit pass or a later flow
  walk through the assignment reports it); tttt reports every unresolved name at bind time. Suppressing it at the
  rest element makes `[...x = a] = a; [...a = y] = a;` wrong instead (tsgo reports x there, reached by the flow walk
  of the second statement). Order-dependent in tsgo.
- **TS2590 where an intersection is first normalized** (seed 41 normalizedIntersectionTooComplex ann-string-number):
  tttt also reports it at the call (36,14), tsgo only at the callback parameter. Which node is current when the
  too-complex intersection is first built; tttt may build it twice.
- seed 41 noCircularitySelfReferentialGetter2 str-to-num: a getter's circular type prints `any` in tttt's message,
  `ZodOptional<ZodType>` in tsgo (circularity resolution order).
- **Missing circularity errors** (seed 42): TS4109 "Type arguments for 'Array' circularly reference themselves" when a
  conditional `P extends Object` (was `void`) relates `ReducerAction[]` while ReducerAction's alias is being resolved
  (recursiveArrayNotCircular extends-object); TS7022/TS7024 when a reduce callback returns the variable it
  initializes (unknownSymbolOffContextualType1 swap-stmts). See "Circularity tsc finds and tttt doesn't".
- seed 42 objectRestNegative undefined-init (strictNullChecks off): `let o = undefined; var { ...m, a } = o` is TS2700
  in tsgo when the pattern has another element (the initializer isn't widened to any under the pattern's contextual
  type), not with the rest alone; tttt reports neither and then TS2322 instead of TS2741 for `({ b, ...na } = o)`.
- seed 42 signatureCombiningRestParameters1 add-optional: a union of signatures one of which has a rest parameter of
  type `[string] | undefined` (TS2370): tsgo's combined signature reports a spread argument as TS2556, tttt combines
  the rest parameter to never and says TS2345.

## Mutation seed 2026102001 (2026-10-20, -n 3000: 5 / 1552 differ)
Fixed: a computed key naming no property against a generic mapped contextual type, recursive-return skip with type
arguments / through exported variables. Left:
- arrayFrom return-void (the binding-element entry above).

## Mutation seed 2026102101 (2026-10-20, -n 8000: 10 / 4102 differ)
Fixed: a property symbol an accessor joins (merged interfaces/classes, an auto accessor after an interface property)
typed by its accessors without the property's optionality (TS2717/TS2687/TS2564 against them; TS2687 compares with an
accessor as the first declaration); a known-key object with a generic property no longer relates to a generic mapped
type through the member fallback (`{ type: T }` to `Partial<X>`, `Partial<C<T>>` of a deferred conditional). Left:
- declarationEmitNestedAnonymousMappedType extends-object: `M[k] extends Object ? M[k] : never` related to `string |
  number | symbol` by the deep-nesting cut-off of `conditional_source_related()`'s default constraint step (hacks.md:
  removing it costs react +82% instructions and gives largeTupleTypes a spurious TS2799).
- complicatedIndexedAccessKeyofReliesOnKeyofNeverUpperBound add-optional: the TS2322 now comes, at the `return` where
  tsgo puts it at the shorthand `type` of the returned literal (tsgo's hasExcessProperties moves r.errorNode to the
  excess property during a reporting pass, then a later failure reports there; not reproduced).
- jsonRawJSON2 ann-string-number (also seed 4's missing TS2430): harness only, the shared lib program's declaration
  order (harness.md "Shared lib programs"); the CLI reports it.
- Known families: unknownSymbolOffContextualType1 (TS7022 circularity), conditionalContextualReturnSubstitutionCache
  (TS2208 render), restElementWithInitializer2 (lazy TS2304).

## Mutation seed 2026102102 (2026-10-20, -n 8000: 10 -> 8 / 4003 differ)
Fixed: TS2678/TS2352 heads name the normalized pair like TS2322's (`1` against `string | undefined` is `number` to
`string`); a union's combined signature is declared by the member signature it was made from (TS2771 "The last overload
is declared here" for `(string[] | number[]).splice`). Left, each looked at:
- **A const's name from its initializer when its annotation gives none** (typeGuardNarrowsIndexedAccessOfKnownProperty7
  and unionTypeWithIndexSignature never-const: `export const key: never = Symbol()` then `[Foo.key]: string` /
  `this[Foo.key] = ...`): tsgo's tryGetNameFromEntityNameExpression falls back to the initializer's type (a unique
  symbol) when the annotation (never) isn't usable as a name, so `this[Foo.key]` still matches the member for
  TS2564's flow check and `both[sym]` reads the `[sym]` property. tttt goes by the declared type only
  (field_initialized_in_constructor()). Only with a wrong annotation; not done.
- generatorTypeCheck46 member-typo: TS2488 twice in tsgo, once for the yield* operand typed in the context-sensitive
  pass (`{ [x: number]: {} }`, the method skipped) and once final; tttt reports the final one (the "errors of an early
  pass stay" family).
- escapedIdentifiers member-typo: tsgo prints `argX\u0031` (escaped) in TS2551 only when another escaped identifier with
  the same unescaped text appears earlier in the file (`arg\u0031 = 1` in a method); alone it prints `argX1` like tttt.
  A tsgo text quirk, not mirrored.
- unicodeLineSeparatorInMultilineComment never-const: tsgo's CLI reports the TS2322 like tttt; its test harness writes
  no errors baseline for that file. Not a tttt bug.
- Known: conditionalContextualReturnSubstitutionCache
  (TS2208), complicatedIndexedAccess... (position, seed 2026102101).

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
  It's an extra error on an already-failing call.
- **missing TS2536 from declaration emit** (arrayFakeFlatNoCrashInferenceDeclarations str-to-num, `@declaration`):
  tsgo reports two TS2536 at the alias's type literals (positions inside `BadFlatArray`) while serializing `foo`'s
  inferred type, in place of the original TS5088.
- **position TS5115** (recursiveConditionalCrash3 extends-object): the "infinitely circular" error is one line off
  (where the runaway is first met).
- **position TS2322 from a sticky errorNode** (complicatedIndexedAccessKeyofReliesOnKeyofNeverUpperBound add-optional):
  tsc reports the whole-object message at the `type` shorthand property. An excess-property check on a member of the
  target intersection (`type` is excess for `Partial<Omit<..., "id" | "type">>`) moved the relater's errorNode there;
  that message was later replaced by the outer chain but the node wasn't restored. tttt models the moved node only when
  the excess-property message itself is reported.
- **elaboration TS2344** (distributiveConditionalBaseConstraint drop-optional): the chain differs.
