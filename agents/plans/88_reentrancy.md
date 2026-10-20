# Plan 88: tsc's re-entrancy (resolution barriers, nested results, unguarded members)

Written 2026-10-20 (agent A of the "real code" fan-out). Status: research done, no general mechanism adopted. The
evidence says the general model is real but only one of its paths matters on real code, and that path is gated by an
older architectural choice (tttt pushes return contexts down eagerly). Read the census before touching any in-flight
check. Tool: `scripts/reentrytrace.py` (below).

## tsc's model (tsgo 7.1, checker.go / relater.go)

- One stack, `typeResolutions`, of (entity, property) pairs: pushTypeResolution / popTypeResolution around a symbol's
  type (0), base constructor (1), alias declared type (2), signature return type (3), base constraint (4), type
  arguments (5), base types (6), write type (7), initializerIsUndefined (8), alias target (9).
- `findResolutionCycleStartIndex` only looks down to `resolutionStart` (and stops at an entry whose property is
  already set). Two places move `resolutionStart` to the stack's top: getResolvedSignature, for a call's *first*
  resolution only (`cached == nil`; a call asked again while `resolvingSignature` is resolved again with no new
  barrier), and getVariancesWorker for the *outermost* measurement (a restart from a variance circularity clears the
  variance stack, so it is outermost again: a new barrier).
- Below the barrier nothing is a cycle: the entity is computed again from scratch, nested, now above the barrier, where
  a third request does find the cycle. Termination comes from barriers being set once per call / per outermost
  measurement, not from per-entity flags. Every cache is written "if still unset" after the work: the nested
  (inner) result finishes first and wins (`links.declaredType == nil`, `resolvedReturnType == nil`,
  `resolvedBaseConstraint == nil`, ...). For calls: the nested resolution's `links.resolvedSignature` replaces the
  outer's result (getResolvedSignature's "if the signature resolution originated on a node that itself depends on the
  contextual type..."); better-auth shows the nested resolution's TS2345 is reported.
- `isResolvingReturnTypeOfSignature` uses the same scan: a return type in flight below the barrier is "not
  resolving", so getContextualReturnType asks for it, which re-enters return type inference.
- Unguarded: resolveStructuredTypeMembers has no in-flight state at all (a type whose members are being resolved is
  resolved again, nested), nor does getTypeOfInstantiatedSymbol: those recurse until the instantiation depth (100,
  TS5115/TS2589) or count (5M) limit, and the instantiation that hits the limit becomes the error type for good.

## tttt's model (2026-10-20)

Per-site approximations, each tuned to the tests that needed it:
- `Checker.resolving` and `Checker.barriers`: the same stack and barriers, pushed by resolve_call() (a nested
  re-resolution's doesn't move it, `resolving_nested_call`) and the outermost variance measurement;
  `resolved_return_type()` reads the return type outside the barrier. Each site's rule is in ty_resolving_reenters().
- Declarations (lazy_type_evaluate_inner()) and inferred return types (function_return_type()) re-enter below the
  call barrier **once** (`reevaluating`), then say circular. tsc has no "once": it re-enters until the request is
  above a barrier.
- Aliases re-enter only below the outermost variance measurement (BARRIER_VARIANCE, alias_reenters(), the apollo fix
  7238e629e). Nothing else sees the variance barrier.
- Symbol properties re-enter below the call barrier up to depth 4, instantiated properties up to 200
  (prop_type_slow(), `prop_reentry_depth`).
- Base types look for their cycle across barriers (ty_resolving_cycle_across_calls(): tsc's second round finds it
  above the barrier, tttt shortcuts to the outcome).
- A call in flight is resolved again (call_return_type(), `resolving_nested_call`), but the nested result is dropped
  (the outer one is kept, its errors only).
- Members cache while in flight ("early members", members_in_flight) and never re-enter;
  members_reentered_unguarded() synthesizes tsc's TS5115 when a probe of eager bases would have looped.
- Return contexts are computed eagerly when return inference starts (function_impl_enter_return_context()), right
  after the function's own RESOLVING_FUNCTION_RETURN push, so the own-signature case always sees "in flight above the
  barrier" and gets no context (ty_contextual_return_type_self()). tsc asks at the moment a call inside the body
  infers, below that call's barrier, and re-enters.

## The census (instrumented tsgo, `scripts/reentrytrace.py`)

tsgo with `TSGO_REENTRY=1` on every `-p` corpus (vscode aside) and a crc32 quarter of the source sweep (1362
projects, plus better-auth/apollo/arktype):

| event (tsc) | corpora | source sweep quarter | tttt on the same corpora |
|---|---|---|---|
| REENTER 3 (return type) below a call barrier | 557 (valibot, xstate, excalidraw, effect, vue, zod) | 87 projects, 1190 | 0 |
| ... of which reached through RETINFLIGHT (contextual return of the function's own signature) | all of them | 85 projects, 1155 | 0 |
| NESTEDSIG (nested call result replaced the outer) | 14 | 44 projects, 100 | (dropped) |
| MEMBERS (members re-resolved while resolving) | 35 (zod, webpack's zod d.ts to depth 24, vue) | 45 projects, 122 | never |
| REENTER 0 (symbol type) below a call barrier | 0 | 7 projects, 9 (mikro-orm/tests, tiptap, karakeep, dyad, solid, b4run) | 0 |
| REENTER 0 below a variance barrier | 0 | 7 projects, 9 (angular-components' aria tree.ts, crawlee's event_manager.ts) | not modelled |
| REENTER 1 (base constructor), 4 (base constraint) | 3 (effect TaggedClass) / 0 | 0 / 1 (angular-cli) | 0 |
| REENTER 2 (alias) | 0 | 0 (apollo's is a -b project) | apollo only |

tttt's own lazy_type/return re-entry paths (TTTT_DEBUG_REENTRY's REENTER/ONCE) fire **zero** times on valibot,
xstate, excalidraw, effect, vue and mikro-orm/tests, and those projects match tsc anyway (bar vue's known 2): tttt
reaches the same answers through a different order. Where it doesn't:
- better-auth -TS2345 (lynx-store.ts:65, react-store.ts:67): the RETINFLIGHT -> REENTER 3 -> NESTEDSIG chain with a
  *different* nested result (`useCallback(() => listenKeys(store as any, keys, emitChange))`: reproduces in a 20-line
  file, `ba.ts` shape in agents/bugs/sourcesweep_build.md). tsgo's events there: NESTEDCALL, NESTEDSIG,
  REENTER 3 call, RETINFLIGHT call; tttt's: none.
- vue 0 / 2 and arktype's TS5115: MEMBERS nesting to the limit (`r1.ts` in agents/bugs/sourcesweep_checker.md
  reproduces it standalone: tsgo nests one type's members 33 deep; tttt reports the TS5115 at `interface Root`
  (13,24), tsc at the use `p.nodes[0].each(...)` (19,1)).
- apollo cacheOverride: fixed narrowly (alias below the variance barrier).

## Experiments (all reverted)

1. Aliases re-enter below the call barrier too (alias_reenters() scanning from the innermost of both barriers, what
   tsc does): suite unchanged, but mikro-orm/tests gains +TS2456 `IQux`, +TS7022 `Qux`, +TS7023 `repository`
   (defineEntity.test.ts:205-215, `type IQux = InferEntity<typeof Qux>; class QuxRepository extends
   EntityRepository<IQux>; const Qux = defineEntity({ repository: () => QuxRepository, ... })`). tsc's trace: Qux ->
   defineEntity's barrier -> QuxRepository's base constructor -> IQux -> Qux re-entered below the barrier -> the
   nested defineEntity resolution finishes (NESTEDSIG) and nothing is circular. tttt's alias re-entry then meets Qux a
   second time with `reevaluating` set and says circular: the "once" flag, not the alias rule, is what's unfaithful.
2. The outermost variance measurement moves `resolving_start` (so declarations and return types also re-enter there,
   what tsc does): loses classVarianceResolveCircularity2's TS7022 (tsc: REENTER 0 variance twice, then the cycle above
   the barrier; tttt never reaches `Value` a third time, its nested call and variance paths differ).

Both say the same thing: the pieces only work together. Generalizing one site's rule while the others keep their
approximations moves the divergence, it doesn't remove it.

## Is a general model tractable?

Yes in principle, and it fits plan 86 stage 4: the resolving stack and the barriers are per-worker scratch already,
and "first to finish wins" is a publish-once (CAS) rule. But every value computed while something below a barrier was
re-entered is history-dependent (taint: it must not be published to a shared graph; the taint prototype's
TAINT_IN_FLIGHT_* events already mark these). The model, in order of dependency:

- **A. One barrier stack** (done 2026-10-20, plan 86 4a session 1: `Checker.barriers`, ty_barrier_push()/pop(),
  ty_resolving_reenters(); the output unchanged).
- **B. Barrier termination instead of "once".** Drop `reevaluating` (LazyMemo, FunctionReturn) and
  `prop_reentry_depth < 4`: re-enter whenever not above the innermost barrier; the nested computation is above it, so
  a third request is a cycle. Then extend A's helper to every kind below both barriers (experiments 1 and 2 become
  the faithful version). Gate: the suite, mikro-orm/tests, classVarianceResolveCircularity2, apollo's
  ttttAliasReentersBelowVarianceBarrier, and `scripts/reentrytrace.py` on the corpora (tttt's REENTER counts should
  start to appear where tsgo's are, ONCE should disappear).
- **C. Nested call results supersede.** call_return_type()'s nested resolution stores its CallResolution; the outer
  resolution, finishing later, keeps the nested one (tsc's `links.resolvedSignature != resolvingSignature`), and the
  nested resolution's diagnostics are reported (verify what tsc keeps of the outer one's). Together with tsc's chooseOverload rule for a
  single NoConstraintChecks candidate (#64192). Tried alone 2026-10-19: changes nothing, because nothing re-enters
  until D.
- **D. Return contexts pulled, not pushed.** The return expression's contextual type computed when a consumer asks
  for it (a call's return-type inference, a literal's contextual type...), not at function entry. The own-signature
  case then sees the function's return type in flight *below* the inner call's barrier and re-enters it (RETINFLIGHT
  -> REENTER 3), which is the one re-entry real code does at volume (6% of projects). This is the expensive stage:
  `Checker.return_context` is a value passed down to ty_check_expr(); a pull model needs either a lazy-context
  sentinel the consumers force, or recording that the context is "F's return context" and forcing it in
  resolve_call() after the barrier is set. Only better-auth is known to need it (2 diagnostics), and every corpus
  matches without it: do it when contextual typing is being reworked anyway, not on its own.
- **E. Members without in-flight state.** Won't fix unless members.c adopts tsc's resolution order (bases
  instantiated with the reference's arguments rather than raw members instantiated: probe_eager_bases() is the
  existing approximation). Which instantiation is the 100th decides what becomes the error type: only reachable by
  replaying tsc's exact order. vue's 2 and arktype's TS5115 stay. members_reentered_unguarded() is the hack for it
  (hacks.md).

## Recommendation

Do A and B as one execution session (gates above; expect a few suite tests to move both ways and to need tracing with
`--trace`); C after B. D is plan-sized on its own and low-value now (one project): only with a contextual-typing
rework. E: no.

## Validation

- `scripts/reentrytrace.py P` on valibot, xstate, effect, vue, mikro-orm/tests, better-auth/packages/better-auth:
  tttt's REENTER/NESTEDSIG-equivalent counts per file track tsgo's (after D; after B the REENTER 0/2 rows).
- Suite: no loss; classVarianceResolveCircularity2 and ttttAliasReentersBelowVarianceBarrier keep passing.
- Source sweep: mikro-orm/tests, crawlee, angular-components, tiptap, karakeep, dyad, solid, b4run unchanged; after C+D
  better-auth's two -TS2345 gone.
- `scripts/kdiff.py`: re-entry is order-dependent by nature; byte-identical output at every checker count must hold.
