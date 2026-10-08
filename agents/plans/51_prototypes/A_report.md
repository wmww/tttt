# Plan 51 prototype A: tsc's ContextChecked rule, made general

Worktree /tmp/wt-51a (based on a7003eac). All numbers are against HEAD measured in the same environment:
suite 14829 (lost 0), corpora 0 except zod-root 53 and effect 3, sweep match 8594 / spurious 0 / missing 1,
$ExpectType 68628, flagflip missing 18 / spurious 10 / other code 4.

## Short version
The general rule ("the first non-skip check of a context-sensitive function expression or object literal method
decides; no contextual signature then means none for good", tsc's NodeCheckFlags.ContextChecked) **works in tttt**,
once six small, tsc-shaped fixes are in. With them every oracle matches HEAD (0 lost anywhere), and
`Checker.implementation_elaboration` goes away. The hacks.md claim that "tttt checks context-sensitive functions in
places and orders tsc doesn't" turned out to be mostly *not* about order: of the six causes, only one is a real
out-of-order check (jsx.c's TS2710 pre-check), the rest are places where tttt's contextual type, overload filter or
`this` lookup differ from tsc's, which the old "first context wins, a later one fills in" behaviour hid.
Cost: +0.02% to +0.12% instructions. `KEY_CONTEXT_NONE` and `KEY_CONTEXT_FIRST_ABSENT` do *not* go away: they belong
to a different tsc mechanism (the return type reading the contextual signature lazily), not to ContextChecked.

## Census (variant -> every oracle)

| variant | suite (lost comps / tests) | corpora (non-expected) | sweep match/spur/miss | $ExpectType | flagflip miss/spur/other |
|---|---|---|---|---|---|
| HEAD | 14829, 0 | all expected | 8594 / 0 / 1 | 68628 | 18 / 10 / 4 |
| V1: general block, implementation_elaboration deleted | 14824, lost 11 / 5 | webpack 6 (TS7006), drizzle 46 (TS7031/TS2344/TS2347) | 8584 / 10 / 1 | 68491 | 18 / 10 / 4 |
| V2: V1 + JSDoc-typed functions excluded + anyFunctionType has no inferable index + KEY_CONTEXT_NONE/FIRST_ABSENT also for sensitive functions | 14825, lost 10 / 4 | all expected | - | - | - |
| V3: V2 + JSX attributes pass `sig_contextual` + filtering `as` mapped types give property contexts | 14829, 0 | all expected | 8591 / 3 / 1 (jquery TS2683 x2 pkgs, tonic-ui TS7006) | 68500 (jquery `this`) | 18 / 10 / 4 |
| V4: V3 + blocked functions still take `this` from later signatures + JSX spreads pass `sig_contextual` + jsx.c TS2710 doesn't decide | 14829, 0 | all expected | **8594 / 0 / 1** | 68500 (`this` was the first, uninstantiated signature's: `TContext`) | 18 / 10 / 4 |
| V4 + "this: last signature wins" + D (below) + FIRST_ABSENT deleted (= A_v5 patch) | 14829, 0 | all expected | 8594 / 0 / 1 | **68628** | 40 / 10 / 4 (vscode/nosnc -TS7023 x22: FIRST_ABSENT) |
| **V6 = V5 with FIRST_ABSENT kept (A_v6 patch, the recommended one)** | **14829, 0** | **all expected** | **8594 / 0 / 1** | **68628** | **18 / 10 / 4** |

V6 = HEAD on every oracle (suite, 17 corpora, sweep, $ExpectType, flagflip).

Single-deletion variants on top of V4 (suite + corpora quick check, `rvar` build):

| deleted on top of V4 | result |
|---|---|
| KEY_CONTEXT_NONE never set | lost 2 suite (contextuallyTypedParametersWithInitializers1 TYP, declarationsWithRecursiveInternalTypesProduceUniqueTypeParams TYP), effect 5 (+2). **Needed.** |
| KEY_CONTEXT_FIRST_ABSENT never set | suite/corpora 0, but flagflip vscode/nosnc -TS7023 x22. **Needed.** |
| overload_failure_inference's `KEY_FUNCTION_SIG_CONTEXT` suppression (expr.c `none_call && my overload_failure_inference`) | lost thislessFunctionsNotContextSensitive1 TYP (the test hacks.md names). **Needed** (it's the late-return mechanism again). |
| D: overload_failure_inference in check_with's generic-mode skip (`resolved = ty_call_resolved_return()` always) | suite/corpora 0 on top of V4 **and on top of HEAD**; with V5's full run sweep/expect/flagflip 0 too. hacks.md's "regresses effect 15 and react 2" no longer holds: **deletable independently of plan 51** (the field then only serves the SIG_CONTEXT suppression). |

## Why the general rule lost, and the fixes (the "out-of-order check" buckets)

Found with a temporary backtrace at the point the block is set (`TTTT_DEBUG_BLOCK`, `ty_debug_block()`, in
A_v1/A_v3/A_v4 patches; stripped from A_v5/A_v6) plus a second print when a contextual signature arrives after the
block ("LATE").

1. **JSDoc `@type` functions** (webpack 6 x TS7006): `/** @type {T} */ (a) => ...` and `/** @type {T} */
   this.x = (a) => ...` in JS. First check via expr_typecheck/statement_collect_return_types without context. tsc
   doesn't contextually type these at all: getTypeOfParameter goes to getParameterTypeOfTypeTag. Fix: never block a
   function with `jsdoc_signature_type` (its types come from ty_function_apply_jsdoc_type). Not an ordering issue.
2. **Overload candidate that tsc's skip pass rejects** (drizzle 46, sweep: part): `gelTable(name, ({ text }) =>
   ...)` against overloads `columns: TColumnsMap` (Record<string, X>) then `columns: (b) => TColumnsMap`. tttt's
   skip pass (`skipped_function_unfit()`) related anyFunctionType to `Record<string, X>` as assignable, because
   `has_inferable_index()` (relate.c) treated the any-function type as an anonymous empty object; tsc's
   anyFunctionType has no symbol, so isObjectTypeWithInferableIndex is false and the candidate fails in the skip
   pass, never reaching a Normal check. tttt went on to the Normal inference (infer_type_arguments ->
   ty_check_expr) against the wrong overload and blocked there. Fix: one line in has_inferable_index(). (A real
   relation bug, silently wrong at HEAD too; the old code repaired it with the later overload's context.)
3. **JSX attributes/spreads drop `ExprCtx.sig_contextual`** (contextuallyTypedJsxAttribute,
   genericInferenceDefaultTypeParameterJsxReact; sweep tonic-ui__react TS7006): check_jsx_attributes() computed only
   `member_context` from `ctx.contextual`, not the signature view check_object() computes, so in the inference
   round an attribute function saw no signature. Fix: mirror check_object()'s `member_sig_context`/`sig_absent`, also
   for spread attributes.
4. **Generic mapped types with a filtering `as` clause give no property context** (contextualPropertyOfGeneric-
   FilteringMappedType, contextualTypeFunctionObjectPropertyIntersection): `ty_generic_mapped_prop_context()`
   (relate.c) and `is_generic_mapped_context()` (expr.c) bailed on any `name_type`; tsc's
   getIndexedMappedTypeSubstitutedTypeOfContextualType only bails on Remapping (`getMappedTypeNameTypeKind`) and on
   an excluded name of the name type. Fix: use the existing `is_remapping()` (exported as ty_mapped_is_remapping).
5. **`this` gated by the decision** (sweep jquery x2 TS2683 = 128 diags, $ExpectType jquery 128; suite LATE
   census 11 events, vue 21, effect 4, all `this`-only-sensitive object literal methods): tttt stores the contextual
   `this` (KEY_CONTEXT_THIS) inside assign_contextual_signature_as, so a block also lost `this`. tsc's
   getContextualThisParameterType asks getContextualSignature every time; `$.proxy(function (a..g, t) { this }, ctx,
   ...)` is first Normal-checked against a 7-parameter overload (arity rejects the contextual signature, ContextChecked
   is set in tsc too) and only the later overload gives `this`. Fix: when blocked, still set KEY_CONTEXT_THIS from
   the latest contextual signature (last wins: the first one was the uninstantiated `TContext`), including from the
   `none_call` branch of the EXPR_FUNCTION case. Again not an ordering issue: a tsc lazy read tttt made eager.
6. **jsx.c's TS2710 pre-check** (`check_children()` in jsx.c, called from check_jsx_element: "attribute
   specified twice"): `ty_expr_type()` of every spread attribute value *before* the element is resolved, so `{...{
   onToggle: (ctx) => ... }}` is checked without context first. **The only genuine out-of-order check found.** Fix:
   peek with `ty_check_expr_skipping_sensitive()` when the spread is context sensitive (tsc checks spreads only in
   createJsxAttributesTypeFromAttributesProperty, with their contextual type).
7. KEY_CONTEXT_NONE / FIRST_ABSENT were restricted to non-sensitive functions "because marking sensitive ones
   regressed": with the block general that restriction must go (contextuallyTypedParametersWithInitializers1:
   `g1((x = 1) => 0)` keeps `=> 0` only through ty_late_contextual_return).

Remaining first-checks-without-signature that are *not* followed by a later contextual signature (i.e. harmless,
consistent with tsc): suite 1444 blocks, mostly statement-level (`initialized_binding_typecheck` 325,
`expr_typecheck` ~430, `check_call` of IIFEs 132, return collection 84, members 22...). Ones a later context reaches
(LATE): suite 11 (IIFE `(f => f(12))(i => i)` via assign_iife_parameter_types/ty_quick_expr_type: 4; `this`-only
methods in a generic call's inference round: 7), vue 21, effect 4, all `this`-only methods (bucket 5), none observable
once `this` is fixed. Corpora blocks: tsc 2, webpack 42, excalidraw 82, effect 92, vue 258, drizzle 7, others ~0.

## Cost
`./instrcount.sh` (callgrind; HEAD binary vs V5-without-debug, `rvar` build):
tsc 4.771 -> 4.773 G (+0.04%), valibot 4.236 -> 4.237 (+0.02%), xstate 3.183 -> 3.185 (+0.06%), effect 10.048 ->
10.060 (+0.12%). The extra `contextual_signature()` call at each sensitive function's first check is the cost.

## Patches
- A_v1_general_block.patch: the bare general rule (+ BLOCK backtrace instrumentation, `TTTT_DEBUG_BLOCK=1`).
- A_v2_general_block_fixed.patch, A_v3_general_block_clean_suite.patch, A_v4_general_block.patch: the steps above
  (still instrumented; v4 also has the LATE print).
- A_v5_general_block_minus_first_absent.patch: clean (no instrumentation), V4 + this-last + D + FIRST_ABSENT deleted
  (flagflip +22, don't use).
- **A_v6_general_block.patch: the recommended state**: V5 with FIRST_ABSENT kept. Still contains two prototype
  leftovers to tidy: an empty line after `compute`'s forward declaration, and the stale comments above the BLOCKED
  check ("in the implementation's check of a failed overloaded call") and at the KEY_CONTEXT_NONE assignment ("Only
  one that isn't context sensitive").

## Recommendation for plan 51's execution stages (ContextChecked part)
Stage 1 (one session, small): apply A_v6 as is, in this order so each step is measurable:
  a. relate.c has_inferable_index(): anyFunctionType has none (bucket 2). Probably 0-change alone; it's a relation
     fix in its own right.
  b. JSX attributes and spreads pass sig_contextual/sig_absent (bucket 3); filtering mapped `as` clauses give
     property contexts (bucket 4); jsx.c TS2710 peek doesn't decide (bucket 6). Each is independently correct.
  c. The general block in the EXPR_FUNCTION case (sensitive, no jsdoc type, not DONE/BLOCKED, no contextual
     signature or sig_absent), `this` taken from the latest signature even when blocked (bucket 5), NONE /
     FIRST_ABSENT no longer restricted to non-sensitive functions; delete `Checker.implementation_elaboration` and its
     save/restore in add_implementation_success.
  d. Independently: delete overload_failure_inference's use in check_with's generic-mode skip (D).
  Expected losses: none (measured), but rerun all oracles; the environment flickered once ($ExpectType babel
  packages under -f filters; compare full runs only).
Stage 1 cleanup: KEY_CONTEXT_DONE and KEY_CONTEXT_BLOCKED are now exactly tsc's "ContextChecked with/without a
signature": merge them into one key (a sentinel for "none") and rename (e.g. KEY_CONTEXT_CHECKED). Rewrite the
hacks.md entries for KEY_CONTEXT_BLOCKED/implementation_elaboration (gone), and correct the KEY_CONTEXT_NONE entry
(the restriction to non-sensitive functions is gone).

What the ContextChecked port does **not** remove (these are tsc's *lazy contextual return type*, a different
mechanism: getReturnTypeFromBody / getContextualReturnType asking getContextualSignature through the call's resolved
signature when the return type is computed):
  KEY_CONTEXT_NONE, KEY_CONTEXT_FIRST_ABSENT, KEY_FUNCTION_SIG_CONTEXT, ty_late_contextual_return,
  ty_function_context_is_self, and overload_failure_inference's SIG_CONTEXT suppression. Each was deleted alone and
  each loses something (table above). They should be replaced together by the third mechanism in plan 51's brief
  (contextual signature read through the enclosing call's resolved/resolving signature, cached per call), i.e. a
  `ty_function_contextual_signature_now(function)` that walks to the argument's call and asks its resolved signature
  (or the resolving one), which set_contextual_return / the return-type computation would call instead of the
  remembered "context of the last check". That's the stage to schedule second, and it's the bigger one.

## Anything against the port
Nothing against the ContextChecked part: it's cheap, passes everything, deletes one side channel and makes another
(BLOCKED) tsc's flag. The fixes it forced were latent bugs at HEAD (2, 3, 4, 6) that the "later context fills in"
behaviour masked. The risk is in the lazy-return-type part, which this prototype didn't attempt; there the hack notes'
warning ("any other contextual signature would be inferred eagerly, circular for compositeContextualSignature")
still applies and needs its own prototype.

Note for whoever measures next: realworld-style vue at `--checkers 1` reports one TS2345 (vModel.ts:446) at HEAD
too; it's 0 only at the default 4 checkers (a checker-order dependence, unrelated to this work, worth a triage line).
