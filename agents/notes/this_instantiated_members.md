# This-instantiated members (design and progress, det3, 2026-10-24)

Two real projects hit this gap: `test/cases/ttttThisInstantiatedMethodErased.ts` (from hono, 2026-10-22) and
hono/tsconfig.build.json's spurious TS2769 at src/request.ts:214 (bugs/sourcesweep_checker.md). The human asked for
the structural fix; this note is the plan and the log.

## What tsc does
- A class (and an interface that isn't thisless) is its own type reference. resolveTypeReferenceMembers pads the type
  arguments with the type itself for `this` (`HonoRequest<I>` = `[I, HonoRequest<I>]`), so the declared type's members
  are an instantiation with `this -> HonoRequest<I>`.
- createInstantiatedSymbolTable(..., mappingThisOnly) keeps a member as it is only if isThisless(symbol): the symbol
  has exactly one declaration, and that declaration is thisless (annotated return and parameters, none mentioning
  `this`). An overloaded method (overloads plus an implementation) is never thisless.
- An instantiated member's type is instantiated by getObjectTypeInstantiation, which keeps only the outer type
  parameters isTypeParameterPossiblyReferenced finds over *all* the declarations. A declaration with a body and no
  return type annotation possibly references every outer type parameter, `this` included, and so does a signature that
  mentions `this`. When `this` survives the filter, the method type is a new Instantiated object, even for `this ->` the
  type itself.
- Through a receiver of type `this` (getTypeWithThisArgument(C, this)) the arguments equal the parameters, so the
  members are the raw declared ones.
- signaturesRelatedTo, "instantiations of the same anonymous type" (both Instantiated, same symbol, or references to
  one target), compares the signature lists pairwise with type parameters erased. That is why `this` related to
  `HonoRequest | R` passes in tsc: `valid` of `HonoRequest<I>` (an instance) against `valid` of `HonoRequest<{}>` (an
  instance), erased.
- Variances: `HonoRequest<I>` and `HonoRequest<{}>` are two references to one target, so tsc relates them by alias or
  type-argument variances first (structuredTypeRelatedTo, relateVariances), with the structural fallback only for
  unmeasurable or unreliable variances.

## What tttt does today
- A class or interface is a TY_DECLARED. Its members as a type of its own come from resolve_members_inner():
  resolve_members_with_this(declared, own params, self) with `this_only`. instantiate_members() keeps a member that is
  `thisless` (members.c, from function_is_thisless() over the method's *signatures*, which leave out the implementation
  and don't require a single declaration) and instantiates the rest.
- instantiate_anonymous() (instantiate.c) keeps an anonymous type when its free parameters don't include what the
  mapper maps. ty_anonymous_from_overloads() (lower.c) collects the free parameters of the overloads only, not of the
  implementation, so an implementation without a return type annotation doesn't make the type's free parameters
  unknown.
- Net effect: hono's `valid` on the declared type is the raw method type, never an instance. relate.c's
  same_signature_origin() (both TY_ANONYMOUS with one target) fails, and the generic signatures are compared unerased.
- ty_display.c's method_this_instantiated() already models tsc's rule, but only for printing (fresh type parameter
  names in declaration emit, bugs/dts_emit.md).
- `HonoRequest<I>` (TY_DECLARED) against `HonoRequest<{}>` (TY_REFERENCE) has no variance path: it goes structural.
  tsc fails it on I's variance, and tttt reaches the same answer only because the structural comparison happens to fail
  on `valid`. Erasing `valid` alone, the relation-side shortcut tried on 2026-10-24, makes that structural comparison
  pass where tsc reports TS2345 (bugs/sourcesweep_checker.md, the hono entry).

## Changes, staged (each lands green on its own)
1. **Member instantiation** (members.c, lower.c):
   - `Prop.thisless` follows tsc's isThisless: false when the method has more than one declaration (an implementation
     counts).
   - ty_anonymous_from_overloads() adds the implementation's free parameters, so a body without a return annotation
     makes them unknown.
   - Then the declared type's `valid` is an instance, and same_signature_origin() works unchanged.
   - Check that a `this`-receiver keeps the raw members (ty_with_this_arg(C, this) with arguments equal to the
     parameters).
   - Retire method_this_instantiated() in ty_display.c if the instances now carry the information.
2. **Variances between a declared type and its instantiations** (relate.c): relate a TY_DECLARED with this_param (or
   type params) against a TY_REFERENCE to itself through the reference-to-reference variance path, as if the
   declared type were `ref(target, own params)`.
   - Without this, stage 1 can turn tsc's TS2345 into a pass for a method with an annotated implementation. The probe
     is the test's class with `valid(target: keyof VT): any`.
   - A separable piece: a second worker could take it in parallel.
3. **Cleanups**: hacks that compensate, if any surface during 1-2 (grep relate.c and members.c for comments citing
   "declared type" vs "reference").

## What it should close
- test/cases/ttttThisInstantiatedMethodErased.ts (errors).
- hono/tsconfig.build.json and tsconfig.spec.json: byte-identical to tsgo (sourcesweep_checker.md hono entry).
- The probe variant with an annotated implementation must keep tsgo's TS2345 (to become a test case in stage 2).
- Possibly display: the dts_emit.md fresh-type-parameter item, if method_this_instantiated() becomes unnecessary.
- No other `this`-type items were found in missing_checks.md, hacks.md or triage.md.

## Perf risk and measurement
- Stage 1 adds an instance per qualifying method (overloaded, or a body without a return annotation) of every class
  or interface type with a `this` type, wherever its members are resolved for itself.
- Class-heavy code has many such methods: vscode, excalidraw and nest are the corpora to watch, plus drizzle and zod.
  Measure with `scripts/instrcount.sh vscode excalidraw nest drizzle zod` against the base (`--vs REV`), per stage.
  Budget: +0.3% per stage, or an explanation; the types count from `tttt --time` too.
- Stage 2 should be neutral or a win (variances replace structural comparisons).

## Estimate
- Stage 1: half a day to a day, including breadth (suite, DT sweep, $ExpectType, sampled source sweep, kdiff,
  threadcheck, checkerdiff).
- Stage 2: one to two days. Variance measurement already exists for references, but the declared-versus-reference
  entry point needs care with recursion identities and caches.
- Stage 3: as found.

## Progress log
- 2026-10-24: note written. Stage 1 started. The two changes above (thisless per isThisless, the implementation's free
  parameters) don't flip the test yet. The source of the failing comparison is still the raw `valid` (relate.c debug
  print), so the declared type's member isn't the one being related, or instantiate_anonymous() keeps it. Next: find
  which members the relation reads (the `HonoRequest<P, I> & this` intersection's property).
