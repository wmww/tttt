# Type core, inference and flow bugs
Wrong types (not missing diagnostics: `missing_checks.md`; not printing: `display.md`). Test statuses checked
2026-09-27 (triage, suite 8952); "(re-probed)" = the symptom was confirmed then.

## exactOptionalPropertyTypes (leftovers after plan 37 E's missing type)
`TY_MISSING` (documentation.md "The missing type") fixed 3 of the 23 failing configurations and zod-root's TS2412.
Known leftovers (plan 37 E stage 2): inferred candidates keep missing where tsc drops it (contextuallyTypedOptional
Property's TS18048, below under Inference), inferring `T` from `Partial<T>` makes properties required
(strictOptionalProperties1). The rest of `grep -rli
@exactOptionalPropertyTypes thirdparty/tsc/tests/cases` is unsorted.

## Inference
- **First pass skips context-sensitive arguments entirely**: tsc checks them with their functions replaced by
  anyFunctionType and still infers from the (non-inferrable) literal to anything but a naked type parameter. The tests
  that showed it (mappedTypeContextualTypesApplied, correlatedUnions) pass since plan 38 stage 1 without it; look here
  when one needs it.
- Still in the "spurious diagnostics" bucket, not investigated in depth: contravariantOnlyInferenceFromAnnotatedFunction,
  mappedTypeRecursiveInference2.
- Contextual types from a generic mapped type substitute the whole constraint for K, not the property's key.
- promiseTry (spurious TS2578): `Promise.try(cb, ...args)` with `U extends unknown[]` inferred from both the callback's
  parameters and the rest arguments gets no TS2554 arity error.
- contextuallyTypedOptionalProperty(exactOptionalPropertyTypes=true): `T` inferred from `match(y => y > 0)` for an
  optional property keeps `undefined` (tsc strips the missing type), so the operand check says TS18048.
- Older tsc-corpus findings (a probe each): `visitNode(node, visitor, isExpression)` resolving to the overload without
  the predicate (`test/probes/plan05/visitNodeInference.ts`); literal widening in an object literal that has a
  contextual type.

- **Contextual types are instantiated eagerly** (probes `test/probes/plan37/contextual_instantiation.ts`,
  `return_only.ts`; triage 2026-09-28): `f(() => ({ a: true, b: x => x }))` and `k2(() => () => ({ a: true }))`: tsc
  gives the arrow the return type `{ a: boolean }` while the literal node prints `{ a: true }`; tttt the other way
  round. tsc fixes a function's return type at its first contextual check, and instantiateContextualType only replaces
  *instantiable leaves* (with the returnMapper's inferences, not earlier arguments'), so `true` meets raw `T` and
  widens; the final check against the resolved signature keeps the literal. tttt instantiates the whole parameter type
  with the inferences so far (`ty_instantiate(me, param_type, context_mapper)` in call.c's second pass) and memoizes
  the contextual return (`KEY_CONTEXT_RETURN`). Same root as the two bullets above (defaults leaking, eager return
  types) and as `k3(y => () => ...)` being `boolean` where tsc fixes T without candidates (`unknown`): a plan-sized
  port of leaf-wise contextual instantiation, together with plan 37's "fixing without candidates".
- Laziness artifacts of tsc's inference (triage 2026-09-29, both need lazy return/member types to reproduce):
  genericCallWithGenericSignatureArguments3's `(n: Object) => 1` keeps its literal because tsc resolves the inner
  arrow's return type only after U's first candidate is that arrow's own function type: its contextual signature is
  then its own (`contextualSignature === getSignatureFromDeclaration(func)` in getReturnTypeFromBody, contextual type
  = the return type itself). mappedTypeInferenceErrors infers `C = { bar: unknown; baz: unknown }` where tttt has
  `bar: number`: tsc's reverse mapped member types resolve lazily, while the method body's `this.bar` is being checked.
- Return-type inference with Pick: hacks.md `infer_from_objects()` (twiceNestedKeyofIndexInference's K2 becomes
  `never` in `return_inference_mapper()` once K gets the keys; how tsc's returnMapper avoids that is unknown).
  indexedAccessRelation still differs in its elaboration (tsc: `Type 'Foo' is not assignable to type 'S["a"] & T'`,
  the constraint of T) and in the union order of `keyof S | "a"`.

## Relations, instantiation, reduction
- Distributive conditional base constraint: with `S2<T> = T extends string ? T : number`,
  `g2<T extends unknown[]>(x: [...{ [K in keyof T]: S2<T[K]> }])` widens `g2(['a'])` to `[string]`, tsc `["a"]` (its
  forConstraint constraint of `S2<T[number]>` keeps the true branch with T's implied `string`).
- `StateId<U>` to `StateId<T>` (xstate's StateId, no variance measurement) gives TS2589 next to the TS2322 tsc also
  reports: the definitely-assignable checks instantiate deeply nested indexed-access chains. Repro:
  test/probes/xstate/state_id_deep_nesting.ts.
- inferConditionalConstraintMappedMember (types): `{ [K in keyof T]: X } extends { [_ in keyof T]: infer U } ? U : never`
  has constraint X in tsc (K4 variant with template `1` gives number), tttt unknown; why tsc infers there despite the
  generic mapped check type is unclear. Kept out of TS2344 by the deferred-conditional HACK.
- **Intersection reduction by constraint**: `T & P` for a type parameter T with a primitive constraint is reduced
  (`reduce_type_variable_intersection`, and `(T & "a") | (T & "b")` back to T), but not for indexed access type
  variables (see hacks.md) nor type parameters with a deferred declared constraint. Related: conflicting discriminants and privates are reduced at construction
  (`has_conflicting_discriminants`) only for non-generic members; tsc reduces lazily, so a generic intersection
  instantiated later isn't re-checked, and the "The intersection 'A & B' was reduced to 'never' because..." elaborations
  (6 tests) need the unreduced intersection. mappedTypeNotMistakenlyHomomorphic (`keyof Gen<ABC.A>` misses `a`),
  mixinAccessModifiers / mixinPrivateAndProtected (mixin intersections of classes with private/protected `p` aren't
  reduced, so no TS2509 on them; protected access through an intersection names the wrong class).
- **Permissive instantiation** for conditional types is missing (`ty_conditional_resolve` defers where tsc picks the
  false branch); a flat `TyMapper` can't say "every type parameter -> any". Needs a wildcard type + cached permissive
  instantiation.
- `mentions_type_variables` (conditional.c) returns "unsure" for a substitution type, so `definitely_related` falls
  back to real constraints. Making it precise made a variance measurement on react-hook-form (bulletproof-react
  form.tsx:40) overflow into TS2589 (check_related's overflow HACK), +30% RSS. Look at why the restrictive relation
  goes that deep first; an overflow while measuring variances should probably mark the measurement unreliable.
- **isTupleLikeType's length half**: an array-like context whose `length` is a number literal isn't tuple-like
  (only the "0" property rule is in `context_is_tuple_like`).
- **Conditional true branch with `infer`s**: relating a deferred conditional through its true branch with `infer`s as
  unknown is done only when the check type is generic; with a generic extends type tsc has real inferences.
- conditionalTypes2's remaining errors are elaborations: variance-based `Type 'A' is not assignable to type 'B'` for
  `Covariant<A>` vs `Covariant<B>`, and an intersection source with a bare `T` (`Foo & Bar & T`) reported whole where tsc
  reports its constraint (getEffectiveConstraintOfIntersection).
- TS2430 leftovers: interfaceExtendsObjectIntersectionErrors (missing).
- genericMappedTypeAsClause: `{ a, b }` against the generic `MappedModel<T>` is TS2322 where tsc says TS2353 (the
  excess check doesn't look at the resolved members of a generic mapped type); TS2312 missing.
- mergeSymbolReexportedTypeAliasInstantiation: a `type Row2<T>` added by `declare module '.'` to a module re-exporting an
  interface Row2 resolves to the interface (spurious TS2315).
- A homomorphic mapped type over a `satisfies`/const object doesn't resolve its keys (the `computedOptions` mapped
  type in the tsc corpus's `utilities.ts` printed a huge `keyof "a" & keyof "b"` key). Not re-probed.
- Module augmentation / `export =` merges don't add members (moduleAugmentationsImports3/4,
  moduleAugmentationDoesInterfaceMergeOfReexport; umd-augmentation-1..4 fail symbols/types).

- Base constraint of an indexed access comes out wrong in valibot: in
  `library/src/methods/required/requiredAsync.ts` (the `'~run'` member's `InferIssue<TSchema>`, TSchema constrained
  to `SchemaWithoutPipe<LooseObjectSchema<...> | ...>` = `(A | B | ...) & { pipe?: never }`), `ty_base_constraint` of
  `TSchema['~types']` is `undefined`, where tsc has `{ input; output; issue } | undefined`. Found when constrained type
  variable reduction (`reduce_type_variable_intersection`) turned `TSchema['~types'] & {}` into never; not reduced to
  a small repro (a two-member union of interfaces with an optional `'~types'` under exactOptionalPropertyTypes is
  fine). Possibly a constraint computed while an alias or the type parameter's constraint is in flight and memoized.

- **`awaitedTypeStrictNull` overflows the stack** in `instantiate_mapped_over_list` under the debug build + valgrind
  (8MB main stack; the release build passes). Deep but maybe not infinite recursion; worth checking whether the
  recursion is bounded.

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
- Loading webpack through a symlinked path loads its files twice, and then two diagnostics still depend on the checker
  count (`explicit-vendor-chunk/webpack.config.js:7` TS2859 at K=1 / TS2589 at K=4, `lazy-compilation/webpack.config.js`):
  another depth-limit order dependence. The real path (what the scripts use) is identical at every K.

## Circularity (plan 29 stage 3 leftovers; the outer-declaration cases are plan 39)
- recursivelyExpandingUnionNoStackoverflow lacks the TS2589 tsc reaches first (tttt's interned instantiations find the
  TS2615 cycle at once), and tsc prints the mapped type with `containsError` in mapped syntax.
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

## Classes and declarations
- Plan 32 leftovers: a class extending a base that constructs `any` should get tsc's anyBaseTypeIndexInfo
  (`[x: string]: any`) instead of the `ty_has_unresolved_base()` gate; tsc's display of `new Outer<string>().m` clones
  generic method type parameters (`Outer<string>.Outer.m<U>.In`).
- `this` in a class member's computed name: tsc types it as the class instance, tttt any (computedPropertyNames21/23
  types). computedPropertyNames49/50 pass errors now, fail symbols/types.
- Duplicate class/interface members show as separate symbols (tsc: one merged symbol).
- multipleDeclarations (JS): `F.prototype.m = function` next to `this.m = v` is two symbols in tsc, one member here.

## Narrowing and flow
- dependentDestructuredVariables: narrowing destructured variables by each other (tsc's
  getCandidateDiscriminantPropertyAccess on binding elements) isn't implemented: 20 spurious TS2339 (its TS7022s are
  plan 39).
- The JSX attribute types (`KEY_JSX_ATTRIBUTE`) and an IIFE parameter's contextual type depend on check order (the
  last check wins). (An IIFE's parameters are pushed from `ty_quick_expr_type()` too now: before, a variable
  referenced above its IIFE initializer typed the function without them.)
- Destructuring assignment targets are checked as a whole, not property by property (for-of47/48: tsc's
  checkDestructuringAssignment reports at the default). The `{ a = dflt }` default of a shorthand assignment pattern is
  resolved but not checked (`ObjectExprItem.shorthand_default`).
