# Type core, inference and flow bugs
Wrong types (not missing diagnostics: `missing_checks.md`; not printing: `display.md`). Every test named here was
failing on 2026-10-05 (suite 13098) unless the entry says otherwise.

## exactOptionalPropertyTypes
Of the tests using the option, only contextuallyTypedOptionalProperty(exactOptionalPropertyTypes=true) fails on it
(inferred candidates keep the missing type where tsc drops it, below under Inference); discriminateWithOptionalProperty2
fails types in both configurations.

## Inference
- **First pass skips context-sensitive arguments entirely**: tsc checks them with their functions replaced by
  anyFunctionType and still infers from the (non-inferrable) literal to anything but a naked type parameter (hacks.md
  "Skip-sensitive rounds"). correlatedUnions, which once showed it, fails types again: check whether this is why.
- Not investigated in depth: contravariantOnlyInferenceFromAnnotatedFunction, mappedTypeRecursiveInference2
  (`missing_checks.md` "Inference").
- `infer_rest_variadic`/`check_rest_arg` keep an argument's type from whichever overload candidate typed it first
  (`if (arg->type) continue`), though its contextual type is the candidate's rest element; tsc re-checks per candidate.
  Clearing the non-spread rest arguments' types at the top of infer_rest_variadic changed nothing anywhere (suite,
  corpora, instructions) and no probe observes it (`q<T extends ["a"]>(n: number, ...a: T)` before `q<T extends
  string[]>(s: string, ...a: T)` passes). Look here if overloads with differing rest types ever disagree with tsc.
- Contextual types from a generic mapped type substitute the whole constraint for K, not the property's key.
- contextuallyTypedOptionalProperty(exactOptionalPropertyTypes=true): `T` inferred from `match(y => y > 0)` for an
  optional property keeps `undefined` (tsc strips the missing type), so the operand check says TS18048.
- Older tsc-corpus findings (a probe each): `visitNode(node, visitor, isExpression)` resolving to the overload without
  the predicate (`test/probes/plan05/visitNodeInference.ts`); literal widening in an object literal that has a
  contextual type.

- Laziness artifacts of tsc's inference (triage 2026-09-29, both need lazy return/member types to reproduce):
  genericCallWithGenericSignatureArguments3's `(n: Object) => 1` keeps its literal because tsc resolves the inner
  arrow's return type only after U's first candidate is that arrow's own function type: its contextual signature is
  then its own (`contextualSignature === getSignatureFromDeclaration(func)` in getReturnTypeFromBody, contextual type
  = the return type itself). mappedTypeInferenceErrors infers `C = { bar: unknown; baz: unknown }` where tttt has
  `bar: number`: tsc's reverse mapped member types resolve lazily, while the method body's `this.bar` is being checked.
- Return-type inference with Pick: hacks.md `infer_from_objects()` (twiceNestedKeyofIndexInference passes only because
  the shortcut skips return-type inference; how tsc's returnMapper avoids K2 = `never` is unknown).
  indexedAccessRelation differs in its elaboration (tsc: `Type 'Foo' is not assignable to type 'S["a"] & T'`,
  the constraint of T) and in the union order of `keyof S | "a"`.
- (triage 2026-10-07) The contextual type of an argument for a rest parameter typed as a mapped type over `keyof
  [...E, "abc"]` (index 0 -> `{ bar: [...E, "abc"][0] }`) keeps the literal in tsc: TS2345 messages print `{ bar:
  "abc"; }` where tttt prints `{ bar: string; }`. circularInlineMappedGenericTupleTypeNoCrash itself needs tsc's TS2589.
- (2026-10-07) genericCallWithGenericSignatureArguments3 (strict false; errors and types): `f1<U>(a: () => U, b: () =>
  U)` called as `f1(() => (n: Object) => 1, () => 2)` gives U = `(n: Object) => 1` in tsc, `(n: Object) => number` in
  tttt. The inner arrow's return type is computed lazily in tsc, and by then its contextual signature goes through
  the outer arrow's contextual return type, i.e. the resolved signature's U, which is the inner function itself: its
  own signature, so the literal stays (getReturnTypeFromBody). tttt's equivalent (`KEY_CONTEXT_NONE` +
  `ty_late_contextual_return()`) only covers a function that is itself an argument, not one returned from an
  argument. Extending it means recording the late context for functions nested in argument return expressions.
- (2026-10-07) map-to-obj (DefinitelyTyped, 2 `$ExpectType`): `id(new Map([["a", 1]]))` with `id<K extends
  string>(m: Map<K, V>)`: tsc's return mapper for the inner `new Map` comes from the contextual type with the outer K
  as createOuterReturnMapper has it (nothing inferred: its constraint string), so "a" widens; tttt's inner return
  inference maps the inner K to the outer K, whose primitive constraint keeps "a". Instantiating the return source
  with the outer's constraints (infer_type_arguments' `return_source`) fixes it but cost xstate and valibot +1.8%
  each even gated to primitive constraints: needs a cheaper formulation.
- (2026-10-07) ember__service (2, also v3): an object literal method's literal return ("") widens when the call has
  two overloads and the first round (subtype) fails on `this`: the return type is computed (and memoized) during that
  round without the "own contextual signature" that keeps the literal. Repro: `declare class Core { constructor(p?:
  object); static extend<S, I>(): ECC<I>; static extend<S, I, T1 extends {[k: string]: any}>(this: S & ECC<I>, a:
  T1): ECC<T1 & I> }` with `ECC<T> = (new (p?: object) => T) & (new (...a: any[]) => T)`: `new (Core.extend({ m() {
  return ""; } }))().m()` is "" in tsc. Same family as the item above (a return type memoized before the context
  that would keep the literal exists).
- (2026-10-07) babel__traverse (4) and babel__helper-plugin-utils (2): with @babel/types unresolved, a contextual
  type `{ [k: string]: H<t.Node> } & { [k: string]: H<t.Aliases> }` (H a function | object union) gives a method
  parameter NodePath<t.Aliases> in tsc, NodePath<t.Node> in tttt: which unresolved member (tsc keeps named error
  types, tttt collapses them to any) survives the intersection of contextual signatures. Depends on the named error
  types item (triage "Plan 48 stage 4 leftovers"); broken packages only.

## Relations, instantiation, reduction
- **Variance measurement order (vue, `--checkers 1` only, since stable type ordering 2026-10-07)**: a spurious TS2345
  at `runtime-dom/src/directives/vModel.ts:383` (`DirectiveBinding<any, string, any>` to `DirectiveBinding<any,
  "number", any>`). Cause: circular variance measurements finish differently depending on which one starts first.
  tsc starts with `DirectiveHook` (triggered at `runtime-core/src/directives.ts:185`, `hook =
  mapCompatDirectiveHook(...)`) and measures DirectiveHook 2/2/2/16/2, DirectiveBinding 1/16/1, ObjectDirective
  4/4/1/4. tttt starts from the `Directive` alias (in a subtype reduction of `CompatVue | ObjectDirective |
  FunctionDirective | undefined`), nests DirectiveBinding -> ObjectDirective -> DirectiveHook, and gets DirectiveHook
  #2-#4 independent (4: DirectiveBinding is in flight, so both directions hold). The new union order made
  DirectiveBinding/ObjectDirective/SSRDirectiveHook match tsc where the old one didn't; only DirectiveHook is left.
  Reproduce: `TTTT_DUMP_VARIANCES=1 ./release/tttt --checkers 1 -p thirdparty/corpora/vue/tsconfig.corpus.json`
  against `/tmp/tscvar.js`-style instrumentation of `tscinstcount.js` (`VARIANCES=1`, plus a START log in
  getVariancesWorker printing `currentNode`). Find why tsc doesn't measure `Directive` (or measures it later) there.
- **Two copies of one declaration file (webpack through a symlink): one diagnostic left.** From a worktree (where
  `thirdparty/corpora` is a symlink) webpack's `types.d.ts` is in the program twice, as in tsc (root files keep the
  symlinked path, only node_modules results are realpathed and `node_modules/webpack -> ..`), and the examples'
  `webpack.config.js` compare against the other copy's `Configuration`. tsc reports 24 errors that way (2x TS2321
  "Excessive stack depth" at aggressive-merging/webpack.config.js:7 and :20, 22x TS2322). Since 2026-10-05 tttt
  matches 23 of them at every checker count in ~1.5 s (was ~60 s, 8x TS2859 instead): recursive_related() caches a
  failure even after an overflow, as tsc does (it re-explored the same overflowing subtrees until the relation count
  ran out), a depth overflow is TS2321 at the current node (was a HACK reporting TS2589), and check_assignable()
  still elaborates after one. Left: at aggressive-merging/webpack.config.js:20 (the array element tsc's
  elaborateElementwise checks with an error node) tsc's reporting pass overflows again (TS2321) where tttt's
  explanation finds a real failure about 40 levels down (`IteratorYieldResult<Module>` vs `IteratorResult<Module,
  any>` through Compiler -> Compilation -> Module -> Dependency -> ModuleGraph ... -> Iterable<Module>) and reports
  TS2322: the order in which the two explore the copies differs somewhere along that chain. Repro without the corpus
  checkout's symlink: /tmp project with `wpl -> <real webpack>` and a tsconfig whose only file is
  `wpl/examples/aggressive-merging/webpack.config.js` (types node, typeRoots wpl/node_modules/@types).
- Distributive conditional base constraint: with `S2<T> = T extends string ? T : number`,
  `g2<T extends unknown[]>(x: [...{ [K in keyof T]: S2<T[K]> }])` widens `g2(['a'])` to `[string]`, tsc `["a"]` (its
  forConstraint constraint of `S2<T[number]>` keeps the true branch with T's implied `string`).
- `StateId<U>` to `StateId<T>` (xstate's StateId, no variance measurement) gives TS2589 next to the TS2322 tsc also
  reports: the definitely-assignable checks instantiate deeply nested indexed-access chains. Repro:
  test/probes/xstate/state_id_deep_nesting.ts.
- inferConditionalConstraintMappedMember (types): `{ [K in keyof T]: X } extends { [_ in keyof T]: infer U } ? U :
  never` has constraint X in tsc (K4 variant with template `1` gives number), tttt unknown; why tsc infers there
  despite the generic mapped check type is unclear. Kept out of TS2344 by the deferred-conditional HACK.
- **Intersection reduction by constraint**: `T & P` for a type parameter T with a primitive constraint is reduced
  (`reduce_type_variable_intersection`, and `(T & "a") | (T & "b")` back to T), but not for indexed access type
  variables (see hacks.md) nor type parameters with a deferred declared constraint. mappedTypeNotMistakenlyHomomorphic
  (`keyof Gen<ABC.A>` misses `a`).
- **Permissive instantiation** for conditional types is missing (`ty_conditional_resolve` defers where tsc picks the
  false branch); a flat `TyMapper` can't say "every type parameter -> any". Needs a wildcard type + cached permissive
  instantiation.
- **isTupleLikeType's length half**: an array-like context whose `length` is a number literal isn't tuple-like
  (only the "0" property rule is in `context_is_tuple_like`).
- **Conditional true branch with `infer`s**: relating a deferred conditional through its true branch with `infer`s as
  unknown is done only when the check type is generic; with a generic extends type tsc has real inferences.
- conditionalTypes2's remaining errors are elaborations: variance-based `Type 'A' is not assignable to type 'B'` for
  `Covariant<A>` vs `Covariant<B>`, and an intersection source with a bare `T` (`Foo & Bar & T`) reported whole where
  tsc reports its constraint (getEffectiveConstraintOfIntersection).
- TS2430 leftovers: interfaceExtendsObjectIntersectionErrors (missing).
- A homomorphic mapped type over a `satisfies`/const object doesn't resolve its keys (the `computedOptions` mapped
  type in the tsc corpus's `utilities.ts` printed a huge `keyof "a" & keyof "b"` key). Not re-probed.

- Base constraint of an indexed access comes out wrong in valibot: in
  `library/src/methods/required/requiredAsync.ts` (the `'~run'` member's `InferIssue<TSchema>`, TSchema constrained
  to `SchemaWithoutPipe<LooseObjectSchema<...> | ...>` = `(A | B | ...) & { pipe?: never }`), `ty_base_constraint` of
  `TSchema['~types']` is `undefined`, where tsc has `{ input; output; issue } | undefined`. Found when constrained type
  variable reduction (`reduce_type_variable_intersection`) turned `TSchema['~types'] & {}` into never; not reduced to
  a small repro (a two-member union of interfaces with an optional `'~types'` under exactOptionalPropertyTypes is
  fine). Possibly a constraint computed while an alias or the type parameter's constraint is in flight and memoized.

- **`awaitedTypeStrictNull` overflows the stack** in `instantiate_mapped_over_list` under the debug build + valgrind
  (8MB main stack; the release build passes; reproduced 2026-10-05 with `./test.sh -f awaitedTypeStrictNull`). Deep
  but maybe not infinite recursion; worth checking whether the recursion is bounded.

- **No general property list for intersections** (tsc's getPropertiesOfType): `ty_has_exact_optional_unassignable`
  (relate.c, takes the first member's properties for unions now), jsx.c `container_prop_name`, members.c
  `builder_add_intersection_members` and the member loop in class_decl.c (~line 509) take each member's apparent type
  and require `TYF_OBJECT`, so a type parameter constrained to an intersection is skipped. `spreads_into_nothing`
  (expr.c) ignores intersections (tsc: empty when every member is). `with_synthetic_default` (lower.c) keeps index
  signatures and private/readonly members that tsc's getSpreadType drops. check.c's `number_index` check and the
  private-name check are still object-only gates (low value).
- **Spurious TS2589 on zod without variance annotations** (deep-partial.test.ts:111, since the explicit type argument
  constraint check of triage 2026-09-28): `toExtend<z.ZodObject>` against expect-type's recursive `MismatchInfo` goes
  >100 levels (`props_related_impl` -> `prop_type` -> `ty_indexed_instantiated` -> `find_prop` ->
  `conditional_from_root` -> ~135 `instantiate_inner`). tttt's order of `MismatchInfo`'s key union starts with `array,
  nonoptional, _zod...`, tsc's with `default`, which fails at once. Union order again; the TS2344 text there also
  differs (property order, tsc's `... 62 more ...` truncation). `perf_leads.md` "Variance measurements" says how to
  build that zod variant (strip `in`/`out` both after `<`/`,` and at line starts inside multi-line lists).
- (triage 2026-10-07, mut3125) AllowMissing for destructuring defaults should go by tsc's isObjectLiteralType of the
  unwidened parent. tsc has three states (fresh, regular, widened; getWidenedTypeOfObjectLiteral builds a type
  without ObjectFlags.ObjectLiteral), so `var b = {y: 0}; var {x = ""} = b` is TS2339 while `var {x = ""} = {y: 0}` is
  TS2353. tttt's `ty_widened_objects()` keeps `anonymous.object_literal`, so `binding_assigned_type` (lower.c) and
  binding.c treat widened declarations as literals. Fix: the widened twin gets `object_literal = false`, after auditing
  every reader (union partial-property rule, excess properties, the isObjectLiteralType relation rules) and the array
  literal element types tttt widens early (`for (var {x = ""} of [{}])`). Medium to large.
- (mut1640) An element *write* with a `never` index on a union of objects (`both[sym] = 'x'`, `sym: never`, members
  `{ s: number, '0': number, [sym]: boolean } | { [n: number]: number, [s: string]: string | number }`): tsc reads the
  union's number index info (getUnionIndexInfos, getApplicableIndexInfo with never: `number | boolean`), tttt `never`.
- The conditional base-constraint fallback (`base_constraint_uncached` TY_CONDITIONAL, used when there's no
  distributive constraint) leaves an `any` branch out like tsc's getDefaultConstraintOfConditionalType but uses the
  raw true branch, not tsc's inferred one. `ReturnType<T[M]>` property access (seed-5 mut0710/mut3585) still misses
  TS2339.
- (2026-10-07) sliceResultCast (types): `x.slice` of `[number, string] | [number, string, string]` is a union of two
  equal signatures in tsc (each tuple's Array members are instantiated with the tuple as `this`, Array being merged
  makes `this` possibly referenced everywhere); retarget_this_props() only retargets props that mention `this` (a
  deliberate perf shortcut), so tttt's members are shared and the union collapses. A fix must keep the shortcut's
  cost (instrcount valibot/zod) or make the retargeted members cheap.
- (2026-10-07) genericCallInferenceUsingThisTypeNoInvalidCacheReuseAfterMappedTypeApplication1 (types): `inner.log(100)`
  with `inner: XXX` and `log = implement<this>()(<N extends number>(n: N) => succeed(n))` is `F<Effect<100, never,
  never>, this>` in tsc 6 (deferred: `this` is generic), tttt resolves it to `Effect<100, never, never>` (the test's
  own comment, pre-6.0). Likely mechanism, unverified: reading a member through a non-generic class's own declared
  type doesn't instantiate `this` in tsc (resolveObjectTypeMembers' rangeEquals shortcut keeps the declared members),
  and tsc 6's SingleSignatureType has no outer type parameters to carry a `this` mapping; tttt maps `this` to XXX on
  the access. Check `class C { p!: F<1, this> }; declare const c: C; c.p` against tsc first.
- (2026-10-07) computedTypesKeyofNoIndexSignatureType (types): tsc prints `K_1`..`K_9`: instantiateMappedType clones
  the key type parameter per instantiation (a fresh K each time, renamed by the printer), ty_mapped_info_instantiate()
  keeps the one K. Display only, but cloning would also make each instantiation's key distinct for caching; weigh
  the cost before doing it for display alone.
- (2026-10-07) noImplicitAnyUnionNormalizedObjectLiteral1 (types): union order of two widened object literals with the
  same symbol (the spread literal) goes by type id; tsc's getWidenedTypeWithContext makes both members anew in member
  order, tttt's separate widen_nullable() pass keeps the unchanged `{ c: string; ... }` (older id) and remakes only
  the other, so the order flips.
- (2026-10-07) knockout.mapping `$ExpectType` (knockout.mapping-tests.ts:109): tsc orders
  `KnockoutObservableArray<KnockoutObservableType<any>> | KnockoutObservableArray<any>`, tttt the other way round.
  Not investigated (type id order of two references to one target: which type argument list is made first).
- (2026-10-07) node (4 `$ExpectType`, all versions): `[value: number] | [value: number]` from two declarations: tsc's
  tuple targets are keyed by their labels' declarations (getTupleTargetType's namedMemberDeclarations), tttt's by the
  label names, so the two tuples are one type and the union collapses.
## Circularity (plan 29 stage 3 leftovers; the outer-declaration cases are plan 39)
- recursivelyExpandingUnionNoStackoverflow lacks the TS2589 tsc reaches first (tttt's interned instantiations find the
  TS2615 cycle at once), and tsc prints the mapped type with `containsError` in mapped syntax. (Re-checked
  2026-10-07: `type N<T, K> = T | { [P in K]: N<T, K> }[K]`: tsc doesn't cache an alias instantiation while it is
  being made, so it recurses with fresh types to the instantiation depth limit before the mapped symbol finds itself
  circular; tttt interns the instance and meets it at once. Matching it means not interning in-flight alias
  instantiations, not tuning a counter: leave it.)
- circularBaseTypes, infiniteConstraints, circularConstraintYieldsAppropriateError: plan 39 stage 3 item 5.
- TS4109 missing where tttt catches the cycle elsewhere: circularTypeArgumentsLocalAndOuterNoCrash1,
  circularlyReferentialInterfaceAccessNoCrash. limitDeepInstantiations: `Foo<"true", {}>` never reaches the limit.
  In circularTypeArgumentsLocalAndOuterNoCrash1 (`type X = NumArray<X extends {} ? number : number>`, NumArray local
  to `f<_T, _S>`) tttt does find the cycle in `ty_reference_args()`, but outside the typecheck phase (the types walk),
  where `ty_error_at` drops the error. tsc gets there during the alias's check: resolving the argument's conditional
  relates X to `{}`, which resolves X's members and so its type arguments while they're in flight. tttt's relation to
  `{}` never looks at a deferred reference's arguments, and the TS2344 check of the alias body doesn't reach them
  either. Also: the circular branch errors all arguments, outer ones included (`f<any, any>.NumArray<any>`); tsc
  keeps the outer type parameters and errors only the local ones (`NumArray<any>`).
- Spurious TS2589: recursiveConditionalTypes (the `unbox(b1)` calls), recursiveTypeReferences1.
- type-fest's `test-d/opaque.ts` (~90% of type-fest's check time) runs into TS2589 at line 68 (`Tagged`/`UnwrapTagged`
  over `TagContainer`) plus four TS7022 tsc doesn't report: a correctness bug with a perf symptom.
- Spurious TS7022 on `const can = composite && o?.e && !out` when the type of `e` mentions an unresolved name (`Map`
  without lib es2015). Probe: test/probes/plan08/alias_optional_chain.ts minus its `@target`.
- `check_with()` (ty/expr.c) computes an expression with a contextual type outside the LazyType, so no cycle detection
  (`let [x = x] = []` is stopped by the pattern's names being on the resolution stack, plan 39; other contextual
  self-references can still overflow the stack).
- Mutually recursive object literal getters through generic calls (probe test/probes/triage/getter_cycle.ts, zod's
  getter-schema shape with toy types): tsc reports TS7022 on all three variables and TS7023 on both getters, tttt
  nothing, and types the cycle differently (`variantA` any where tsc has `Schema<{ kind: "a"; readonly child: any }>`).
  Where the cycle is entered differs: tsc defers the getter bodies, and the getter's return type is asked for while
  `tree`'s initializer resolves `variantA`'s type. Not caused by the deferral/checkReturnStatement work (base fails it
  identically); worth a TTTT_DEBUG_CYCLE look.
- selfReferentialDefaultNoStackOverflow wants TS7022 on `export default { mixins: [DefaultSpinner] }` importing itself.
- (mut0806) `class D extends class extends Base1 { root() { return undefined } }` with `abstract root(): D`
  (strictNullChecks): tsc's checkClassExpression -> checkClassLikeDeclaration runs eagerly inside D's
  getBaseConstructorTypeOfClass; relating `() => undefined` to `() => D` calls isWeakType(D), which needs D's members
  while its base constructor resolves: TS2506 + TS2310, D ends with no base (types print `typeof (Anonymous class)`).
  tttt defers class expression members and meets neither cycle. Plan 39 territory, low value.
## Classes and declarations
- Plan 32 leftovers: a class extending a base that constructs `any` should get tsc's anyBaseTypeIndexInfo
  (`[x: string]: any`) instead of the `ty_has_unresolved_base()` gate; tsc's display of `new Outer<string>().m` clones
  generic method type parameters (`Outer<string>.Outer.m<U>.In`).
- multipleDeclarations (JS): `F.prototype.m = function` next to `this.m = v` is two symbols in tsc, one member here.
- mergedDeclarations7: tsc's combineValueAndTypeSymbols (a named import from an `export =` whose value has property
  `Passport` and whose namespace exports interface `Passport`) makes a transient symbol whose declared type is a
  *fresh* interface type with its own `this`, so `use(): this` fails structurally (TS2322 "could be instantiated with
  a different subtype"). Needs a synthetic merged symbol with its own declared type. Quirk, low value.
## Narrowing and flow
- dependentDestructuredVariables (types only now): narrowing destructured variables by each other (tsc's
  getCandidateDiscriminantPropertyAccess on binding elements) still differs somewhere; re-diff before working on it.
- The JSX attribute types (`KEY_JSX_ATTRIBUTE`) and an IIFE parameter's contextual type depend on check order (the
  last check wins). (An IIFE's parameters are pushed from `ty_quick_expr_type()` too now: before, a variable
  referenced above its IIFE initializer typed the function without them.)
- Destructuring assignment targets are checked as a whole, not property by property (tsc's
  checkDestructuringAssignment; hacks.md "Destructuring assignments"). The `{ a = dflt }` default of a shorthand
  assignment pattern is resolved but not checked (`ObjectExprItem.shorthand_default`).

## Corpus cells (flagflip) left after triage 2026-10-07 (each re-checked that day)
- (Fixed 2026-10-07, vscode now 0: TS2322 at stateToProgressAdapter.test.ts:1956. Mechanism, for similar cases: the
  arrow's return type inference asks whether its end is reachable; the reachability walk meets `assert.deepStrictEqual(
  calls, 1)` (node's `assert`, a generic assertion signature, so its effects need the call resolved, as in tsc's
  getEffectsSignature), which types `calls`, its initializer and the `image` reference before the declarations are
  checked. initial_binding_type() then typed `image`'s initializer without the annotation as contextual type, so
  `type: ToolResultContentType.EmbeddedResource` widened to the enum, no member of the declared union took it, and
  the cached assignment reduction was the declared union. It looked like an electron trigger because electron's
  typings bring @types/node, which is what makes `import assert from 'assert'` resolve.)
- valibot/nosnc -TS2578 x2 (forward.ts:51, forwardAsync.ts:55): `for (const key of path)` with `path: ValidPath<TInput,
  TPath>` (= `TPath extends LazyPath<TInput, TPath> ? TPath : LazyPath<TInput, TPath>`, deferred). tsc's key is
  generic, `KeyOf<TInput> | KeyOf<TInput[KeyOf<TInput> & keyof TInput]> | (KeyOf<TInput> & keyof TInput)` (tsctypeat;
  it prints `string | number` only where an annotation substitutes the constraint), so `pathInput[key]` on unknown
  isn't TS7053 and the `@ts-expect-error` is unused; tttt's key is `string | number`. tsc iterates the conditional's
  apparent type, its constraint: the true branch narrowed by the extends type and the false branch LazyPath's tuples,
  whose elements stay generic (object types are their own base constraint). tttt's iteration resolves all the way to
  primitives. Same root as the "conditional base-constraint fallback" item above. Repro: a project with one file
  importing `RequiredPath, ValidPath` from valibot's `methods/forward/types.ts`, strictNullChecks false, iterating
  `path` and indexing an `unknown`.
- xstate/eopt +TS2322 input.test.ts:272: generic Spawner signatures compared, `options` intersections that print the
  same are unrelated in tttt (`input?: number` & `input: unknown` reads number | undefined against number).
- xstate/sloppy +TS2589 graph/utils.ts:91 (`snapshot.getMeta()[id]`, Record<StateId<any> & string, ..>): a
  self-contained probe gives TS2589 on both sides, so it depends on which instantiations tsc has cached by then. Low
  value; drop unless it spreads.
- webpack/unused -TS6133 x5, -TS6196 x5 (Compilation.js:117/151/159/160, css/CssModule.js:11, json/JsonModule.js:16,
  module/ModuleFactory.js:8/10, template/DependencyTemplate.js:11/15): JSDoc `@import`ed names used only in
  `@property` tags of a `@typedef {object}` or in a `@callback`'s `@param`/`@returns`. tsc marks a JSDoc type
  reference's alias as used only when the reference is resolved, and those are resolved lazily: a typedef's members
  only when something needs them (a structural comparison, a property access), a callback's signature only when it's
  called or compared. Probe: `/** @import { RO as A1, RO as A3 } from "./a" */`, `@typedef {object} Data` with
  `@property {A1=} ro`, `@callback Cb` with `@param {A3} x`, a function taking `@param {Data} d @param {Cb} c` and
  returning them: tsc says A3 unused, and A1 too unless something resolves Data's members (an array literal of the
  parameters does). tttt marks references at name resolution (several_types.c `unused_mark_referenced`), eagerly. A
  laziness artifact of tsc's: matching it means marking JSDoc typedef/callback-body references when their types are
  resolved. Low value unless more corpora show it.
- (Fixed 2026-10-07: webpack/nuia TS2488 -1/+1, by where a union is first asked; vscode/nosft copilotAgent.ts:5860
  TS2339, overloaded method types in the stable order.) excalidraw/nuia's TS2488 -2/+1 is still the array-literal
  identity mechanism (above in triage, tsc's `[0, 0]` its own type object).
- (Fixed 2026-10-07: vscode/eopt TS7006 x18, vscode/nonia resultCodeEditorView TS2339 x4, vue/nir TS7030.)
- Perf lead, not looked at: 9751c955 (TS2344 on deferred conditional arguments) costs valibot ~0.75%, mostly one
  relation (`TSchema extends ... ? IssueDotPath<TSchema> : string` against keyof any, flatten.ts:41, ~13 ms): tttt's
  relation through IssueDotPath's constraint may be slower than tsc's.

## Moved from hacks.md by the hacks audit (2026-10-08)
- A `declare x;` field without type or initializer in a class without a constructor doesn't take the base class
  property's type (tsc's getWidenedTypeForVariableLikeDeclaration: getTypeOfPropertyInBaseClass under noImplicitAny):
  `class B { x: string = "" } class D extends B { declare x; } const s: number = new D().x;` is TS7008 at `x` and
  `any` in tttt, TS2322 in tsc. Belongs next to `field_type_from_assignments()` (class_decl.c), which needs the class.
- Generators: no next type inferred from how `yield` results are used (tsc's getReturnTypeFromBody collects
  contextual types of yield expressions); `ty_generator_context` filters only unions.
