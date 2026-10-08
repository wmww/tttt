# Plan 51: tsc's contextual-check state instead of side channels
Brief by the insights of 2026-10-07; planned (and stage 1 landed) by the planning session of 2026-10-08 (todo entry
1), from three prototypes in worktrees. Their reports, census tables and patches (against a7003eac, before stage 1)
are in `51_prototypes/`: A = ContextChecked, B = the contextual type read through the resolved signature (context
anchors), C = contextual instantiation.

## What the prototypes found
The brief named three tsc mechanisms. They turned out to be very different in size:
- **Leaf-wise contextual instantiation: nothing to port.** tsc instantiates at every level too
  (getContextualTypeForObjectLiteralElement calls getApparentTypeOfContextualType on the parent first), along two
  chains (no flags / ContextFlags.Signature). ExprCtx's `contextual`, `sig_contextual`, `sig_absent` and
  `raw_contextual` are those chains evaluated eagerly: deleting any one loses (7 tests and vscode 88; vue 1; xstate 7
  and effect 21; 15 tests and vscode 886 without the push-down). A captured inference context compared with the
  dynamic `my outer_inference` at every nested check_with differed 0 times on the suite and 11 corpora. Cost isn't a
  reason either (`ty_instantiate_contextual_type_for` is 1.4% of xstate). The "push-down approximation" wording in
  hacks.md was wrong and is gone.
- **ContextChecked: small, done (stage 1).** The general rule (the first check that doesn't skip a context-sensitive
  function decides; no contextual signature then means none for good) works once six things are fixed, and only one
  of them was an out-of-order check (jsx.c's TS2710 pre-check reading spreads before the element resolves). The other
  five were latent bugs that "a later context fills in" hid: JSDoc `@type` functions, anyFunctionType having an
  inferable index (it fit `Record<string, X>` in the skip pass, drizzle), JSX attributes not passing the signature
  chain, filtering `as` mapped types giving no property context, `this` being decided with the parameters.
- **The read through the resolved signature: the real work (stages 2-3).** What's left of the side channels models
  tsc's *lazy* reads: getReturnTypeFromBody / getContextualThisParameterType asking getContextualSignature when the
  return type (or `this`) is computed, through the call's resolvedSignature (resolvingSignature while it resolves).
  tttt remembers "the context of the last check" instead. Prototype B's context anchors replace that for every read
  after the call has resolved; reads *during* inference are tsc's pushed contextual-type stack, which ExprCtx already
  is.

## Stage 1 (landed 2026-10-08, 0 lost on every oracle)
Prototype A_v6 + C's stage-1 deletions + B's dead `KEY_FUNCTION_CONTEXT`: the general ContextChecked block (deletes
`Checker.implementation_elaboration`), its six fixes, `KEY_CONTEXT_NONE`/`FIRST_ABSENT` no longer restricted to
non-sensitive functions, the generic-mode skip keeps an already resolved nested call everywhere (not only in an
overload failure's inference: hacks.md's "regresses effect and react" no longer held), `ty_function_note_widening_context`
/ `KEY_CONTEXT_WIDENING` deleted (dead), `infer_type_arguments`' first-pass return-mapper branch and
`check_rest_arg`'s pre-instantiation deleted (check_with already instantiates; `check_arg` regularizes a literal
against the return-mapped context, tsc's checkExpressionWithContextualType). Suite 14829, corpora unchanged, sweep
8594, `$ExpectType` 68628, flagflip 18/10/4; instructions within +0.12% (A) and +-0.05% (C).

## Stage 2 (landed 2026-10-08, todo entry 4; 0 lost on every oracle)
Prototype B's context anchors (`B_anchor.patch`), as designed above in the prototype report, cleaned up:
`ContextAnchor` {ARG | PROP | ELEM | RETURN} recorded by `ty_record_call_anchors()`; `KEY_CONTEXT_RETURN` holds the
first contextual Signature; `ty_contextual_return_type_self()` reads the return through the anchor once the call is
resolved. Deleted: `ty_late_contextual_return`, `KEY_FUNCTION_SIG_CONTEXT`, `Checker.overload_failure_inference` /
`overload_failure_candidate()`, the per-check rewrite of `KEY_CONTEXT_RETURN`, set_contextual_return's self special
case. The thread-local failure sink became `CallInfo.outcome_out` (`CallOutcome`: failure candidate, signature count,
the chosen candidate's checked parameter types). What differed from the prototype:
- **The ember regression was already gone**: stage 1's fixes (`this` decided apart from the parameters) removed it;
  nothing to bisect. `$ExpectType` +3 (ember__service `""`, the type_core.md item).
- **Own signature, not in flight: its return type is the context** (prototype: none). tsc's getContextualReturnType
  only refuses it while it resolves; the types baseline's final check sees `boolean[]`, so `[true]` is `true[]`
  (neverReturningFunctions1).
- **Composite with its own in flight** (`composite_in_flight()`, `ContextSelf`): no contextual signature (the guard
  stays before `contextual_signature()`, which would otherwise resolve the in-flight return: spurious TS7024 on vue),
  but it counts as self for TS7024 (tsc's widening check asks the composite's return type unguarded: vue
  hydration.spec.ts:1691). Only a direct own signature keeps a unit return.
- **`KEY_CONTEXT_FIRST_ABSENT` also for anchored functions first checked after the resolution** (not only under
  `my resolving_call`): with lazy reads the inner function of `getColorTheme: () => ({ getColor: () => undefined })`
  is first checked while the outer's return is in flight (vscode sessionView.test.ts:106 TS7023, nosnc).
- **`CallResolution.exact`** (non-generic, one signature): not anchored, not even recorded (recording moved after
  resolution, the only time anchors are read). Recovered normalizedIntersectionTooComplex's TS2590.
- **RETURN anchors mirror `function_impl_enter_return_context()`**: async return expressions get
  `ty_async_return_expressions_context()` (now shared), generators' returns aren't anchored.
- **Cost**: derivations cached per anchor (`AnchorRead.transient` when they went through an own return type not yet
  known, or a composite in flight), per literal for the discriminated context, ARG reuses the final check's parameter
  types; `discriminate_by_object_members` atomizes the literal's names once (was props x members `checker_atom`
  calls: also cheaper for check_object). Instructions vs HEAD: see measurements.md (all within ~0.5% but xstate
  +0.7%: anchor bookkeeping plus one extra intern table doubling).
Oracles: suite 14832 (0 lost), corpora unchanged, sweep 8594, `$ExpectType` 68628 -> 68631, flagflip 18 / 10 / 4.

## Stage 3 (landed 2026-10-08, todo entry 9; 0 lost on every oracle)
1. **Widening decided where the return type is computed**: `ty_function_decide_widening_reportable()` at the end of
   `infer_return_type()` (tsc's getReturnTypeFromBody), with the contextual signature of that moment (through the
   anchor once the call is resolved, else `KEY_CONTEXT_RETURN`, the first one). `KEY_CONTEXT_WIDENING_ABSENT` and
   `KEY_CONTEXT_UNFIXED` deleted. The prototype's +48 TS7011 / +2 TS7023 were gone after stage 2: neutral.
2. **`KEY_CONTEXT_FIRST_ABSENT` and `KEY_CONTEXT_NONE` deleted.** TS7023/TS7024's own-context circularity is just
   `KEY_CONTEXT_RETURN_SELF` (the gate on "the first check had no signature" changed nothing once returns are read
   through anchors). NONE became the general rule below.
4. **One `KEY_CONTEXT_CHECKED`** (tsc's ContextChecked) for every function expression, not only context-sensitive
   ones: the first check that doesn't skip it decides its contextual signature, `context_checked_none` without one;
   it replaces DONE + BLOCKED + NONE. Generalizing cost vscode 1 + 8 nosnc TS7018/TS7010 until a latent bug was fixed:
   `infer_rest_variadic()`'s first pass checked a literal with context-sensitive functions in it without the call's
   inference, so `ConstructorParameters<C>[0]` didn't see C's candidate (`test/probes/rest_skip_pass_outer_inference.ts`).
3. **`KEY_LITERAL_CONTEXT` as a stack: written off.** Measured (`this` reads by `literal_context_now()`, one checker):
   while the literal is being checked the frame always equals the persisted value (0 differ on 8 corpora), and the
   persisted key is read after the check returns, both before the call resolves (valibot 266, zod 296, vue 148,
   effect 130: a method's return type inferred during the call's inference, zod's `defineLazy(inst, { check() {
   this.def } })`; tsc reads resolvingSignature there) and after everything without an anchor (valibot 1086, vue 514,
   effect 500, vscode 322: `<any>{...}`, declarations, exact non-generic calls). Replacing the key needs anchors for
   every context source, i.e. a general lazy getContextualType (tsc's parent walk), not a stack. Not planned.

Optional, not done: the inference context by ancestry at chain *entries* (tsc's getInferenceContext: a stack of
(node, OuterInference) pushed per argument check, looked up by span containment), replacing ~15 save/clear/restore
sites of `my outer_inference` in call.c and stopping the flow engine's leak (C's audit, `V_LOGAUD=1`: 121 entries
where flow narrowing evaluates an unrelated type-guard call while another call infers). No baseline changes expected,
but item 4's latent bug (a first pass that forgot to set `my outer_inference`) is exactly what it would prevent.

## Permanent (justified, documented in hacks.md / code)
ExprCtx's four chain fields; `KEY_CONTEXT_IIFE_RETURN` (getContextualType(iife), fixed at the first check);
`KEY_LITERAL_PARENT` (a parent side table for ThisType markers of outer literals); the expression memo's "last
contextual check wins" (tsc's final Normal-mode check writes the node type: first-wins loses 19 tests); the call.c
return-mapper "gaps" HACK and `literal_arg_context()` until someone explains contextualParamTypeVsNestedReturnType-
Inference1-4 under tsc's exact returnMapper (a plan 37 lead).

## Not in this plan
Lazy *return types* (tsc computes an argument function's return type late; tttt during inference):
genericCallWithGenericSignatureArguments3, DT map-to-obj, bugs/type_core.md's laziness entries. Riskier (union and
diagnostic order depend on when return types are inferred); revisit after stage 3 with numbers.

## Validation
Each stage: `./oracles.py` 0 lost on every oracle against its start, `./instrcount.sh` within ~0.5% (or explained),
hacks.md's "Calls and inference" / "Expressions and contextual typing" entries for every deleted channel removed and
the survivors rewritten as justified. Plan done when stages 2-3 are landed or written off here with numbers.
