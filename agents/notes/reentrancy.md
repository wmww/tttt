# tsc's re-entrancy and tttt's approximations
Research of 2026-10-20/21 (the full write-up: `git show af936072c:agents/plans/88_reentrancy.md`). The general
model is real, but only one of its paths matters on real code, and that path is gated by tttt pushing return contexts
down eagerly. Read this before touching any in-flight check. Tool: `scripts/reentrytrace.py` (instrumented tsgo with
`TSGO_REENTRY=1` against tttt's `TTTT_DEBUG_REENTRY=1`).

## tsc's model (tsgo 7.1, checker.go / relater.go)
- One stack, `typeResolutions`, of (entity, property) pairs around a symbol's type (0), base constructor (1), alias
  declared type (2), signature return type (3), base constraint (4), type arguments (5), base types (6), write type
  (7), initializerIsUndefined (8), alias target (9).
- `findResolutionCycleStartIndex` looks only down to `resolutionStart`. Two places move it to the top:
  getResolvedSignature for a call's *first* resolution (`cached == nil`), and getVariancesWorker for the *outermost*
  measurement. Below the barrier nothing is a cycle: the entity is computed again, nested, above the barrier, where a
  third request finds the cycle. Termination comes from barriers being set once per call / outermost measurement.
- Every cache is written "if still unset" after the work: the nested result finishes first and wins. For calls the
  nested resolution's `links.resolvedSignature` replaces the outer's, and its diagnostics are reported (better-auth).
- `isResolvingReturnTypeOfSignature` uses the same scan: a return type in flight below the barrier is "not
  resolving", so getContextualReturnType asks for it and re-enters return type inference.
- Unguarded: resolveStructuredTypeMembers and getTypeOfInstantiatedSymbol have no in-flight state; they recurse
  until the instantiation depth (100, TS5115/TS2589) or count (5M) limit, and the instantiation that hits it becomes
  the error type for good.

## tttt's model (per-site, documentation.md "LazyType")
- `Checker.resolving` and `Checker.barriers` (ty_barrier_push(): a call's first resolution, the outermost variance
  measurement); `ty_resolving_reenters()` holds each site's rule.
- Declarations and inferred return types re-enter below the call barrier every time, like tsc (barrier termination;
  the old "once" flags were removed 2026-10-21 with every oracle byte-identical; the suite never fired ONCE).
- Aliases re-enter below the innermost barrier, a call's or a variance measurement's (alias_reenters(); variance
  only until 2026-10-23, the apollo fix 7238e629e).
- Symbol properties re-enter below the call barrier up to depth 16 (`prop_reentry_depth`, 4 until 2026-10-23; without a bound only the
  barrier would stop it), instantiated properties up to 200.
- Base types look for their cycle across barriers (ty_resolving_cycle_across_calls(): a shortcut to tsc's outcome).
- A call in flight is resolved again, but the nested result is dropped (the outer one is kept).
- Members cache while in flight and never re-enter; members_reentered_unguarded() synthesizes tsc's TS5115 (hacks.md).
- Return contexts are computed eagerly at function entry (function_impl_enter_return_context()), so the
  own-signature case sees "in flight above the barrier" and gets no context; tsc asks later, below the inner call's
  barrier, and re-enters.

## The census (tsgo on every `-p` corpus but vscode and a quarter of the source sweep, 1362 projects)
| event (tsc) | corpora | source sweep quarter | tttt |
|---|---|---|---|
| REENTER 3 (return type) below a call barrier | 557 (valibot, xstate, excalidraw, effect, vue, zod) | 87 projects, 1190 | 0 |
| ... reached through RETINFLIGHT (own signature's contextual return) | all | 85 projects, 1155 | 0 |
| NESTEDSIG (nested call result replaced the outer) | 14 | 44 projects, 100 | dropped |
| MEMBERS (members re-resolved while resolving) | 35 (zod, webpack's zod d.ts to depth 24, vue) | 45 projects, 122 | never |
| REENTER 0 (symbol type) below a call barrier | 0 | 7 projects, 9 | 0 |
| REENTER 0 below a variance barrier | 0 | 7 projects, 9 (angular-components' aria tree.ts, crawlee) | not modelled |
| REENTER 1 / 4 (base constructor / constraint) | 3 (effect TaggedClass) / 0 | 0 / 1 (angular-cli) | 0 |
| REENTER 2 (alias) | 0 | 0 | apollo only |
tttt reaches tsc's answers through another order on valibot, xstate, excalidraw, effect, vue and mikro-orm/tests.
Where it doesn't: better-auth's -TS2345 (lynx-store.ts:65, react-store.ts:67: RETINFLIGHT -> REENTER 3 -> NESTEDSIG
with a different nested result; a 20-line `ba.ts` repro in bugs/sourcesweep_build.md), vue 0 / 2 and arktype's TS5115
(MEMBERS nesting to the limit; `r1.ts` in bugs/sourcesweep_checker.md: tsgo nests one type's members 33 deep).

## Experiments (reverted)
1. Aliases re-entering below the call barrier too (tsc's rule): suite unchanged, mikro-orm/tests gained TS2456/TS7022/
   TS7023 at defineEntity.test.ts:205-215 because of the then-present "once" flag. **Landed 2026-10-23, with the two
   order fixes it needed** (below)
   (opencode): `type P = Schema.Type<typeof P>` beside `const P = withStatics(s, { make: (input: Omit<P, "id">): P
   => ... })`. Reached from the alias first, P's call infers from the arrow, whose parameters meet the alias in flight
   below the call's barrier: tsgo re-enters (REENTER 2 call, instrumented), while tttt made the alias the error type for
   good, silently (ty_resolving_cycle() found nothing above the barrier). Which file was checked first decided it (R =
   any/unknown in effect's Effects, TS2719 on branded ids, 8-27 errors varying run to run;
   test/cases/ttttAliasReentersBelowCallBarrier.ts). With aliases re-entering, opencode's `Instance.Services` /
   `LayerNode.Output<typeof graph>` chain (graph -> nodes -> the supervisor's layer -> PluginInternal.list -> pre ->
   ShellTool.Plugin -> ... -> graph, every step a call argument) nests property re-entries deeper: the depth-4
   backstop in prop_type_slow() returned circular for `PluginInternal.list` (so the call `PluginInternal.list()` was
   any and the supervisor got TS7006/TS2322) only when location-service-map.ts came first. Raised to 16 (64 gave byte-identical output on opencode, mikro-orm and crawlee); tsgo has no
   bound. TTTT_CHECK_ONLY=src/location-service-map.ts,src/plugin/supervisor.ts at --checkers 1 in packages/core was
   the recipe. mikro-orm/tests regressed again (TS2456/TS7022/TS7023 at defineEntity.test.ts:205-215), without any
   "once" flag: (1) tsc's checkTypeReferenceNode checks type arguments before resolving the reference, so `type IQux =
   InferEntity<typeof Qux>` evaluates Qux first and IQux is first asked inside Qux's call (tttt resolved the alias
   first; identifier_type_typecheck_args() now runs first); (2) the nested resolution of defineEntity's call (below its
   barrier, after IQux -> typeof Qux re-entered Qux) asks, through an identity relation in inference, for the return
   type of `repository: () => QuxRepository`, which the outer contextual check is inferring. tsc's
   contextuallyCheckFunctionExpressionOrObjectLiteralMethod calls getReturnTypeFromBody without pushTypeResolution,
   so the request infers it again, nested; tttt's ty_argument_return_type() pushed RESOLVING_FUNCTION_RETURN and found
   the cycle above the barrier. Now the contextual check doesn't push, and a return in flight with no stack entry
   re-enters (test/cases/ttttContextualReturnNotOnStack.ts). (2) alone fixes the reduced case and mikro-orm; (1) is
   tsc's order. Found by an instrumented tsgo printing every pushTypeResolution cycle and below-barrier re-entry with
   Go stacks (a small overlay of checker.go, built with GOCACHE outside /tmp).
2. The outermost variance measurement moving `resolving_start` for declarations and returns too: loses
   classVarianceResolveCircularity2's TS7022 (the nested call and variance paths differ).
Lesson: the pieces only work together; generalizing one site's rule while the others keep their approximations moves
the divergence.

## Outstanding stages
- **C. Nested call results supersede**: call_return_type()'s nested resolution stores its result; the outer keeps the
  nested one (tsc's `links.resolvedSignature != resolvingSignature`) and the nested diagnostics are reported; with
  tsc's chooseOverload rule for a single NoConstraintChecks candidate (#64192). Alone it changes nothing (tried
  2026-10-19): nothing re-enters until D.
- **D. Return contexts pulled, not pushed**: the return expression's contextual type computed when a consumer asks,
  so the own-signature case re-enters (the one re-entry real code does at volume, 6% of projects). `Checker.return_
  context` is a value passed down to ty_check_expr(); a pull model needs a lazy-context sentinel or forcing "F's return
  context" in resolve_call() after the barrier is set. Known to matter: better-auth's 2 diagnostics and the zod getter
  case (bugs/missing_checks.md). Do it with a contextual-typing rework, not alone.
- **Test list for C/D: bun's differential cases missing TS7022** (`scripts/buncases.py -k=-TS7022`, 46 cases on
  2026-10-23): 08-C11e 09-C7b 09-P6 10-C1 10-C1b 10-C2b 10-C5 10-C5d 10-C5f 10-P2 11-P2 11-P4 16-C1a 16-C1b 16-C1c
  17-C4 19-P4 21-C1b 22-C2b 30-P1 31-P1 31-P2 31-P7 31-P8 39-C5a 43-P1 51-L08 57-L15 59-L20 62-L13a 63-L04 63-L09
  72-R15a 72-R15b 76-R20 81-R01 81-R09 85-R03a 85-R03c 85-R03g 85-R16c 89-R12e 89-R14 99-Q1 99-Q2 99-V4. Most are
  circularities tsc finds through a call resolved while a declaration is in flight. The smallest is 10-C1b,
  `function i2<T>(x: T, y = i2(x, undefined)) { return x; }`: tsc reports TS7022 on y, and tttt's only event under
  `TTTT_DEBUG_REENTRY=1` is `NESTEDCALL a.ts:1` (the nested resolution's result is dropped, stage C). Also 10-C1
  (`function g4() { return f4(1, 2); } function f4(a: number, b = g4())`: TS7023 g4 + TS7022 b) and 21-C1b
  (`declare const helper: (n: number) => typeof k; const k = compute(1); function compute(v) { helper(1); return v; }`).
  Real code: crowd-dev's connectors (`const log = ctx.child({ cursor })` re-entered through cursor's back edge and an
  annotated `const connection: T | null = fetchPage(ctx, log)`'s assignment narrowing, bugs/sourcesweep_checker.md).
  Not all are re-entry: some are other eager orders (16-C1a: a base type `Pick<typeof xa, "a">` with `xa = ya!`; 10-C2b:
  a setter parameter `typeof x10` in x10's own initializer). Triage each with `scripts/reentrytrace.py` first.
- **F. Lazy parameter typing** (design only, cor3 2026-10-29; not built: not a bounded change). tsc's
  getSignatureFromDeclaration types nothing: a signature's parameters are symbols, and its shape comes from syntax
  alone (count, `this` by name, minArgumentCount from `?`/initializer/rest/an untyped IIFE parameter past the written
  arguments, HasRestParameter, the untyped-JS flag through getContextualType). A parameter's type is getTypeOfSymbol
  of it when someone asks, under its own pushTypeResolution; getTypeOfParameter/getTypeAtPosition add the optional
  undefined then. instantiateSignature instantiates the symbols, each instantiated symbol typed on demand
  (getTypeOfInstantiatedSymbol). Who asks: isSignatureApplicable/checkApplicableSignature only for the positions that
  get an argument (and the rest type for a spread); TS2554's counts need no types; compareSignaturesRelated,
  inference from signatures and printing ask all. tttt: function_signature_params() types every parameter of a
  declared signature in one pass (ty_binding_type each, then the optionality pass that needs min_args), and
  signature_resolve_slow() instantiates every parameter type of an instantiated one; the record (SigResolved) is
  published whole (one SharedWord), re-entries read sig_resolving_record()'s patch (typed so far, the one in flight
  any, the rest typed then). What differs: a re-entry that passes an argument for the in-flight parameter gets any
  where tsc reports the parameter's circularity (10-C1b's TS7022 on y in `function i2<T>(x: T, y = i2(x,
  undefined))`, 99-V4's assignNonContextualParameterTypes__01/02), and parameters after the one asked are typed
  inside a cycle they aren't part of (kebo's MST cycle, openclaw's TS2554 history, pothos's TS7023 via variance).
  The design: SigResolved keeps the syntactic shape (count, min_args including the default-before-required rule,
  optional, rest, this position, decl) up front; SigParam.type becomes a slot read only through
  `sig_param_type(me, rs, i)`: declared, ty_binding_type(binding) (its own RESOLVING_BINDING entry and memo: the
  cycle tsc finds) plus the optionality undefined; instantiated, the target's slot instantiated by the mapper,
  memoized per (signature, index) (a SharedWord per parameter, or a KEY_SIG_PARAM memo kind classified as a graph
  memo with the taint rule); `this_type` likewise. The re-entry patch (sig_resolving_record, SigResolving.typing/
  typed/whole/completing) goes away. Cost and risk: ~76 sites read `params[i].type` directly (relate.c, call.c,
  infer.c, ty_display.c, declaration_emit.c, check.c, flow.c, iterate.c, conditional.c, members.c, class_decl.c,
  graph_debug.c) and ~120 handle SigResolved; the relation and inference loops are hot (an extra branch and load per
  parameter: measure instructions on drizzle, zod, vscode); the shared graph's publication of a per-parameter word
  needs the taint/claim protocol of a lazy field (plan 86 4f), and `undefined_added` is decided at typing time. Gate
  list: the suite (functionWithDefaultParameterWithNoStatements*, the 99-V4 assign*ParameterTypes family, 10-C1/
  10-C1b, ttttGenericArgumentParamsUntypedWithoutContext), kebo/apps/mobile, openclaw core at K1 and K16 (kdiff),
  pothos (ttttVarianceReentersOwnReturnType), checkerdiff and threadcheck (`quickgate -k`), expecttypes, the DT sweep,
  instrcount. A stint of its own (estimate 2-3 days with the graph part); do the accessor refactor first as an
  output-identical commit, then the laziness.
- **E. Members without in-flight state**: won't fix unless members.c adopts tsc's resolution order (bases instantiated
  with the reference's arguments); which instantiation is the 100th decides the error type. vue's 2 and arktype's
  TS5115 stay (bugs/tsgo_divergences.md).
Validation for any of them: the suite, classVarianceResolveCircularity2 and ttttAliasReentersBelowVarianceBarrier,
mikro-orm/tests, crawlee, angular-components, tiptap, karakeep, dyad, solid, b4run unchanged; better-auth's -TS2345
gone after C+D; kdiff byte-identical.
