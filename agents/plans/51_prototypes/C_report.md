# Plan 51, prototype C: contextual instantiation (tsc's instantiateContextualType vs tttt's push-down)

Worktrees: /tmp/wt-51c (census build, every variant an env toggle `V_xx=1`, plus the audit instrumentation),
/tmp/wt-51d (clean stage-1 deletion patch applied). HEAD a7003eac.

## TL;DR
- **The push-down is not the approximation the hacks describe.** tsc instantiates at *every level* too:
  getContextualTypeForObjectLiteralElement / ...ElementExpression call getApparentTypeOfContextualType(parent, flags),
  which runs instantiateContextualType on the parent before the property lookup, and the flags (ContextFlags.Signature
  or none) propagate up the chain. So tsc has two lazily recomputed chains; tttt's ExprCtx carries both eagerly:
  `contextual` = the None chain, `sig_contextual` = the Signature chain (only where `may_want_signature()`, null = same),
  `sig_absent` = "the Signature chain has no type here", `raw_contextual` = getContextualType() at this level (before
  this level's instantiation, what a nested call / conditional branch / tagged template reads). All four are tsc
  concepts; deleting any of them loses (table below). They are not side channels and should be documented as such.
- **The dynamic `my outer_inference` equals tsc's node-captured inference context inside every push-down chain**: an
  ExprCtx field capturing it at chain entry and compared at every nested check_with mismatched 0 times on the whole
  suite and 11 corpora (vscode included). The "leaf-wise model with a captured OuterInference" is therefore a
  refactor with no behavior change for ExprCtx readers. Where dynamic vs ancestry *does* differ is at chain entries:
  an ancestry audit (innermost active inference whose call span contains the node, tsc's getInferenceContext) found
  121 non-JSX entries in the suite where the dynamic one is set but isn't an ancestor (flow narrowing evaluating
  earlier type-guard calls `if (is(x))` while a later call infers: typeGuardsAsAssertions, narrowingMutualSubtypes,
  coAndContraVariantInferences3/4...), 31 "cleared but an ancestor is inferring" (thislessFunctionsNotContextSensitive2,
  cachedContextualTypes, switchStatements; partly tsc's own explicit `undefined` push in checkApplicableSignature), 8
  "a different ancestor" (circularReferenceInReturnType*). None of these change a baseline today (no lost/gained).
- **Cost is not a driver.** ty_instantiate_contextual_type_for is 1.39% inclusive on xstate, below callgrind's listing
  threshold on effect; of the check_with entries under inference (effect 270k, xstate 38k, valibot 41k) only 1-10%
  produce a changed type. Leaf-wise reading would at best save part of ~1%, and tsc-style re-walking per read could
  cost more.
- **What does go away cleanly (stage 1, 0 lost on every oracle, instructions +-0.05%)**:
  `ty_function_note_widening_context` + `KEY_CONTEXT_WIDENING` (dead: 0 effect anywhere), the first-pass
  return-mapper branch of `infer_type_arguments` (the `instantiate_instantiable(param_type, return_mapper)` +
  `arg->expr->tag == EXPR_CALL ? param_type : context` + its own regularization: check_with already instantiates with
  the call's return mapper; the one thing it did that check_arg didn't is tsc's checkExpressionWithContextualType
  regularizing against instantiateContextualType(contextualType, node), now done in check_arg), and check_rest_arg's
  return-mapper pre-instantiation. Patch: /tmp/plan51/C_stage1_deletions.patch (-46/+16 lines).
- **Stale notes found**: type_core.md "Contextual types are instantiated eagerly" (both probes
  test/probes/plan37/contextual_instantiation.ts and return_only.ts pass now; delete the entry); hacks.md "Return
  mapper" bullet: "Non-sensitive function arguments get their parameter type fully instantiated" (it was top level,
  and stage 1 deletes it), "for all: tsc-exact on tests, but valibot +9%" (not tsc-exact any more: tsc's always-separate
  returnContext loses 5 tests, see C1).

## Census (each variant alone, env toggle in /tmp/wt-51c)
HEAD: suite 14829, lost 0; corpora all 0 but zod-root 53, effect 3; sweep match 8594 missing 1; expecttypes 68628;
flagflip missing 18 / spurious 10 / other 4. (flagflip `-F nosnc|sloppy|nonia` subset: missing 2, spurious 2.)

| variant | what is deleted | suite lost | corpora (non-baseline) | sweep | expect | flagflip |
|---|---|---|---|---|---|---|
| A1 | `ty_function_note_widening_context` / KEY_CONTEXT_WIDENING (no-op) | 0 | 0 | = | = | = (full run with C4) |
| A2 | KEY_CONTEXT_WIDENING_ABSENT (the sig_absent TS7011 rule) | 0 | 0 | see below | | nosnc subset: missing 2 -> 4 (-2 TS7011 vscode) |
| A3 | KEY_CONTEXT_UNFIXED (read the fixed signature instead) | 0 | 0 | | | nosnc subset: missing 2 -> 340 (-260 TS7018, -76 TS7011, 6 cells) |
| A1+A2+A3 | all three | 0 | 0 | | | full: missing 18 -> 358 |
| B1 | `sig_contextual`/`sig_absent` (one chain only) | 13 comps / 7 tests | webpack 2, xstate 22, vue 1, effect 39, vscode 88 | (run3) | | |
| B2 | `sig_absent` only | 0 | vue 1 | 8594/1 = | 68628 = | missing 18 -> 20 |
| B3 | `raw_contextual` (nested calls/branches read the instantiated type) | 0 | xstate 7, effect 21, vscode 5 | 8594/1 = | 68628 = | spurious 10 -> 39 |
| B4 | the push-down instantiation altogether (readers that re-instantiate still do) | 24 comps / 15 tests | 12 corpora, vscode 886, effect 129 | | | |
| C1 | return mapper always from the separate returnContext (tsc's code, the "gaps" HACK deleted) | 7 comps / 5 tests, gained 2 | 0 | (run3) | | |
| C2 | first-pass `return_mapper` branch (raw param type, check_with instantiates) | 0 | effect 4 (+1), vscode 8 | | | |
| C2 + check_arg regularizes with the return-mapped context | | 0 | 0 | = | = | = (stage 1) |
| C3 | `literal_arg_context()` return-mapper pre-instantiation | 1 (nestedGenericSpreadInference) | 0 | (run3) | | |
| C4 | `check_rest_arg` return-mapper pre-instantiation | 0 | 0 | 8594/1 = | 68628 = | 18/10/4 = |
| **stage 1** = A1 + C2(+fix) + C4 | clean patch | 0 | 0 | 8594/1 = | 68628 = | 18/10/4 = |

(Rows marked "(run3)" or empty weren't run on sweep/expecttypes/flagflip: the timebox ran out, and they already fail
the suite/corpora filter. A2/A3 flagflip numbers are the `-F 'nosnc|sloppy|nonia'` subset, where HEAD has missing 2,
spurious 2. The A variants only touch strictNullChecks-off paths, so sweep/expect can't move.)

### Loss buckets
- **B1 (one chain)**: the Signature-flag view is load-bearing exactly where tsc's tests for it are:
  contextualTypeCaching (`emit('a', { callback: (r) => {}, nested: {...} })`: TS7006 `compiler`), instantiateContextualTypes,
  inferenceContextualReturnTypeUnion1/3, correlatedUnions, mappedTypeRecursiveInference2,
  subtypeReductionWithAnyFunctionType; corpora: contextual signatures losing earlier arguments' inferences
  (effect 39, vscode 88). tsc-faithful as is.
- **B2 (sig_absent)**: vue 1, flagflip -2 TS7011: `sig_absent` is "getApparentTypeOfContextualType(node, Signature)
  is undefined here while the None chain has a type" (`<TP extends Partial<T> = {}>`: `{}` under the non-fixing mapper,
  the constraint otherwise). Faithful; could be a sentinel value of `sig_contextual` instead of a bool.
- **B3 (raw_contextual)**: effect `Cause.test.ts:546` `Cause<Option<string> | Option<E>>` vs `Cause<Option<E>>`: a
  nested generic call infers from the outer call's partially instantiated contextual type. tsc's resolveCall takes
  getContextualType() (not instantiated at the call's own level). Faithful.
- **B4**: everything above at once plus every reader that doesn't re-instantiate (object/array member lookups, memo
  keys). Confirms the push-down is the evaluation strategy, not an add-on.
- **A3 (UNFIXED)**: TS7018/TS7011 under strictNullChecks off. tsc's shouldReportErrorsFromWideningWithContextualSignature
  reads getContextualSignatureForFunctionLikeDeclaration: the *uninstantiated-at-object-level* contextual signature
  (`() => T` stays `() => T` under instantiateInstantiableTypes), whose return is generic, so no report. tttt's
  KEY_CONTEXT_DONE holds the signature after ty_fix_contextual_signature (tsc's `instantiatedContextualSignature`,
  used only for parameter types). So KEY_CONTEXT_UNFIXED is the cache of tsc's getContextualSignature, a real concept:
  keep it, but store it always (not only "when it differs") under an honest name.
- **A2 (WIDENING_ABSENT)**: -2 TS7011 (vscode `mockObject`, commit 47065f06). Emulates tsc computing the return type
  while the call is being resolved, when getContextualTypeForArgumentAtIndex reads `resolvingSignature` (no
  parameters -> no contextual signature -> report). That's plan 51's "resolved signature" mechanism, not contextual
  instantiation.
- **C1 (tsc's returnMapper always from the separate return inference)**: contextualParamTypeVsNestedReturnTypeInference1-4
  (`effectGen(function () { return { fn: effectFn(function (a) {...}) } })` types `a: any` instead of `string`),
  nestedGenericSpreadInference; gains inferFromGenericFunctionReturnTypes3 (2 components). So tttt's "only the gaps" HACK
  compensates for some other difference in what the nested call's contextual type is (its uninferred_contextual), not
  for valibot perf only. Open question for whoever ports returnMapper exactly.
- **C2 alone**: `Promise.resolve('notReady')` returning into `Promise<WorkspaceSelectionResult>` (vscode x8,
  chatView.ts:130) and effect `Effect.fail("a")` (dtslint/Effect.tst.ts:39): the literal argument wasn't regularized
  against the return-mapped contextual type. With check_arg asking `return_mapped_context()` (tsc's
  checkExpressionWithContextualType -> instantiateContextualType(contextualType, node, undefined)) it's clean.
- **C3**: nestedGenericSpreadInference (`call(wrap(<T>(x: T) => x), 1)` prints `{ x: <T>(x: T) => T }` instead of tsc's
  `{ x: (x: unknown) => unknown }`): generic_function_argument() needs the return-mapped context for
  instantiateSignatureInContextOf. Leave.

## Prototype 2 (leaf-wise model) and how far it got
1. Captured inference context in ExprCtx (`inference`, `inference_set`, propagated through object members, array
   elements, spreads; compared with `my outer_inference` at each nested check_with): **0 mismatches** on suite and all
   corpora. Conclusion: replacing the dynamic read with a captured pointer removes no field and changes nothing; it
   would only matter for readers outside ExprCtx (return-type inference in function.c, ty_contextual_this_type, lazily
   evaluated parameter types), and those are entry points where tsc's answer is getInferenceContext(node) = ancestry.
2. Ancestry audit at chain entries (`ty_proto_inference_audit`, call span containment): numbers in the TL;DR. JSX
   entries dominate the raw count (651 of 811) only because `call_span` of a JSX element isn't the whole element (an
   audit artifact). The real divergences are the flow engine evaluating unrelated calls while an inference is active
   (dynamic leaks in), and a few explicitly cleared spots.
3. P1, "widening decided where the return type is computed" (tsc's placement: reportErrorsFromWidening inside
   getReturnTypeFromBody; KEY_CONTEXT_SIGNATURE written by set_contextual_return, a reportability bit stored when
   infer_return_type runs, under !strictNullChecks && noImplicitAny only, skipping self signatures): deletes all three
   widening side channels (WIDENING, WIDENING_ABSENT, UNFIXED) with suite/corpora 0 and the nosnc flagflip subset's
   missing back to 2, but **+48 spurious TS7011** (valibot `optional(string(), () => undefined)`, const TDefault
   constrained by `Default<TWrapped, undefined>`, overloads) and **+2 TS7023** (excalidraw textWysiwyg.test.tsx `edge`).
   First-wins vs last-wins signature made no difference. Diagnosis: tttt computes such return types at other moments
   than tsc (tsc: at the first contextual check, contextuallyCheckFunctionExpressionOrObjectLiteralMethod, or lazily
   under resolvingSignature), so placing the decision at computation time inherits tttt's timing. P1 becomes viable
   only after the ContextChecked / resolved-signature port (plan 51 items 2-3). Patch (all toggles + audit + P1, in
   one): /tmp/plan51/C_proto_full.patch (V_P1=1).

## Cost
Callgrind (perf unavailable), single checker:

| corpus | HEAD | stage 1 |
|---|---|---|
| tsc | 4.771 G | 4.772 G |
| valibot | 4.236 G | 4.238 G |
| xstate | 3.183 G | 3.184 G |
| effect | 10.048 G | 10.045 G |
| drizzle | 8.064 G | 8.064 G |

ty_instantiate_contextual_type_for inclusive: xstate 1.39%, effect < listing threshold. Push-down stats
(`V_STATS=1`, wt-51c): check_with calls / under inference / instantiation changed the type / sig view asked / sig
view differs: tsc 254k/21k/2.5k/3.9k/0, valibot 139k/41k/3.3k/7.4k/144, xstate 102k/38k/1.0k/9.2k/2.5k,
effect 795k/270k/10k/67k/1.5k, drizzle 95k/18k/0.8k/1.5k/24. A leaf-wise rewrite has no perf case.

## Recommendation for plan 51's execution
Do not port "leaf-wise instantiation": tttt's push-down already is tsc's instantiateContextualType evaluated eagerly
along both ContextFlags chains, at 0 behavior difference from a captured inference context, at ~1% cost. Instead:

Stage 1 (part of execution session 1, ~1 hour): apply /tmp/plan51/C_stage1_deletions.patch (delete
ty_function_note_widening_context/KEY_CONTEXT_WIDENING; delete the first-pass return-mapper branch of
infer_type_arguments, check_arg regularizes against `return_mapped_context()`; delete check_rest_arg's pre-instantiation).
Expected 0 lost on every oracle (measured). Then documentation fixes: rewrite ExprCtx's comments to say which tsc
chain each field is (contextual = getApparentTypeOfContextualType(node, None) before apparent, sig_contextual = with
ContextFlags.Signature, sig_absent = Signature chain undefined, raw_contextual = getContextualType(node)); remove the
"push-down approximation" language from hacks.md "Calls and inference" bullet 1, type_core.md's eager-instantiation
entry (probes pass), and the "Return mapper" bullet's stale sentences; rename KEY_CONTEXT_UNFIXED to what it is (the
contextual signature as tsc's getContextualSignature returns it, before the fixing instantiation) and set it always
(trivial; measure).

Stage 2 (with the ContextChecked / resolved-signature port, the other prototypes' territory): widening reportability
moves into infer_return_type (P1) once return types are computed when tsc computes them; that deletes
KEY_CONTEXT_WIDENING_ABSENT and the fallback chain of ty_function_widening_reportable. Expected losses if done
before: +48 TS7011 / +2 TS7023 spurious (nosnc flagflip), handle by doing it after, not by patching.

Stage 3 (optional, small): get the inference context by ancestry at the chain *entries* (tsc's getInferenceContext:
a stack of (node, OuterInference) pushed per argument check, looked up by span containment; JSX needs the element's
full span). Today's divergences don't change any baseline, so this is hygiene: it would replace the save/clear/
restore dance around `my outer_inference` (about 15 sites in call.c, function.c's keep_outer) and stop the flow
engine's leak. Measure first with the audit in C_proto_full.patch (`V_LOGAUD=1`).

Leave as permanent (justified, document): sig_contextual/sig_absent/raw_contextual (tsc concepts), C3's
literal_arg_context (higher-order needs it), the C1 "gaps" HACK until someone explains contextualParamTypeVsNested-
ReturnTypeInference1-4 under tsc's exact returnMapper (a lead for plan 37 / a bug file, not this plan).

## Against the port
The plan's premise ("instantiates contextual types by pushing the inference mapper down ... tsc: leaf-wise") is
wrong in the part this prototype covers: the leaf-wise reading is tsc's laziness, not a different semantics, and the
only semantic differences (timing of return-type computation, resolved vs resolving signature) belong to the
ContextChecked / getResolvedSignature mechanisms. Of the contextual-instantiation side channels, one is dead
(KEY_CONTEXT_WIDENING), one is a misnamed tsc concept (UNFIXED), one depends on the resolved-signature port
(WIDENING_ABSENT), and the return-mapper pre-instantiations were redundant with check_with (deleted in stage 1).
