# Plan 11: generic functions as arguments (context instantiation, higher-order inference)

## Why
~50 of the 91 corpus diagnostics come from a generic function type meeting a non-generic signature, in three places
where tttt erases the source's type parameters (to their constraint when inferring, to `any` when relating) instead
of doing what tsc does:
- **TS2558 38 + TS2345 3 + TS2322 1, nodeFactory.ts**: `memoizeOne<A, T>(callback: (arg: A) => T): (arg: A) => T`
  applied to `<T extends JSDocType>(kind: T["kind"]) => () => ...` must return the *generic* `<T extends JSDocType>
  (arg: T["kind"]) => () => Mutable<T>` (tsc 3.4 higher-order inference), so `getJSDocPrimaryTypeCreateFunction<
  JSDocAllType>(...)` is legal. tttt infers `A`/`T` from the base signature (`base_signature_mapper`: `T` := its
  constraint), gets a non-generic result and reports TS2558 on every use; the TS2345s at 502/507/509 are the argument
  check through `compare_signatures`' erasure (`Mutable<any>`), the TS2322 at 512 the `factory` literal holding the
  wrongly typed `createJSDocAllType`.
- **TS2322 parenthesizerRules.ts 662, tsbuildPublic.ts 945**: `cast(nodes, isNodeArray)` and
  `withProgramOrUndefined(identity)`: a generic function passed where the contextual signature is non-generic is
  instantiated *in the context of* that signature (`isNodeArray`'s `T` := `Expression` from `(value: readonly
  Expression[]) => value is TOut`), not erased. Same machinery, no higher-order propagation (the outer call doesn't
  return a function).
- **TS2322 moduleNameResolver.ts 1080** is a different root cause in the same area: `getOrCreate(m, dir, () =>
  createModeAwareCache())` infers `MCache<unknown>` because the lambda's return type is computed after
  `my outer_inference` is gone (see stage 0). Folded in with the triage probe `nested_lambda_context.ts`, which is
  the other half of "the inference context doesn't reach lazily consumed contextual types".
- Suite: 16 of these fail today: `genericFunctionInference1/2`, `inferFromGenericFunctionReturnTypes2/3`,
  `genericFunctionsAndConditionalInference`, `genericFunctionHasFreshTypeArgs`, `contextualSignatureInstantiation`
  (+1-4, `...WithTypeParameterConstrainedToOuterTypeParameter`), `genericCallWithGenericSignatureArguments` (+2, 3),
  `genericContextualTypes1`; triage's `asyncYieldStarContextualType` (`promise.then(mapper)`). Suite TS2558 is not
  this (`./failstats.py --code TS2558`: instantiation expressions, class heritage).

Probes (`test/probes/plan11/`, all failing today, tsc side via `./tscbaseline.sh`): `genericSigRelation.ts`
(stage 1: tttt misses `(x: string[]) => number = <T>(x: T[]) => T`), `genericArgInContext.ts` (stage 2),
`memoizeOne.ts` + `pipe.ts` (stage 3: `pipe(list, box)` is `<T>(a: T) => { value: T[] }`, `wrap(<U>(u: U) => [u])<string>`),
`nestedReturnContext.ts` + `../triage/nested_lambda_context.ts` (stage 0). `pipe.ts` also expects tsc's TS2345 on
`compose(list, box)` (tsc can't do right-to-left composition; tttt currently accepts it via `any` erasure).

## How tsc does it (checker.ts)
- `instantiateSignatureInContextOf(sig, contextual, ctx?)`: a fresh inference context over `sig`'s type parameters;
  infer from the contextual signature's parameters (instantiated with `ctx.mapper`, the *fixing* one, or
  `nonFixingMapper` if its rest type is a type parameter) to `sig`'s; without `ctx` also from contextual return to
  `sig`'s return at `ReturnType` priority; `getSignatureInstantiation(sig, getInferredTypes(...))`.
- `compareSignaturesRelated`: a source with type parameters other than the target's is replaced by
  `instantiateSignatureInContextOf(source, getCanonicalSignature(target), undefined, compareTypes)` before comparing
  (constraint checks of the inferred types go through the relation in progress). tttt's target type parameters are
  already canonical (instantiations share type parameters, `instantiate_outer`), so `getCanonicalSignature` is a no-op.
- `instantiateTypeWithSingleGenericCallSignature` (applied by `checkExpression` in `Inferential` mode, ie to generic
  call arguments): the expression's type has one generic call (or construct) signature, the contextual type one
  non-generic. In `resolveCall`'s first round (`SkipGenericFunctions`) such arguments are skipped (non-inferrable
  `anyFunctionType`, flag `SkippedGenericFunction`), and the round is redone with them. Then:
  - **Higher-order**: if the outer call's signature returns a single non-generic call/construct signature and not
    every outer type parameter has candidates: take the argument's signature with its *own* type parameters as
    arguments (renamed `T_1` on a name clash with ones already propagated: `getUniqueTypeParameters`), infer from its
    parameters to the contextual signature's into a fresh candidate set (contravariant, priority 0), then from the
    returns; if it produced candidates that don't overlap the context's existing ones, adopt them
    (`mergeInferences`), append the type parameters to `context.inferredTypeParameters`, and return the type of the
    instantiated signature (`getOrCreateTypeFromSignature`).
  - Otherwise `instantiateSignatureInContextOf(sig, contextual, context)`.
- `getSignatureInstantiation(candidate, typeArgs, js, inferredTypeParameters)`: if the instantiated return type has a
  single call/construct signature, the result's return type is a clone of it with `typeParameters =
  inferredTypeParameters`. That is where the generic result (and so the legal `<JSDocAllType>`) comes from.
- `isGenericFunctionReturningFunction` (line ~36352): a *call* argument `g(x)` whose callee is generic and returns a
  function is also skipped in the first round, so `pipe(f(), list)` sees the outer inferences first.

## Where tttt diverges
- `infer.c: infer_from_signatures` always erases a generic source through `base_signature_mapper` (right for tsc's
  `inferFromSignature(getBaseSignature(...))` inside type inference; wrong as the only treatment of generic
  arguments: tsc never gets there for them, it instantiates first).
- `relate.c: compare_signatures`: positional unification with same-arity generic targets, else `any` erasure
  (`hacks.md`). No inference.
- `call.c: infer_type_arguments`: two passes (non-sensitive, then context-sensitive); no notion of a generic function
  argument; `Candidate` has no inferred type parameters, `candidate_return_type` just instantiates.
- No way to make a type from a bare `Signature` (tsc's `getOrCreateTypeFromSignature`): anonymous types come from a
  node, a `FunctionImpl`, or `{target, mapper}`.
- Signature instantiations aren't cached (`relate.c: instantiate_signature` allocates per call; only
  `erased_signature` memoises). With inference in the relation that becomes an arena leak on hot paths.

## Design
- **`ty_signature_instantiation(me, sig, TyList args)`** (members.c or instantiate.c): memoised by (sig, arg ids)
  (new `KEY_SIGNATURE_INSTANCE`), type parameters dropped, like tsc's `getSignatureInstantiationWithoutFillingIn
  TypeArguments`. Replaces `relate.c: instantiate_signature` and `erased_signature`'s allocation.
- **`ty_signature_type(me, sig)`**: a `TY_ANONYMOUS` whose members are exactly one call (or construct) signature,
  interned per `Signature*` (new field `Ty.anonymous.signature`, `free_params_unknown` so `ty_instantiate` keeps the
  whole mapper). Display must print it like any function type (`<T extends Node>(arg: T["kind"]) => () => T`).
- **`instantiate_signature_in_context_of(me, sig, contextual, Inference* outer, TyMapper const* outer_mapper)`**
  (infer.c): a local `Inference` over `sig->type_params`, `infer_types` target->source per parameter (reuse the
  parameter walk of `infer_from_signatures`, which wants splitting into "apply to parameter types" and "apply to
  return types" helpers like tsc's `applyToParameterTypes`/`applyToReturnTypes`), returns only without `outer`, then
  tsc's `getInferredTypes` (no literal widening subtleties needed here; unknown/default/constraint fallback, constraint
  check). Decide whether to share `call.c: inference_mapper` (move it to infer.c) or write the small variant.
- **Higher-order state on the call's inference**: `Inference.inferred_type_params` (a growable TyList) plus
  `inference_merge(dst, src)` for the no-overlap adoption. `Candidate.inferred_type_params`; `candidate_return_type`
  wraps: instantiated return type with a single call/construct signature -> `ty_signature_type` of a signature whose
  target is that one (identity mapper or none) and whose `type_params` are the inferred ones. The explicit type
  arguments path (`explicit_type_arguments`, TS2558/TS2344) then works unchanged on the result.
- **Generic-argument pass in `infer_type_arguments`**: after pass 1, before the context-sensitive pass (tsc redoes the
  whole round; check whether ordering matters on the named tests), each argument whose checked type has a single
  generic call/construct signature (and no properties? tsc: `getSingleSignature(type, kind, allowMembers=true)`) and
  whose parameter type (instantiated with the current inference mapper, instantiable parts only) has a single
  non-generic one: higher-order branch or context instantiation, then `infer_types` from the resulting type. Pass 1
  must not infer from these arguments (check them for the memo, skip `infer_from_arg`). `arg->type` stays the
  uninstantiated generic type: that is what the types baseline prints and what the applicability check relates
  (through stage 1's `compare_signatures`), as in tsc.

## Stages (each ends with `./fasttest.sh --summary` clean or explained, probes checked, commit)
0. **Inference context reaches the argument's function body** (independent, small; do first as a warm-up).
   (a) `ty_expr_is_context_sensitive`: an arrow function with an expression body is context sensitive if the body is
   (tsc's `hasContextSensitiveReturnExpression`) -> `nested_lambda_context.ts`. (b) A non-context-sensitive function
   expression argument's return type is computed while `my outer_inference` still points at the call (tsc computes
   it eagerly in `contextuallyCheckFunctionExpressionOrObjectLiteralMethod` while `getInferenceContext` finds the
   call): force `signature_return_type` of such arguments inside `check_arg`'s outer-inference window, or keep the
   window open over `infer_from_arg`. Watch for the in-flight return memo and `flow_uncached`. ->
   `nestedReturnContext.ts`, corpus moduleNameResolver.ts 1080. If (b) turns out to need the contextual return type
   instantiated per consumer (tsc's `instantiateContextualType` with `returnMapper`/`nonFixingMapper`), write it up
   in triage.md rather than growing this stage.
1. **Relations**: `ty_signature_instantiation` cache, `instantiate_signature_in_context_of` without outer context,
   `compare_signatures` uses it (delete the positional unification and the `any` fallback + the hacks.md entry).
   Constraint checks inside must not recurse unboundedly: tsc passes the relation in progress as `compareTypes`;
   a plain `ty_is_related` from inside a relation may be fine given the maybe-stack, test it on the corpus. `./bench.sh`
   before/after: generic methods of different declarations (`Array.map` vs `ReadonlyArray.map` when structural) now
   infer instead of unifying. -> `genericSigRelation.ts`, parts of `contextualSignatureInstantiation*`.
2. **Generic arguments instantiated in context**: `ty_signature_type`, the generic-argument pass, non-higher-order
   branch only. -> `genericArgInContext.ts`, parenthesizerRules.ts 662, tsbuildPublic.ts 945,
   `genericCallWithGenericSignatureArguments*`.
3. **Higher-order propagation**: inferred type parameters, merge, `candidate_return_type` wrap. Renaming on clash
   (`T_1`) needs a synthetic type parameter whose constraint is the original's through a mapper (tsc's
   `tp.target`/`tp.mapper`; `ty_synthetic_type_param` exists, `TyParamState` has no mapper); do it if the named tests
   need it, else leave a hack entry. -> `memoizeOne.ts`, `pipe.ts`, corpus TS2558 38 -> 0 and nodeFactory 502-512,
   `genericFunctionInference1/2`, `inferFromGenericFunctionReturnTypes*`, `asyncYieldStarContextualType`.
4. **Optional**: skip generic calls returning functions in pass 1 (`isGenericFunctionReturningFunction`), if the
   named tests still fail on it. Otherwise triage.md.

## Risks
- Instantiated signatures share type parameters with their targets (`instantiate_outer`, hacks.md): a propagated
  type parameter keeps its own constraint, fine; a renamed one doesn't have that luxury (see stage 3).
- Relation perf and recursion (stage 1): inference per generic-source comparison where there was a mapper before.
  Measure; if needed memoise `(source sig, target sig) -> instantiated source` per checker (safe: no outer context).
- Order dependence: tttt's arguments are checked once per candidate with memos; tsc re-checks in the second round.
  A generic argument's memo is its uninstantiated type in both, so this should hold, but overloaded callees
  (`skip_sensitive` rounds in `resolve_with_signatures`) re-run `infer_type_arguments`: the generic-argument pass must
  be idempotent (no state left on the argument).
- The `memoizeOne.ts` types component also needs tsc's `T_1` display renaming of a type parameter that shadows one in
  scope (`create : <T_1 extends Node>...` inside the generic arrow); that is a display gap (triage.md, overload lists),
  not this plan: gate on the errors component there.

## Validation
All six probes match tsc (`FAST=1 ./filetest.sh`; `memoizeOne.ts` errors component, see Risks). `./realworld.py`:
TS2558 0, no diagnostic left at nodeFactory.ts 502-512, parenthesizerRules.ts 662, tsbuildPublic.ts 945,
moduleNameResolver.ts 1080 (91 -> ~45). Of the 16 named suite tests, most components pass; record which still fail
and why. `./fasttest.sh --summary` no unexplained losses; `./bench.sh` no regression, numbers into measurements.md.
Out of scope, seen on the way: nodeFactory.ts 5073-5104/7175 (`Mutable<T>` of a type parameter, update helpers),
utilities.ts 9417 (`x ||= createMultiMap()` contextual type).

Estimate: 2 sessions (stage 0 + 1 in the first, 2 + 3 in the second), stage 4 only if cheap.
