# Plan 38: tsc's inference flow in call resolution
Brief by the insights of 2026-09-28; turned into this plan by the planning session of the same day (todo entry 2 of
the sixth window), which prototyped the port in worktrees. That session died before writing up; the next session
recovered its worktrees, fixed the prototype's last losses and **landed stage 1** (todo entries 2 and 3 in one
session). What is left is stage 2 below. `38_prototypes/stage1.patch` is the landed prototype before cleanup,
`stage1_base.patch` the one before the loss analysis, `wt38b_on_base.diff` a sub-agent's unmerged experiment (stage 2).

## Why
call.c's inference was the one area where tttt stacked approximations instead of doing what tsc does: tttt decided up
front, by probing shapes, which type parameters a context-sensitive argument fixes (`fixed_by_argument` /
`fixed_by_expr` / `fixed_by_function`, only for type parameters that already had candidates); it instantiated a
context-sensitive argument's whole parameter type with the inferences so far (so a candidate-less `T = {}` emptied a
generic mapped type, and "a type parameter without candidates stays itself only where it would be `unknown`" patched
that up); it memoized the contextual return type from that eager instantiation. bugs/type_core.md "Inference" had five
bullets naming this root, it was webpack's largest remaining cause and most of xstate's.

## tsc's flow (what the port reproduces)
- **Contextual types are instantiated where they are looked at, top level only** (instantiateContextualType +
  instantiateInstantiableTypes, called by getApparentTypeOfContextualType for every node): type variables at the top
  of a node's contextual type, or in its unions/intersections, are replaced; object types, mapped types, function
  types stay generic, and their members are instantiated when *their* nodes ask. Two mappers: a **contextual
  signature** lookup (ContextFlags.Signature, which every level of the chain up to the argument passes on) uses the
  non-fixing mapper (all inferences so far) when some type parameter has candidates or a default; **every other
  lookup** (literal widening, tuple-ness, discriminating...) only the return mapper (what the call's own contextual type
  said). getInferenceContext(node) finds the innermost call inferring.
- **Fixing is lazy**: the fixing mapper fixes a type parameter the moment instantiation asks for it. That happens when
  a context-sensitive function gets its contextual signature (contextuallyCheckFunctionExpressionOrObjectLiteralMethod
  -> instantiateSignature(contextualSignature, context.mapper) -> assignContextualParameterTypes asks for the
  unannotated parameters' types and `this`), candidates or not (a candidate-less one is fixed to its default,
  `unknown` or the constraint: getInferredType). A rest parameter of type parameter type uses the non-fixing mapper.
  Generic function arguments fix what the contextual signature's parameters mention (instantiateSignatureInContextOf ->
  applyToParameterTypes).
- **A function argument's return type is decided at its first contextual check** (getReturnTypeFromBody inside the
  same contextuallyCheck..., with the inferences of that moment, the return expressions' contextual type instantiated
  at their top); later checks of the body see the resolved signature's instantiated types.
- **A call is resolved once** (links.resolvedSignature); errors are reported from that resolution.

## Stage 1 (landed)
What it does in tttt's terms:
- `OuterInference` (call.c) is tsc's inference context of the innermost call; `my outer_inference` is set around
  every argument check (both passes, intra-expression sites), `OuterInference.fixing` in the second pass.
- `check_with()` instantiates each node's contextual type on entry (`ty_instantiate_contextual_type()`: the return
  mapper, top level) and carries `ExprCtx.sig_contextual`, the signature view (`ty_instantiate_contextual_type_for(...,
  true)`: the non-fixing mapper), when it differs; object and array literals derive both views for their members.
  Functions take their contextual signature from the signature view.
- `ty_fix_contextual_signature()` (call.c), from `assign_contextual_signature()`: fixes the call's type parameters
  that the unannotated parameters' and `this`' types mention (`assignment_mentions()`: a `ty_mentions_any()` walk),
  candidates or not, and
  returns the signature instantiated with the inference mapper. The second pass passes the raw parameter type.
  Deleted: `fix_for_expr`, the second pass's `fixed_by_argument` loop and candidates gate, the "only where it would be
  unknown" stand-in, `Checker.contextual_fixing_mapper`.
- **Replay** (`KEY_ARG_FIXES`, (argument, signature) -> `ArgFixes`, which type parameters and to what): tttt resolves a call several times (the error pass,
  every outer inference round, overload rounds), but a function is assigned its contextual signature once, so a later
  resolution would fix nothing. The second pass records which type parameters checking each argument fixed and
  replays that before checking it again. This is the answer to the brief's memoization question: tttt's per-node
  memo (`LazyMemo` keyed by the contextual type) is compatible with tsc's model (functions are assigned once in both;
  literals are rechecked per contextual type, and not storing memos under inference changes nothing in the suite);
  what isn't is tttt's *repeated resolution*, and the replay record covers it.
- Return types: `compute()`'s EXPR_FUNCTION computes an argument function's return type right after assigning its
  contextual signature while a call infers (`ty_argument_return_type()`), and `KEY_CONTEXT_RETURN` is the raw return
  type of the contextual signature of the function's *last* check (`set_contextual_return()`), instantiated at the top
  where it is read (`infer_return_type()`, and `check_with()` for the return expressions).

Measured (suite vs 6475225b's 10554; corpora are tttt's diagnostics, 0 under tsc):
| step | suite | zod | xstate | webpack |
|---|---|---|---|---|
| lazy fixing + top-level instantiation (non-fixing mapper everywhere) | +4 / -13 | | | |
| + replay | +5 / -11 | | | |
| + return type at first contextual check, last-check return context | +8 / -10 | 16 | 47 | 13 |
| + return mapper only outside signatures | +8 / -6 | 2 | 69 | 14 |
| + signature view through literal members (`stage1_base.patch`) | +8 / -7 | 2 | 42 | 14 |
Every piece is needed: the brief's "the pieces only work together" holds, so stage 1 is the whole core.
Probes: `38_prototypes/xstate_default_in_contextual_type.ts`, `xstate_return_mapper_leafwise.ts`, `xstate_nested_generic_call.ts`,
`xstate_fixing_through_parameter_walk.ts` (Box<unknown>) and `test/probes/plan37/contextual_instantiation.ts`,
`return_only.ts` now match tsc. Prototype perf (instructions, K=1; landed numbers below): tsc +0.9%, webpack +0.8%, zod +1.3%, valibot +2.6%,
xstate +5.3%, type-fest 0 (diffuse: no new hotspot; the probes instantiate each contextual parameter type once per
type parameter).
Not changed: xstate's output still depends on the checker count (triage), 4 diagnostics in both HEAD and prototype.

Loss analysis (after the table's last row, -7): four fixed by sub-agent A (`stage1.patch`: `ty_instantiate_fixing()`
for a ThisType<T> marker, intersection members see the whole intersection as `this` in contextual property lookups,
`may_want_signature()` so the signature view is only built where a function may ask, nested array literal candidates
through array element types, inference to a simplified indexed access), then three by the landing session:
- neverReturningFunctions1: `type_of_dotted_name()` (flow.c) took the *contextual* `this` for a call statement's
  callee; tsc's getTypeOfDottedName takes only an explicit one (getExplicitThisType). Asking for it while the call
  inferred fixed T through the ThisType marker (a method's return type is computed at its first contextual check, so
  its body's never-returning-call check ran inside the inference).
- narrowingNoInfer1: `mentions_type_variables()` had no TY_SUBSTITUTION case, so `NoInfer<A>` "mentioned" every type
  parameter and the callback fixed B before its return type inferred into it.
- nonInferrableTypePropagation2: the replay record now holds what each type parameter was fixed *to* (`ArgFixes`): a
  nested call resolved again with another return mapper fixed A to `unknown` while its arrow's `n` was already
  `number` from the first resolution.

Landed result: suite 10554 -> 10565 (+14 tests gained, 0 lost: arrayLiteralInference, correlatedUnions,
discriminatedUnionInference, fixingTypeParametersRepeatedly2, generatorTypeCheck45/46, noInfer,
mappedTypeContextualTypesApplied, reverseMappedPartiallyInferableTypes...). Corpora (default 4 checkers): xstate 61 ->
43, webpack 17 -> 13, the others still 0, zod-root still tsc's 55. Instructions (1 checker): tsc +0.3%, zod +0.2%,
xstate +2.4%. The plan 19 probe `webpack/contextual_return_default_leak.ts` infers tsc's `B` now (it still fails on
an optional parameter's `| undefined` display).

## Stage 2 (execution, next)
Start from `./realworld.py --corpus xstate` and the probes; each item is independent.
1. **Generic mapped contextual types stay generic** (`38_prototypes/xstate_contextual_type_keeps_generic_mapped.ts`,
   still fails; xstate's `assign({ prop: ({ spawn }) => ... })` TS7031s, 7 in the runtime tests): the member of a
   generic mapped contextual type should be read through the mapped type with only its key instantiated, not after
   the whole type was instantiated with `C = {}`.
2. **Mapped template instantiated in one step** (`wt38b_on_base.diff`, `ty_mapped_template_instantiated()`: an
   instantiated mapped type's template is its declaration's, instantiated with every mapper composed, like tsc's
   addMemberForKeyTypeWorker; xstate 42 -> 40 on the base prototype, suite neutral). Its `object_literal_unchanged()`
   in instantiate.c is a second, separate idea (an object literal type a mapper doesn't touch stays itself); measure
   both on top of stage 1 separately.
3. **xstate diagnostics stage 1 introduced** (at `--all-errors`, all 0 under tsc): interpreter.test.ts:1310 TS2728 +
   1319 TS2322, types.test.ts:3034/3055/3847 TS2322. Reduce each to a probe in `38_prototypes/`. Done: input.test.ts:255
   and invoke.test.ts:1427 TS7031 (`xstate_intra_expression_raw_context.ts`: intra-expression sites are now tsc's,
   recorded by the literal check and inferred from before the next fixing, instead of a pre-walk that checked each
   arrow against the raw member type). Not the same cause: spawnChild.test.ts:107 TS7031 (`spawnChild(child, { id:
   ({ context }) => ... })` nested in `entry: [...]`).
4. **JSDoc `@template` fixed by a nested JSDoc function type** (below): webpack MultiCompiler.js 645/649 and
   FileSystemInfo.js 3859, still there.
5. Replace the stand-ins stage 1 left (hacks.md "Inference" entries it names): the replay record, the probes that
   still decide fixing for generic function arguments (`fixed_by_argument`/`fixed_by_type`), and
   `LazyMemo.speculative`, if tsc's resolve-once model can be had with a per-call-resolution cache.

## JS: a JSDoc `@template` is fixed by any nested JSDoc function type
`38_prototypes/jsdoc_template_fixed_by_nested_function_type.ts` (todo entry 1 of the sixth window): in JS, tsc's
isTypeParameterPossiblyReferenced walks up from a JSDoc type node and never meets the `@template` tag, so R counts as
referenced by `s: () => void` in `@param {(i: number, s: () => void) => R} setup`. Checking the context-sensitive
setup arrow instantiates that parameter type with the fixing mapper, which fixes R before it has candidates: the JS
default `any`. webpack depends on it: MultiCompiler.js `_runGraph` (SetupResult is any: TS18048, TS2345) and
FileSystemInfo.js `_readContext` (T is any: TS2345 at 3859). With lazy fixing landed, this needs the quirk in
`assignment_mentions()` (a JSDoc function type in JS mentions every `@template` of its host).

## Validation
Stage 2: xstate and webpack lower with no new diagnostic (compare `file:line code` sets against the previous commit;
strip colours and sort with LC_ALL=C), the probes above pass, 0 suite tests lost, the other corpora still 0.
