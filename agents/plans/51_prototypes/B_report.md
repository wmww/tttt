# Plan 51, prototype B: the contextual type read lazily, through the call's resolved signature

Worktree /tmp/wt-51b (census, main tree) and /tmp/wt-51b/.proto (the prototype). Baseline (HEAD a7003eac): suite 14829,
corpora 0 except zod-root 53 / effect 3, sweep match 8594 missing 1, expecttypes 68628, flagflip missing 18 spurious 10
other 4.

## 1. Census: delete each side channel, measure

Variants are in /tmp/p51b/variants.py (each a few-line edit; `VROOT=dir variants.py a+b` applies them to a tree).
"lost" = suite components lost vs HEAD. Corpora: only the ones that moved. Wide = sweep / expecttypes / flagflip (run only
for the variants that were clean on suite+corpora).

| variant (what is deleted) | suite lost | corpora | wide (sweep match / expect / flagflip missing,spurious,other) |
|---|---|---|---|
| `self`: KEY_CONTEXT_RETURN_SELF never written | 10 comp (10 tests) | clean | - |
| `late`: ty_late_contextual_return() a no-op (NONE still recorded) | 7 | clean | - |
| `none`: KEY_CONTEXT_NONE never written (so SIG_CONTEXT, late dead) | 1 | effect 3->4 | - |
| `isself`: ty_function_context_is_self() false (TS7024) | 0 | clean | 8594 / 68628 / **50**,10,4 |
| `fctx`: KEY_FUNCTION_CONTEXT fallback in widening_reportable | 0 | clean | 8594 / 68628 / 18,10,4 (**dead weight**) |
| `iife`: KEY_CONTEXT_IIFE_RETURN never written | 21 comp (10 tests) | zod 1, zod-root 54, ts-eslint 1 | - |
| `lparent`: KEY_LITERAL_PARENT never written | 15 comp (6 tests) | webpack 23, vue 49, excalidraw 3 | - |
| `linfer`: KEY_LITERAL_INFERENCE never written | 0 | valibot 3 | - |
| `lctx`: `this` of literal methods ignores KEY_LITERAL_CONTEXT | 51 comp (22 tests) | valibot 195, zod 222, vue 520, webpack 30, effect 16, vscode 13, excalidraw 16 | - |
| `lctxgen`: generic-mode save/restore of KEY_LITERAL_CONTEXT | 0 | clean | not run (combo below) |
| `retfirst`: KEY_CONTEXT_RETURN first write wins (not rewritten per check) | 7 | clean | - |
| `memofirst`: expression memo, first contextual check wins | 19 | clean | - |
| `widening`: ty_function_note_widening_context no-op (KEY_CONTEXT_WIDENING) | 0 | clean | flagflip 18,10,4 (sweep/expect not run; gated on !strictNullChecks) |
| `blocked`: KEY_CONTEXT_BLOCKED never written | 0 | clean | flagflip **36**,10,4 (vscode/eopt TS7006) |
| all of isself+fctx+widening+blocked+lctxgen | 0 | clean | 8592 (spurious 2) / 68596 / 58,58,14 |

Dead weight on every oracle measured: `fctx` (KEY_FUNCTION_CONTEXT) for sure, `widening` very likely (suite, corpora,
flagflip unchanged), `lctxgen` likely (suite+corpora clean; the combo's sweep/expect/flagflip damage is explained by
isself (flagflip +32), blocked (+18) and the remaining combo spurious; not isolated). Everything else carries. The suite+corpora filter alone is
misleading here: four variants are clean there and only flagflip (the non-strict, noImplicitAny-without-strictNullChecks
and exactOptionalPropertyTypes cells) sees them.

### What the carrying channels stand for (buckets)

- **Own signature as context** (`self`, `late`; tests selfReference, objectFreeze, functionExpressionContextualTyping3,
  indexSignatures1, intraExpressionInferences, conditionalTypeBasedContextualTypeReturnTypeWidening,
  thislessFunctionsNotContextSensitive1, silentNeverPropagation, contextuallyTypedParametersWithInitializers1,
  amdLikeInputDeclarationEmit): `id(() => 1)`-shaped calls where the type parameter is inferred as the function itself,
  so the resolved signature's parameter is the function's own type and tsc's getReturnTypeFromBody keeps its unit
  return (`contextualSignature === getSignatureFromDeclaration(func)`). This is exactly the lazy read: the return type
  is computed after the call resolved and the context is read through links.resolvedSignature.
- **ContextChecked** (`none`, `blocked`): "the first check without a contextual signature decides there is none" - tsc's
  NodeCheckFlags.ContextChecked. Not a lazy-read artefact; it is the *other* mechanism of plan 51 (prototype A's
  subject). `none`'s 1 test + effect 1 (`Effect.mergeAll(() => 1, (_b, a) => () => a)`, TS2322 `number` vs `1`) is a
  function that would otherwise take its own type as contextual signature in the applicability check.
- **TS7024 self-circularity** (`isself`, 32 flagflip missing): tsc's shouldReportErrorsFromWideningWithContextualSignature
  asking for the own signature's return type while it resolves. Needs the "context is own" fact, which the lazy read
  gives (see stage 1 below).
- **IIFE context** (`iife`): getContextualReturnType's iife branch, `getContextualType(iife)`; recorded at the IIFE's
  first contextual check. A fixed context, not a last-check hack; justified.
- **Literal parent links** (`lparent`): a syntactic parent table for nested object literals (ThisType<T> markers in
  outer literals: vue, webpack). Justified as a parent side table (tsc has `node.parent`).
- **Literal contextual type for `this`** (`lctx`, `linfer`): tsc reads getContextualType(literal) while the call is
  still inferring, through the pushed contextual type and the inference context (getInferenceContext). The persisted
  KEY_LITERAL_CONTEXT is tttt's version of tsc's contextualTypes stack + inferenceContexts stack; it can't go while
  reads happen mid-inference (which is most of vue/zod/valibot: ThisType and `this.x` inside methods of an argument).
- **Return context per check** (`retfirst`; inferentialTypingWithFunctionTypeNested, inferTupleFromBindingPattern,
  contextuallyTypeAsyncFunctionReturnType, typeParameterConstModifiersReturnsAndYields, unionAndIntersectionInference1,
  dependentDestructuredVariablesFromNestedPatterns, conditionalTypeBasedContextualTypeReturnTypeWidening): the last
  check is the applicability check against the resolved signature; "rewritten per check" stands in for the read
  through the resolved signature.
- **Expression memo** (`memofirst`; objectFreezeLiteralsDontWiden, correlatedUnions, functionOverloads39, const type
  parameter tests, reverse mapped...): the printed node type is the last (post-resolution) check's, like tsc's
  resolvedType written by the final Normal-mode check. This mirrors tsc; it is not one of the lazy-read hacks. Keep.

## 2. The prototype: context anchors

Design (B_anchor.patch, ~290 lines, mostly src/ty/expr.c):
- `ContextAnchor` {ARG(call, index) | PROP(literal, name or computed key, parent) | ELEM(literal, index, parent) |
  RETURN(function)}, arena-allocated. `ty_record_call_anchors()` walks a call's arguments *syntactically* once, when its
  resolution starts (call.c call_return_type), through object literal properties/methods, array elements, ternary
  branches and the return statements of function arguments (block + expression bodies, recursively), and records
  `KEY_FUNCTION_ANCHOR` (FunctionImpl -> anchor) and `KEY_LITERAL_ANCHOR` (object literal -> anchor); first one wins.
- `anchor_context()` derives the contextual type *now*: ARG = the resolved signature's parameter type at the index,
  instantiated with the resolved mapper (CallResolution keeps `mapper`, and for a failed call the overload-failure
  candidate, tsc's resolvedSignature then); PROP/ELEM reuse check_object/check_array's helpers
  (apparent_literal_context, discriminate_by_object_members, contextual_named_prop_type / contextual_prop_type,
  contextual_array_element_type); RETURN = the return type of the enclosing function's anchored contextual signature
  (not while its own return type is in flight: isResolvingReturnTypeOfSignature). While the call is unresolved the
  anchor says "not anchored" and the reader falls back to the push-down value: tsc's pushed contextual type
  during inference.
- KEY_CONTEXT_RETURN stores the contextual *Signature*, first one wins (tsc's ContextChecked: the first contextual
  check decides); its return type is read when asked (`ty_contextual_return_type_self()`, which also says whether the
  signature is the function's own). For a non-own signature the return type is still resolved at set time
  (errors like TS2590 are reported there).
- Composite guard: a union contextual type with 2+ callable members, one of which is a function whose return is in
  flight, gives no contextual signature (tsc's isResolvingReturnTypeOfSignature over compositeSignatures;
  compositeContextualSignature).
- ty_contextual_this_type reads the literal's context through its anchor once the call is resolved.

Deleted with it: KEY_CONTEXT_RETURN_SELF, ty_late_contextual_return() + KEY_FUNCTION_SIG_CONTEXT, KEY_FUNCTION_CONTEXT,
the `overload_failure_inference` exception in EXPR_FUNCTION, the per-check rewrite of KEY_CONTEXT_RETURN;
ty_function_context_is_self() re-derived from the lazy read (keeps KEY_CONTEXT_NONE / FIRST_ABSENT as the "first check
had no signature" condition). Kept: KEY_CONTEXT_NONE (as ContextChecked), KEY_LITERAL_CONTEXT/INFERENCE/PARENT
(mid-inference reads), KEY_CONTEXT_IIFE_RETURN, the memo.

Results (B_anchor.patch):

| oracle | HEAD | prototype |
|---|---|---|
| suite | 14829 | 14827 (lost 2: neverReturningFunctions1 TYP, normalizedIntersectionTooComplex ERR; gained 0) |
| corpora | clean | clean (all 17 unchanged) |
| sweep | match 8594, missing 1 | match 8590, **spurious 4** (+TS2578 in types/ember, ember/v3, ember__object, ember__object/v3) |
| expecttypes | 68628 | 68595 (-36 in the same four ember packages: `this.foo` in a method of an `extend()` argument is `any`, was `string`; +3: ember__service and v3 now `""` as tsc says, the type_core.md item) |
| flagflip | 18 / 10 / 4 | 18 / 10 / 6 (other code +2, not looked at) |

So: on suite+corpora the prototype is nearly 0-lost; the wide oracles show one real regression family (ember's
overloaded `extend` with ThisType markers; repro types/ember/test/core-object.ts:71, `this.foo; // $ExpectType string`
inside `func1()` of `Ember.CoreObject.extend({...}, {...})`). Not caused by the literal-`this` anchors nor by the
overload-failure candidate (switching either off leaves the 36); so it's the function anchors / first-wins
KEY_CONTEXT_RETURN of stage 1, to bisect with B_anchor_envswitches.patch (TTTT_RET_FIRST, TTTT_NO_ANCHOR,
TTTT_KEEP_SELF, TTTT_KEEP_LATE...). TS7024 needed one correction found by flagflip: "own signature" must be decided at
the time the return type is inferred (function.c writes KEY_CONTEXT_RETURN_SELF then), not when TS7024 is reported
(the first version said spurious 58: valibot `optional(string(), () => undefined)`).

The census of "what anchors make deletable" on the suite alone: with anchors, `retfirst` (7 lost alone) and
`self`/`late`/SIG_CONTEXT (10/7 alone) go with 0 suite loss; `memofirst` still loses its 19 (expected, see buckets);
`none` still costs effect 1 (it is ContextChecked, not a lazy read).

Remaining losses:
- neverReturningFunctions1 (TYP): `parse() { return [true]; }` inside a ThisType'd literal argument
  (`registerComponent(..., { schema: { myProperty: { parse() {...} } } })`): the anchor says parse's context is its own
  signature (T inferred as the literal), so the return expression has no contextual type and `[true]` prints
  `boolean[]`; tsc prints `true[]` (its node type is from the check during inference). Needs a look at which check
  tsc's types baseline sees.
- normalizedIntersectionTooComplex (ERR, TS2590 missing): the anchored derivation builds the huge union for
  `ref: x => ...` outside the place where tsc reports it (the push-down check). Wrapping the derivation in
  ty_enter_node(function) didn't help: likely the union is created (and cached as error) in a context whose errors are
  dropped. Fixable by not deriving through anchors for non-generic calls (the push-down context is exact there).
- genericCallWithGenericSignatureArguments3 still fails as at HEAD: the inner arrow's return type is computed
  during inference (eagerly), before the resolved signature exists; laziness of return types is the missing half.

Cost (measured before the final TS7024 correction, which only adds a key write per self-context return; callgrind, `scripts/instrcount.sh --bin`, HEAD -> prototype): tsc 4.772 -> 4.781 G (+0.19%), valibot 4.236 -> 4.272
(+0.85%), xstate 3.186 -> 3.223 (+1.16%), effect 10.054 -> 10.128 (+0.74%). Unoptimised: anchors are walked and
allocated for every call's literal arguments, generic or not; derivations are not cached. Memory: no new AST field;
one arena anchor (48 bytes) per function/object literal/property under a call argument, plus an intern entry each.

## 3. Recommendation for plan 51's execution

Verdict: **port the read side, not the whole pull model.** tsc's model has two halves and tttt needs both, in different
places:
- while a call resolves, a node's contextual type is what was *pushed* for it (tsc's contextualTypes /
  inferenceContexts stacks). tttt's push-down ExprCtx is that half already; KEY_LITERAL_CONTEXT/INFERENCE are its
  "pushed stack" persisted past the check (they carry vue/zod/valibot `this`; `lctx` costs hundreds of corpus
  diagnostics), so they are justified as such, not lazy-read hacks.
- once the call is resolved, every later read goes through the resolved signature (links.resolvedSignature). That half
  is what the "last check" side channels approximate, and anchors replace them cleanly.
A full parent-pointer getContextualType would duplicate every check_* case's context logic in a second, pull-shaped
copy (two sources of truth) and cost an Expr->parent table for every node; the anchor walk only touches call
arguments,
reuses the push-down helpers per step, and needs no AST field. I chose anchors for that reason.

Stages (each 0 lost on every oracle; the census and B_anchor.patch are the starting point):

1. **Dead weight first** (no design needed): delete `KEY_FUNCTION_CONTEXT` (fctx: 0 on every oracle) and
   `ty_function_note_widening_context` / KEY_CONTEXT_WIDENING if the sweep/expect run confirms (widening: suite,
   corpora and flagflip all unchanged; it's gated on !strictNullChecks so the strict sweep shouldn't see it), the generic-mode
   save/restore of KEY_LITERAL_CONTEXT (lctxgen: 0 on suite and corpora, wide oracles not isolated). Not
   KEY_CONTEXT_BLOCKED (flagflip missing 18 -> 36) nor is_self (18 -> 50).
2. **Anchors + lazy contextual return** (B_anchor.patch): ContextAnchor + KEY_FUNCTION_ANCHOR/KEY_LITERAL_ANCHOR
   recorded by `ty_record_call_anchors()` from call.c's `call_return_type` (resolution start); CallResolution keeps
   the mapper and the overload-failure candidate (`ty_call_resolved_candidate`; replace the prototype's thread-local
   `resolve_failure_sink` with a proper CallInfo out-param); KEY_CONTEXT_RETURN holds the Signature, first wins;
   `ty_contextual_return_type_self()` replaces `ty_contextual_return_type()`; function.c's self case and TS7024 go
   through it (KEY_CONTEXT_RETURN_SELF = "inferred with its own signature as context", written by function.c only).
   Deletes: ty_late_contextual_return, KEY_FUNCTION_SIG_CONTEXT, the `overload_failure_inference` branch in
   EXPR_FUNCTION, set_contextual_return's in-flight special case, the per-check rewrite. Expected losses
   (prototype): neverReturningFunctions1 (TYP), normalizedIntersectionTooComplex (ERR TS2590), and the ember
   `extend` family (sweep 4 packages, $ExpectType -36), see above; gains: ember__service's `""` (+3). Fix before
   landing: (0) bisect the ember regression first (it is the one that says something about the design); (a) skip the anchor derivation where it can't differ (a non-generic single-signature call: the
   push-down context *is* the resolved one); that also recovers most of the instruction cost, and probably
   normalizedIntersectionTooComplex; (b) neverReturningFunctions1 needs a probe against tsc's types baseline.
3. **Literal `this` through anchors after resolution** (already in the patch, neutral on the suite): then try making
   KEY_LITERAL_CONTEXT a real stack (pushed in check_object, popped on return) to see whether any read of it happens
   after the check returns *and* before the call resolves (deferred function body checks run after resolution, so it
   may be none). If none, the persisted key and the `generic_mode` save/restore go; KEY_LITERAL_INFERENCE becomes the
   stack entry's inference.
4. Left as is, with hacks.md entries rewritten as justified: KEY_CONTEXT_NONE / BLOCKED / FIRST_ABSENT
   (ContextChecked, prototype A's subject), KEY_CONTEXT_IIFE_RETURN (getContextualType(iife), fixed at the first
   check), KEY_LITERAL_PARENT (a parent side table), the expression memo's "last contextual check wins" (it *is* tsc's
   behaviour: the final Normal-mode check against the resolved signature writes the node's type; `memofirst` loses 19
   tests and anchors don't change that).

Arguing against going further: genericCallWithGenericSignatureArguments3 and the DT ember__service / map-to-obj cases
are not fixed by lazy reads alone: tttt computes return types eagerly during inference (ty_argument_return_type,
set_contextual_return resolving the contextual return) where tsc defers them. Lazy return types are a separate,
riskier change (ordering of union members and of diagnostics depends on when return types are inferred).


## Files

- /tmp/plan51/B_anchor.patch: the prototype, switches hardcoded (anchors on, RETURN_SELF/late off, first-wins on);
  this is what the numbers above are for. Apply to a7003eac.
- /tmp/plan51/B_anchor_envswitches.patch: the same with TTTT_* environment switches (and TTTT_DEBUG_ANCHOR logging) to
  bisect; *without* the final TS7024 correction (is_self there reads the lazy self, which made flagflip spurious 58).
- /tmp/plan51/B_anchor_stage1.patch / B_anchor_stage2.patch: intermediate states (function anchors only / plus literal
  `this`).
- /tmp/p51b/variants.py: the census edits; /tmp/p51b/census/*: raw outputs (`*.lost` = lost test names per variant).
