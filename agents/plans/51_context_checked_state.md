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

## Stage 2 (todo entry 4): context anchors and the lazy contextual return
Start from `51_prototypes/B_anchor.patch` (rebase onto stage 1: the EXPR_FUNCTION case in expr.c conflicts; the
`KEY_FUNCTION_CONTEXT` and `overload_failure_inference` generic-mode parts are already done). Design:
- `ContextAnchor` {ARG(call, index) | PROP(literal, name/computed key, parent) | ELEM(literal, index, parent) |
  RETURN(function)}, arena-allocated; `ty_record_call_anchors()` walks a call's arguments syntactically once when
  its resolution starts (call.c `call_return_type`): object literal members, array elements, conditional branches,
  return expressions of function arguments; records `KEY_FUNCTION_ANCHOR` / `KEY_LITERAL_ANCHOR`, first wins. No AST
  field (tsc's `node.parent` would be a second, pull-shaped copy of every check_* context rule: B chose anchors
  for that).
- `anchor_context()` derives the contextual type now: ARG = the resolved signature's parameter type at the index with
  the resolved mapper (CallResolution keeps the mapper and, for a failed call, the overload-failure candidate);
  PROP/ELEM reuse check_object/check_array's helpers; RETURN = the enclosing function's anchored contextual signature's
  return (not while that return type is in flight). Unresolved call: "not anchored", the reader keeps the push-down
  value (tsc's pushed type during inference).
- `KEY_CONTEXT_RETURN` holds the contextual Signature, first wins; `ty_contextual_return_type_self()` reads its return
  type when asked and says whether it is the function's own. A union contextual type with 2+ callable members, one of
  them in flight, gives no contextual signature (compositeContextualSignature).
- Deletes: `ty_late_contextual_return`, `KEY_FUNCTION_SIG_CONTEXT`, `KEY_CONTEXT_RETURN_SELF` as a read
  (function.c writes it for TS7024 only), the `none_call && my overload_failure_inference` branch and with it
  `Checker.overload_failure_inference`, set_contextual_return's in-flight case, the per-check rewrite of
  `KEY_CONTEXT_RETURN` (first-wins loses 7 tests without anchors, 0 with).

Prototype numbers (on a7003eac): suite 14827 (lost neverReturningFunctions1 TYP, normalizedIntersectionTooComplex ERR
TS2590), corpora clean, sweep 8590 (ember, ember/v3, ember__object, ember__object/v3: spurious TS2578), `$ExpectType`
68595 (-36 ember `this.foo` in `extend()` argument methods is `any`; +3 ember__service `""` gained), flagflip other
code 4 -> 6 (not looked at); instructions tsc +0.19%, valibot +0.85%, xstate +1.16%, effect +0.74% (unoptimised).
In this order:
1. **Bisect the ember regression first** with `B_anchor_envswitches.patch` (TTTT_RET_FIRST, TTTT_NO_ANCHOR, ...,
   TTTT_DEBUG_ANCHOR); B ruled out the literal-`this` anchors and the overload-failure candidate. `./expectprobe.py`
   on `types/ember__object` cuts it down. If the cause is a design flaw (an anchor derived through the wrong
   signature, a read that tsc does mid-inference), fix the design, not the case.
2. Skip the derivation where it can't differ: a non-generic call with one signature (the push-down context is the
   resolved one). Recovers most of the instruction cost, probably normalizedIntersectionTooComplex too (the anchored
   derivation builds the huge union outside the place TS2590 is reported).
3. neverReturningFunctions1: probe which check tsc's types baseline prints for `parse() { return [true]; }` in a
   ThisType'd literal argument (`true[]` vs tttt's `boolean[]`).
4. Replace the prototype's thread-local `resolve_failure_sink` with a CallInfo out-parameter.
5. The flagflip "other code" +2: look at them (`./flagflip.py -k`).
Bar: 0 lost on every oracle (`./oracles.py`), instructions within ~0.5%. If the ember regression shows anchors can't
express some read, stop and write that up here rather than patching around it.

## Stage 3 (todo entry 9): what stage 2 makes possible
1. **Widening decided where the return type is computed** (C's P1 in `C_proto_full.patch`, `V_P1=1`): a reportability
   bit stored when `infer_return_type` runs (under !strictNullChecks && noImplicitAny), deleting
   `KEY_CONTEXT_WIDENING_ABSENT` and `ty_function_widening_reportable`'s fallback chain. Before stage 2 it gave +48
   spurious TS7011 (valibot `optional(string(), () => undefined)`) and +2 TS7023 (excalidraw): tttt computed those
   return types at other moments than tsc. Measure again on top of stage 2; if the timing still differs, find which
   eager return-type computation tsc doesn't do (ty_argument_return_type in EXPR_FUNCTION is the suspect).
2. **`KEY_CONTEXT_NONE` / `KEY_CONTEXT_FIRST_ABSENT` through anchors**: each still loses something alone (A: NONE 2
   tests + effect 2; FIRST_ABSENT flagflip -22 TS7023). With the lazy return in place, try expressing "its first check
   had no signature" as the anchored read being unresolved at that moment (tsc's resolvingSignature).
3. **`KEY_LITERAL_CONTEXT` as a push/pop stack** (pushed in check_object, popped on return): if nothing reads it after
   the check returns and before the call resolves, the persisted key and check_with's generic-mode save/restore go and
   `KEY_LITERAL_INFERENCE` becomes the stack entry's inference. It carries hundreds of corpus diagnostics (vue, zod,
   valibot `this`), so measure carefully.
4. Cleanups: merge `KEY_CONTEXT_DONE` and `KEY_CONTEXT_BLOCKED` into one `KEY_CONTEXT_CHECKED` (a sentinel for "no
   signature"); rename `KEY_CONTEXT_UNFIXED` (it is tsc's getContextualSignature before fixing; store it always).
Optional, only if time is left: the inference context by ancestry at chain *entries* (tsc's getInferenceContext: a
stack of (node, OuterInference) pushed per argument check, looked up by span containment), replacing ~15
save/clear/restore sites of `my outer_inference` in call.c and stopping the flow engine's leak (C's audit, `V_LOGAUD=1`:
121 entries where flow narrowing evaluates an unrelated type-guard call while another call infers). No baseline
changes today, so it is hygiene.

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
