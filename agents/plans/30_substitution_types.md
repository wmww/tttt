# Plan 30: constraints of indexed accesses, then substitution types

Written by a planning session on 2026-09-27. **Status: stages Q, 1 and 2 done** (2026-09-27: suite 8603 -> 8613 -> 8616,
valibot 54 -> 50, tsc 0, type-fest 48, react 1; callgrind on the tsc corpus +0.5%, +0.2%). Stage 3 open.

### What stage 2 did (for stage 3's author)
- TY_SUBSTITUTION (base, constraint), `ty_substitution()`/`ty_no_infer()`/`ty_substitution_intersection()`/
  `ty_actual_type_variable()` (checker.c). Made at the end of `lower()` by `conditional_flow_type()` (lower.c) from
  `TypeNode.flow`: the chain went into the *syntax* after all (the plan's fallback idea, but as pointers rather than
  spans): `type_lookup_names()` gives every node the chain of `TernaryType.true_flow` entries around it (two per
  conditional, one per parity; parameters flip, function bodies cut). So the determinism audit wasn't needed: any
  lowering of a node sees the same chain. Check/extends types per conditional are cached (KEY_IMPLIED_CONSTRAINT).
- The checker's true-branch stack (`substituted()`) is gone. Conditional types resolve on the actual type variable
  of the check type; a substituted check type isn't distributive (tsc's isDistributive is `checkType.flags &
  TypeParameter`).
- What it took besides the plan: free params of anonymous types and conditional outer params include the type
  parameters of the extends types of the true branches around a node (isTypeParameterPossiblyReferenced: *not* the
  check types, which made type-fest's PartialOnUndefinedDeep 300x slower); a true branch is instantiated with the
  infer mapper composed into its own in one go (`branch_type_with`, conditional.c); homomorphic mapped types find
  their variable through the substitution (`instantiate_mapped_over_list`); `distributive_constraint` keeps
  base_constraint's depth; genericity of a substitution is its parts' (`NoInfer<{x: "foo"}>` isn't generic: elaboration
  indexes into it); the argument error head names the base.
- Not done / left: item 7's audit was done by test fallout only (62 `kind == TY_TYPE_PARAM` sites, the ones that
  mattered were homomorphism, distributivity, inference targets); the mapped-type half of getConditionalFlowTypeOfType;
  noInfer's `mutate` case and conditionalTypes2's elaborations (`bugs/type_core.md`).
Most of the triage cluster was **not** substitution types but tsc's *simplification* of indexed accesses
(getSimplifiedIndexedAccessType), now `ty_simplified` (relate.c). Substitution types are the smaller-yield stage left.
Probes: `test/probes/plan30/`.

### What stages Q and 1 did (for stage 2's author)
- Restrictive check: `definitely_related` (conditional.c) collects the free type parameters with
  `mentions_type_variables` in collect mode and maps them to cached unconstrained clones (KEY_RESTRICTIVE_PARAM). The
  collect walk must not look inside lazy parts (it lowered the alias being declared: spurious TS2456 on type-fest's
  ConditionalPickDeep): anonymous types use their syntactic `free_params`, conditional types their outer params, and an
  unknown outer (`typeof x` inside) makes it "unsure", which falls back to the real constraints. Stage 2's instantiation
  rule can reuse `restrictive_type_param`.
- `ty_simplified(ty, writing)` memoised per (id, writing) in links (KEY_SIMPLIFIED), itself while in flight; applied
  to both sides at the top of `is_related_to`. `constraint_of_indexed` is getConstraintFromIndexedAccess (index
  constraint first, then object, deferred results kept), `base_constraint` simplifies first and has the
  isMappedTypeGenericIndexedAccess case (`Members.expanded_from`, now also carried by instantiated members),
  `write_constraint_of_indexed` is tsc's (base constraints, non-generic, intersection over a union index).
- Found on the way: `apparent_type` now maps indexed/conditional types to their base constraint (a `T[K]` callee);
  getNarrowableTypeForReference now covers property/element accesses and the contextual trigger (the plan's predicted
  controlFlowComputedPropertyNames loss and nonNullMappedType); `awaited_type_needed` treats a mapped type as its own
  base constraint (awaitedType regressed otherwise).
- Not done: getSimplifiedConditionalType, generic tuple by number, NoIndexSignatures on the write side (hacks.md).
  typeVariableConstraintIntersections still fails for another reason (`bugs/type_core.md`, intersection reduction by constraint).

## Why
Triage items sharing this root (`agents/triage.md` "From the suite checker bugs session" and "Substitution types";
plan 19 R17):
- (b) `o[k]` with a generic object and generic key goes through `ty_access` (any) instead of the deferred `O[K]`
  (`generic_element_access`, ty/expr.c). **The earlier "14 regressions" came from also deferring generic objects with a
  non-generic key**: tsc resolves `t["a"]` / `this[sym]` eagerly through the constraint in expressions (the
  `accessNode.kind !== IndexedAccessType` branch of getIndexedAccessTypeOrUndefined). Deferring only generic *keys*
  (`if (!ty_is_generic_index(key)) return nullptr;`) is +9 / -5 components; all 5 losses (typeParameterExtendsPrimitive,
  additionOperatorWithConstrainedTypeParameter, literalTypeWidening, nonNullMappedType,
  controlFlowComputedPropertyNames) are `Record<K, X>[K]` / `{ [P in A]: X }[A]`, which tsc simplifies to `X`.
- (c) `{ [P in K]: E }[T]` over a *non-generic* mapped type (mappedTypeGenericIndexedAccess,
  typeVariableConstraintIntersections): tsc's isMappedTypeGenericIndexedAccess / substituteIndexedMappedType.
- (a) generic TS2536: of the 16 regressions of the earlier attempt, re-run on HEAD, the causes are: implied constraints
  of conditional true branches (conditionalTypeSubclassExtendsTypeParam `[K] extends [keyof MR] ? MR[K]`,
  recursiveConditionalEvaluationNonInfinite `[T] extends [any[]] ? T[0]`, inferenceAndHKTs `F extends {type: unknown}
  ? (F & X)['type']`), missing simplification (deferredLookupTypeResolution2: `({[K in S]: 'true'} & {[k: string]:
  'false'})['1']` distributes to never), `keyof X` targets only seeing through a type *parameter* X (TY_KEYOF case of
  the target switch in `structured_related`, relate.c: identityAndDivergentNormalizedTypes `keyof Extract<...>`,
  nonNullableWithNullableGenericIndexedAccessArg `keyof (T["order"] & {})`), remapped mapped types (tsc indexes those by
  getIndexTypeForMappedType: intersectionConstraintReduction), plus two to investigate (indexedAccessRetainsIndexSignature
  `{[P in Diff<keyof T, K>]: T[P]}`, intersectionReductionGenericStringLikeType `key & template` index).
- Substitution types proper: conditionalTypes2 (5 spurious TS2322/TS2345 on `Extract<T, Function>`, whose true branch
  `T` is `T & Function` in tsc), `T[K] extends unknown[] ? (...args: T[K]) => void` (spurious TS2370, probe
  `true_branch_constraint.ts`), type-fest shared-union-fields-deep (R17, 8), the three TS2536 cases above, NoInfer
  (noInfer, noInferRedeclaration, narrowingNoInfer1... types; hacks.md "NoInfer<T> is plain T").
- Stale or different root: valibot `args.ts:43` no longer reproduces (gone from the corpus output; drop the triage
  line). numericStringLiteralTypes' TS2370 is tsc's getResolvedApparentTypeOfMappedType (a homomorphic mapped type
  over an array-constrained T is array-like), not substitution types: separate triage item.
- Found on the way: valibot `unwrap.ts:103` (`return schema.wrapped` to `TSchema['wrapped']`) is
  `write_constraint_of_indexed` (relate.c) *intersecting* over a union constraint of T. tsc's write constraint
  (getIndexedAccessTypeOrUndefined with AccessFlags.Writing) intersects only over a union *index*; a union object reads
  the union property's type (probe `write_constraint.ts`).

## What tttt has today
- `TY_INDEXED` (object, index), made by `ty_indexed()` (checker.c), deferred when `is_generic_operand()`. No
  simplification anywhere: `grep -i simplif src/ty` is empty.
- Constraints: `base_constraint()` (relate.c, tsc's computeBaseConstraint without the getSimplifiedType call it
  starts with), `constraint_of_indexed()` (source side of relations, only sees through TY_TYPE_PARAM / intersection
  operands and *rejects deferred results*, which stops `T[K]` -> `Record<K,number>[K]` -> `number`),
  `write_constraint_of_indexed()` (target side), `distributive_constraint()`.
- Implied constraints: only `ty_check_indexed_access_type` knows them, from the checker's true-branch stack
  (`ty_enter_true_branch`, `substituted()`, check.c; fed by the check walk in type.c). Lowering has none.
- Homomorphism is syntactic: `in_type.tag == TYPE_KEYOF` in `members.c` (`keyof_constraint`, `modifiers_type`) and
  `instantiate_mapped_over_list` (instantiate.c).
- TypeNodes have no parent pointers and lowering isn't memoised per node: `ty_of_node()` re-lowers with a fresh
  `LowerCtx`. Lazy pieces keep their `LowerCtx` (`TyCondBranch.ctx`, `anonymous.ctx`, `prop->ctx`, `sig->ctx`).

## Stages

### Stage Q (quick, half a session): tsc's write constraint + restrictive "definitely true"
1. `write_constraint_of_indexed`: drop the intersection over a union object constraint; intersect only over a union
   index (tsc: `accessFlags & Writing ? getIntersectionType(propTypes)` in the index-union loop). Alone: suite +1
   (functionCallOnConstrainedTypeVariable), valibot -1 (unwrap.ts), **but the tsc corpus gets 2 errors**
   (esDecorators.ts 1018/1062, probe `extract_pick.ts`): `Extract<CL, Pick<T, "kind">>` resolves to its true branch
   for members that only relate through T's constraint. tsc only takes the true branch if the check holds under the
   *restrictive* instantiation (type parameters without constraints: getRestrictiveInstantiation in
   getConditionalType); tttt uses the real constraints (`ty_conditional_resolve`, conditional.c; the neighbouring
   HACK comment is the permissive half of the same rule). The intersection was masking it.
2. So first add the restrictive check: either a mapper kind that maps every type parameter to a cached unconstrained
   clone (tsc's restrictiveMapper/getRestrictiveTypeParameter; mappers are lists today, so this is new in
   instantiate.c), or a relation variant (own cache slot, like TY_RELATION_ASSIGNABLE_STRICT_NULLS) in which
   `ty_param_constraint` answers "none". Prefer the mapper if cheap: stage 2 needs the same test when instantiating a
   substitution type. Only run it when either side mentions type variables.
3. Validate: tsc corpus 0, valibot 59, suite no losses.

### Stage 1 (one session): tsc's simplification and indexed-access constraints
Port as functions next to `base_constraint` (relate.c) or a new `ty/indexed.c`:
1. `ty_simplified(ty, writing)` = getSimplifiedType: for TY_INDEXED getSimplifiedIndexedAccessType (union index
   distributes: union when reading, intersection when writing; union/intersection object distributes when the index
   isn't instantiable, except intersections tsc wouldn't distribute (shouldDeferIndexType: instantiable member plus
   `{}`, as in `NonNullable<X>`); generic tuple by number; a generic mapped object without a remapping `as` ->
   substituteIndexedMappedType). Conditional part (getSimplifiedConditionalType, `T extends U ? T : never`) can
   wait. Memoise per (id, writing) in the links table (tsc's simplifiedForReading/Writing), with an in-flight marker
   (tsc returns the type itself on re-entry).
2. `substitute_indexed_mapped(mapped, index)`: template with the key parameter := index, plus `undefined` when `?` or
   (generic modifiers type) any mapped type in it has `?`, or (non-generic) some optional property's key fits the
   index constraint (couldAccessOptionalProperty).
3. `base_constraint`: simplify first (getResolvedBaseConstraint does `computeBaseConstraint(getSimplifiedType(t))`),
   and the isMappedTypeGenericIndexedAccess case: `Members.expanded_from` (non-generic mapped object) with a generic
   index, no `as`, no `-?`.
4. `constraint_of_indexed` = getConstraintFromIndexedAccess: mapped-generic case, then index constraint
   (getSimplifiedTypeOrConstraint), then object constraint; keep deferred results (the relation recurses on them).
5. Relations: normalise with `ty_simplified` like getNormalizedType (source reading, target writing) at the
   TY_INDEXED cases of `structured_related`.
6. TY_KEYOF target: through `getSimplifiedTypeOrConstraint(operand)` for any operand (conditional, indexed,
   intersection), not only TY_TYPE_PARAM.
7. Drop the bail-out: `generic_element_access` defers when the *key* is generic, whatever the object.
Prototype (items 3, 4 (partly), 5 and 7, mapped-type substitution only, no memo; deleted once stage 1 landed): suite **+8
full, 18 components gained, 1 lost** (controlFlowComputedPropertyNames: `(Record<K, string | undefined>[K] & {})
.toUpperCase`, the apparent type of an intersection with a deferred member; fix it, don't accept it), corpora
unchanged except valibot pick.ts:475 (an expected error now appears). typeVariableConstraintIntersections still
fails (`optionHandlers[kind](option)`: the call path doesn't reach the constraint; check `ty_apparent_type` /
call.c's signature lookup on a TY_INDEXED callee). Expected with the full stage: nonNullMappedType, literalTypeWidening,
mappedTypeGenericIndexedAccess (errors), correlatedUnions, indexedAccessNormalization, keyofAndForIn,
typeGuardsTypeParameters, asyncFunctionReturnType, deeplyNestedConstraints...

### Stage 2 (done, see above): substitution types
1. **Representation**: `TY_SUBSTITUTION` (base, constraint), interned by (base id, constraint id), flags
   TYF_DEFERRED | base flags' generic bits. `ty_substitution(base, constraint)` = getSubstitutionType: returns base if
   constraint is any/unknown or equals base, or base is any. NoInfer: `constraint = unknown` marks it (tsc's
   isNoInferType), made by `intrinsic_alias_instance` (lower.c) instead of plain T.
2. **Creation** (getConditionalFlowTypeOfType): `LowerCtx` gets a pointer to an interned chain of
   `{ check Ty, extends Ty, check node, extends node, parent }` pushed when lowering a TYPE_TERNARY's true branch
   (`lower`, case TYPE_TERNARY, `inner` ctx; also reaches `lower_branch`'s lazy TyCondBranch), plus a `covariant` bit
   flipped by parameter positions (signature parameters in `signature_from_function` & co). At the end of `lower()`,
   for a non-empty chain: collect every entry whose check type is the node's type (compare
   `actual_type_variable()` of both; unary tuple nodes `[A] extends [B]` recurse like getImpliedConstraint), only in
   covariant positions unless the type is a type variable (TY_TYPE_PARAM or TY_INDEXED); wrap in a substitution with
   the intersection of their extends types. The mapped-type rule of the same function (the key of a homomorphic
   mapped type over an array-constrained T gets `number | ${number}`) is optional here.
   **The hard part**: the chain must be the same whoever lowers the node. `ty_of_node()` (check walk) builds it from
   the checker's true-branch stack (extend type.c's `ty_enter_true_branch` to push nodes and track parameter
   variance); every other fresh-ctx lowering of an inner node (`(LowerCtx){ .generic = true }` in members.c,
   call.c, iterate.c) must be audited: roots are fine, inner nodes are not. Anonymous types are interned by node:
   include the chain pointer in that key or the first lowering wins. Plan 29 (deferred alias/type reference
   resolution) must carry the chain in whatever it stores for later lowering; the in-progress lazy mapped template
   (`template_ctx`) likewise.
3. **Instantiation** (instantiateType's Substitution case): new base, new constraint; base a type variable and
   constraint generic -> substitution; constraint any/unknown or restrictive-assignable (stage Q) -> base; base a type
   variable -> substitution; else `constraint & base`.
4. **Relations**: source -> `base & constraint` (getSubstitutionIntersection), target -> base (getNormalizedType's
   writing side); identity: both parts. `base_constraint`: of the intersection. `ty_is_generic_object`/`index`:
   either part. `ty_keyof` / `ty_indexed` on one: deferred like any type variable.
5. **Inference**: target -> its base (getActualTypeVariable); source -> infer from base, then from the intersection at
   a new lower priority (INFER_PRIORITY_SUBSTITUTE_SOURCE); NoInfer targets infer nothing.
6. **Printing**: the base (NoInfer: `NoInfer<base>`), in `ty_display.c`, including alias-argument positions.
7. **Everything that asks `kind == TY_TYPE_PARAM`** (82 sites, 35 in relate.c): decide per site whether a
   substitution of T is T there. tsc's rule of thumb: getActualTypeVariable where identity matters (distributivity,
   homomorphic variable, implied-constraint matching, inference targets), the substitution itself elsewhere.
8. Delete `substituted()`/`true_branch_*` from the Checker once lowering carries it (hacks.md entry goes too).

### Stage 3 (one session): the dependent fixes
1. Generic TS2536 in `ty_check_indexed_access_type` (check.c) and element accesses (`check_access`, the TODO at the
   type-parameter index): checkIndexedAccessIndexType, i.e. every member of the index assignable to `keyof O` (a
   remapping mapped type: its getIndexTypeForMappedType) or numeric with a number index signature on O's apparent
   type; TS2536 "Type 'K' cannot be used to index type 'T'." (probe `generic_ts2536.ts` has tsc's three). The
   prototype is `test/probes/plan30/ts2536_proto.txt`: with stages 1-2 in, re-run it and check the "investigate"
   pair. Wanted by ~8 tests (constraintWithIndexedAccess, infiniteConstraints, mappedTypeErrors2...).
2. R17: homomorphism from the lowered constraint (`mapped->constraint->kind == TY_KEYOF` of a type parameter via
   getActualTypeVariable, getHomomorphicTypeVariable) instead of `in_type.tag == TYPE_KEYOF`; keep the syntactic test
   only as the cheap prefilter in `instantiate_mapped_over_list` and for `modifiers_type` (tsc's
   getModifiersTypeFromMappedType is syntactic too). Probe `test/probes/plan19/typefest/suf_dist.ts`.

## Validation
- Per stage: `./fasttest.sh --summary` with no lost component (or each loss explained in the commit), then
  `./fasttest.sh -s`. Named tests: stage Q functionCallOnConstrainedTypeVariable; stage 1 the list above plus
  typeVariableConstraintIntersections; stage 2 conditionalTypes2, noInfer*, `true_branch_constraint.ts`; stage 3
  constraintWithIndexedAccess, infiniteConstraints, mappedTypeErrors2, and the 16 regressions of the earlier TS2536
  attempt (conditionalTypeSubclassExtendsTypeParam, inferenceAndHKTs, deferredLookupTypeResolution2...) staying green.
- `./realworld.py` must stay 0 after every stage (stage Q is exactly where it breaks). `--corpus valibot` 60 -> 58
  after stages Q+1; `--corpus type-fest` -8 after stage 3 (shared-union-fields-deep); react unchanged.
- Probes in `test/probes/plan30/` with `FAST=1 ./filetest.sh`.
- Perf: callgrind on the tsc corpus before/after stages 1 and 2 (simplification runs in every indexed relation: it
  must be memoised; substitution creation must cost nothing when the chain is empty). `./bench.sh`, measurements.md.

## Risks
- Stricter relations expose latent inference bugs (vision.md: budget for them): `o[k]` stops being `any` in many
  generic function bodies, so errors appear where tttt was silently lenient. That's correct, but expect a tail.
- Stage 2's chain determinism (above) is the design risk; if the audit finds too many inner fresh-ctx lowerings, the
  alternative is a pre-pass that records, per TYPE_TERNARY true branch, the node range it covers, so any node can find
  its enclosing true branches by span (spans are ordered; true branches nest).
- Printing: tsc prints the base, but union/intersection *ordering* by id changes when a substitution is interned
  first; watch `./displaydiff.py` after stage 2.
- Plan 29 may change when/where type references are lowered; coordinate so the chain travels with deferred nodes.
