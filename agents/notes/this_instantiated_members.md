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
- 2026-10-24: note written, stage 1 started.
- 2026-10-24, stage 1 landed, much smaller than planned: `valid`'s type is a function anonymous (Ty.anonymous.function,
  the first overload), not an overloads type. instantiate_anonymous() narrows its free parameters with
  ty_function_free_params(), which looked at that one declaration only. It now gives up (unknown) when the overload's
  implementation has a body and no return type annotation (tsc's isTypeParameterPossiblyReferenced over all
  declarations). The declared type's `valid` is then an instance with `this ->` the type, same_signature_origin()
  pairs it with `HonoRequest<{}>`'s instance erased, and the test passes.
  - The annotated-implementation probe keeps tsgo's TS2345, now test/cases/ttttThisInstantiatedAnnotatedImplementation.ts.
  - hono's build and spec configs are byte-identical to tsgo.
  - Not needed after all: the Prop.thisless change (isThisless's single declaration) and the overloads-type change in
    ty_anonymous_from_overloads(); both were tried and dropped.
  - **Stage 2 is not needed.** relate.c already relates a declared type with a `this` type to references of itself
    through variances (`source_self`/`target_self`, "a class or interface with a `this` type is a reference to
    itself"; corB checked it, variancetrace agrees with tsgo's [3]). The "Variances" paragraph above was wrong about
    tttt.
  - Gates:
    - quickgate --vs-main -k: suite +2/-0, realworld 0/0, threadcheck, checkerdiff.
    - instrcount: drizzle +0.00%, zod -0.02%, vscode +0.00%, excalidraw +0.01%, nest -0.02%.
    - $ExpectType: 68640/68640.
    - DT sweep: 8595/8595.
    - Source sweep --sample 4: 1663/1671. All eight mismatches are byte-identical to main's build (openclaw times
      out).
- Next: ty_display.c's method_this_instantiated() may now be redundant (the instances say it), a cleanup to try
  separately.
