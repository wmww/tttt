# Type core, inference and flow bugs
Wrong types (not missing diagnostics: `missing_checks.md`; not printing: `display.md`). Test statuses checked
2026-09-27 (triage, suite 8952); "(re-probed)" = the symptom was confirmed then.

## Inference
- const type parameters with several candidates: `function f4<const T>(x: [T, T]): T; f4([[1, "x"], [2, "y"]])`
  gives TS2322s where tsc infers `readonly [1, "x"] | readonly [2, "y"]` (jsdocTemplateTag6, plain TS too).
- **Context-free type of return-sensitive functions** (badInferenceLowerPriorityThanGoodInference, spurious TS2339 now):
  tsc's first round types an argument like `() => ({ a: 1, b: x => {} })` as a return-only signature whose return is
  checked with `SkipContextSensitive` (nested sensitive functions are the non-inferrable `anyFunctionType`, nothing
  memoised: `links.contextFreeType`). `ExprCtx.skip_sensitive` exists (plan 19 R10), but a function argument is still
  skipped whole: the missing piece is the return-only signature.
- **Fixing without candidates** (plan 30 stage 2): `mutate((a, b) => b)` with `callback: (a: NoInfer<T>, b: number)
  => T` is `number`, tsc `unknown`: tsc fixes every type parameter a context-sensitive function's contextual
  parameters mention, candidates or not; call.c's argument loop only fixes those with candidates. Dropping that gate
  lost 16 tests (bindingPatternCannotBeOnlyInferenceSource, objectFreeze, thisTypeInObjectLiterals2...): the gate
  stands in for something else (probably what a fixed parameter without candidates resolves to: its
  default/constraint via getInferredType). noInfer.ts types only needs this.
- **Generic rest with a binding pattern**: `wrap<Args extends unknown[]>(_: (...args: Args) => void)`, `wrap(({
  cancelable } = {}) => {})`: tttt types the parameter `unknown` (the constraint's element), tsc the pattern's own type
  `{ cancelable: any; }` (inferFromAnnotatedParameters with a generic rest): inferredRestTypeFixedOnce.
- `unknown` where tsc infers a type: intraExpressionInferences, mappedTypeRecursiveInference; mappedTypeMultiInference
  (`K` of `...cssSets: { [P in K]?: Style }[]` inferred from the first rest argument only: spurious TS2353).
- Reverse mapped types (plan 08) are eager objects, no arrays/tuples, cut at depth 4 (`reverse_mapped_type`); tsc's
  are lazy. Contextual types from a generic mapped type substitute the whole constraint for K, not the property's key.
- Contextual signature of a type with several call signatures (tsc's getIntersectedSignatures:
  signatureCombiningRestParameters2, spurious TS7019). 107 tests have a spurious TS7006: sample them.
- promiseTry (spurious TS2578): `Promise.try(cb, ...args)` with `U extends unknown[]` inferred from both the callback's
  parameters and the rest arguments gets no TS2554 arity error.
- contextuallyTypedOptionalProperty(exactOptionalPropertyTypes=true): `T` inferred from `match(y => y > 0)` for an
  optional property keeps `undefined` (tsc strips the missing type), so the operand check says TS18048.
- `ThisType<T>` arguments aren't instantiated with the inference in progress (Vue-style `defineComponent` options).
- Older tsc-corpus findings (a probe each): `visitNode(node, visitor, isExpression)` resolving to the overload without
  the predicate (`test/probes/plan05/visitNodeInference.ts`); literal widening in an object literal that has a
  contextual type.

## Relations, instantiation, reduction
- **Intersection reduction by constraint** (typeVariableConstraintIntersections): tsc reduces `K & "c"` to never for
  `K extends "a" | "b"`, and `option.kind` of `Options & { kind: K }` is K (getReducedType / intersection with a
  constraint). Related: conflicting discriminants and privates are reduced at construction
  (`has_conflicting_discriminants`) only for non-generic members; tsc reduces lazily, so a generic intersection
  instantiated later isn't re-checked, and the "The intersection 'A & B' was reduced to 'never' because..." elaborations
  (6 tests) need the unreduced intersection. mappedTypeNotMistakenlyHomomorphic (`keyof Gen<ABC.A>` misses `a`),
  mixinAccessModifiers / mixinPrivateAndProtected (mixin intersections of classes with private/protected `p` aren't
  reduced, so no TS2509 on them; protected access through an intersection names the wrong class).
- **Permissive instantiation** for conditional types is missing (`ty_conditional_resolve` defers where tsc picks the
  false branch); a flat `TyMapper` can't say "every type parameter -> any". Needs a wildcard type + cached permissive
  instantiation. (reactHOCSpreadprops, the test it cost, passes now through something else.)
- `mentions_type_variables` (conditional.c) returns "unsure" for a substitution type, so `definitely_related` falls
  back to real constraints. Making it precise made a variance measurement on react-hook-form (bulletproof-react
  form.tsx:40) overflow into TS2589 (check_related's overflow HACK), +30% RSS. Look at why the restrictive relation
  goes that deep first; an overflow while measuring variances should probably mark the measurement unreliable.
- `in`/`out` variance annotations are parsed and dropped (`parse_generic_decl_item()`, `GenericItem` has no field):
  tsc takes the variance from them (getVariancesWorker) and checks them: TS2636 (declared variance disagrees with the
  measured one, marker types), TS2637 (annotation on a type that isn't an object/alias of one), TS1273/TS1274/TS1030
  (misplaced or repeated modifiers). varianceAnnotations (27 errors), varianceReferences, varianceAnnotationValidation,
  varianceAnnotationsWithCircularlyReferencesError, jsdocTemplateTag7/8: ~6 tests. One session: store the modifiers,
  use them in `measure_variances`, port checkVarianceAnnotations.
- **isTupleLikeType's length half**: an array-like context whose `length` is a number literal isn't tuple-like
  (only the "0" property rule is in `context_is_tuple_like`).
- **Conditional true branch with `infer`s**: relating a deferred conditional through its true branch with `infer`s as
  unknown is done only when the check type is generic; with a generic extends type tsc has real inferences.
- conditionalTypes2's remaining errors are elaborations: variance-based `Type 'A' is not assignable to type 'B'` for
  `Covariant<A>` vs `Covariant<B>`, and an intersection source with a bare `T` (`Foo & Bar & T`) reported whole where tsc
  reports its constraint (getEffectiveConstraintOfIntersection).
- TS2430 leftovers: interfaceExtendsObjectIntersectionErrors (missing), mappedTypesAndObjects (spurious TS2322).
- contextualTypeShouldBeLiteral: contextual types of union members are reduced (`string | "done"` is `string`,
  hacks.md), so the discriminant widens and the wrong member is picked (spurious TS2353 + TS2322).
- genericMappedTypeAsClause: `{ a, b }` against the generic `MappedModel<T>` is TS2322 where tsc says TS2353 (the
  excess check doesn't look at the resolved members of a generic mapped type); TS2312 missing.
- genericClassExpressionInFunction: `s.genericVar = 12` on an instance of a class extending a generic class
  expression is "not assignable to type 'V'" (the base's type parameter isn't instantiated).
- genericRecursiveImplicitConstructorErrors3: TS2314 missing, spurious TS2355 (an erroneous return annotation isn't
  `any`-like), spurious TS2339. A failed type-argument-arity call returns `any` where tsc still resolves a signature.
- mergeSymbolReexportedTypeAliasInstantiation: a `type Row2<T>` added by `declare module '.'` to a module re-exporting an
  interface Row2 resolves to the interface (spurious TS2315).
- mappedTypeAsClauseRelationships: one elaboration line too many for `+?` with a remapping `as`.
- A homomorphic mapped type over a `satisfies`/const object doesn't resolve its keys (the `computedOptions` mapped
  type in the tsc corpus's `utilities.ts` printed a huge `keyof "a" & keyof "b"` key). Not re-probed.
- Module augmentation / `export =` merges don't add members (moduleAugmentationsImports3/4,
  moduleAugmentationDoesInterfaceMergeOfReexport; umd-augmentation-1..4 and augmentExportEquals6_1 fail symbols/types).
  moduleAssignmentCompat1-4: `Symbol(A)` without a Decl for a non-instantiated namespace used as a value.

- tsc's non-widening `undefined`: `{}["hi"]` under noImplicitAny is `undefined` and `var x = {}["hi"]` stays
  `undefined`; tttt widens it to any (noImplicitAnyIndexing(Suppressed) fail types on it only). hacks.md `widen_nullable`.
- `infer X extends C`: C is resolved in the scope of the whole extends type (`type_lookup_names`), so another `infer`
  of the same clause isn't visible (tsc: TS2304, inferTypesWithExtends2 passes); unchecked: an `infer` of an *outer*
  conditional's extends type in C.

## Circularity (plan 29 stage 3 leftovers)
- Plan 29's optional stage 4, always-lazy conditional branches (hacks.md `TyCondBranch`): the prototype (`P29_BRANCH`
  in `test/probes/plan29/experiment.patch`, git dbc107b) gave suite +1 but type-fest +2 (writable-deep 2 x TS2345,
  writable TS2589). Only do it once those are understood; then `node_reaches_alias_in_flight()` can go unless
  `prop_judgeable()`/`members_judgeable()` still need it. Validate with all corpora and `./findcrash.py`.
- recursivelyExpandingUnionNoStackoverflow lacks the TS2589 tsc reaches first (tttt's interned instantiations find the
  TS2615 cycle at once), and tsc prints the mapped type with `containsError` in mapped syntax.
- TS4109 missing where tttt catches the cycle elsewhere: circularTypeArgumentsLocalAndOuterNoCrash1,
  circularlyReferentialInterfaceAccessNoCrash. limitDeepInstantiations: `Foo<"true", {}>` never reaches the limit.
- Spurious TS2589: recursiveConditionalTypes (the `unbox(b1)` calls), recursiveTypeReferences1.
- type-fest's `test-d/opaque.ts` (~90% of type-fest's check time) runs into TS2589 at line 68 (`Tagged`/`UnwrapTagged`
  over `TagContainer`) plus four TS7022 tsc doesn't report: a correctness bug with a perf symptom.
- Spurious TS7022 on `const can = composite && o?.e && !out` when the type of `e` mentions an unresolved name (`Map`
  without lib es2015). Probe: test/probes/plan08/alias_optional_chain.ts minus its `@target`.
- `check_with()` (ty/expr.c) computes an expression with a contextual type outside the LazyType, so no cycle detection
  (`ty_binding_default_type` guards `let [x = x] = []` with `defaults_in_flight`; other contextual self-references can
  still overflow the stack).
- selfReferentialDefaultNoStackOverflow wants TS7022 on `export default { mixins: [DefaultSpinner] }` importing itself.

## Classes and declarations
- Plan 32 leftovers: a class extending a base that constructs `any` should get tsc's anyBaseTypeIndexInfo
  (`[x: string]: any`) instead of the `ty_has_unresolved_base()` gate; tsc's display of `new Outer<string>().m` clones
  generic method type parameters (`Outer<string>.Outer.m<U>.In`).
- `this` in a class member's computed name: tsc types it as the class instance, tttt any (computedPropertyNames21/23
  types). computedPropertyNames49/50 pass errors now, fail symbols/types.
- Duplicate class/interface members show as separate symbols (tsc: one merged symbol).
- multipleDeclarations (JS): `F.prototype.m = function` next to `this.m = v` is two symbols in tsc, one member here.
- abstractClassUnionInstantiation fails alone (`-f`) and passes in the full run: its TS2511 errors land on an invalid
  span (see `harness.md`); something order-dependent in the shared lib state.

## Narrowing and flow
- **Evolving arrays** (tsc's autoArrayType: `let x = []`, then `x.push(v)` / `x[i] = v` grow the element type; needs
  FLOW_ARRAY_MUTATION nodes). Auto types (`let x;`) exist, the array half is `any[]` (controlFlowArrays).
- **Destructuring assignment flow order** (controlFlowAssignmentPatternOrder, 12 spurious TS2322;
  controlFlowBindingPatternOrder): assignments in a pattern's computed keys and defaults happen in tsc's evaluation
  order, which the flow builder doesn't follow.
- taggedPrimitiveNarrowing: `typeof hash !== "string"` then `throw` narrows `string & { __hash: true }` to never.
- tsc's requiresScopeChange exceptions are done (the `.2` tests pass); `({ f } = this)` inside method f counts as a
  read of the private f (tsc's rightIsThis self access): noUnusedLocals_destructuringAssignment.
- Destructuring assignment targets are checked as a whole, not property by property (for-of47/48: tsc's
  checkDestructuringAssignment reports at the default). The `{ a = dflt }` default of a shorthand assignment pattern is
  resolved but not checked (`ObjectExprItem.shorthand_default`).
