# Plan 29: lazy mapped templates and tsc's deferred type references

Written 2026-09-27 (planning session, from plan 19 R18/R19 and the triage "Category-sized" bullet).

## Status
**Stages 1 and 2 done** (2026-09-27): suite 8579 -> 8599 (+20 tests), type-fest 53 -> 46, valibot 60 -> 54, react 1,
tsc corpus 0, valibot RSS unchanged (544 MB, after keying deferred instantiations by the node's outer type
parameters; without that it was 598). Two components lost, both stage 3's diagnostics:
`recursivelyExpandingUnionNoStackoverflow` types (`M` prints `unknown`, tsc `any` + TS2615) and
`circularTypeArgumentsLocalAndOuterNoCrash1` types. `awaited_type()` got tsc's awaitedTypeStack guard (the cycle
gives the error type; TS1062 is stage 3's). The array/tuple switch to the syntactic rule was neutral, as predicted;
`node_reaches_alias_in_flight()` stays for `lower_branch()` (stage 4) and `prop_judgeable()` (checker.c).
Stages 3 and 4 are next. The prototype patch (`test/probes/plan29/experiment.patch`) can be deleted with stage 3.

Original notes: the investigation prototyped every stage behind `getenv()` toggles:
`test/probes/plan29/experiment.patch` (against 9da8c37; hacky, **don't apply as is**, read it as a map of the change).
Measured with the prototype (stages 1+2+3): suite **+20 tests / +29 components, 2 components lost** (both need the
diagnostics of stage 4), tsc corpus 0, type-fest **53 -> 46**, valibot **60 -> 54**, react 1, tsc corpus callgrind
**+0.06%** (5.909 G -> 5.912 G), valibot peak RSS **541 -> 598 MB** (see Risks).

Verdict: no new architecture is needed. tttt already has tsc's deferred type references for arrays and tuples
(`ty_deferred_reference()`, `TyDeferredRef`, every reader goes through `ty_reference_args()`), an alias instantiation
cache keyed by (alias, args) (`alias_instance()`, stored after completion like tsc's `links.instantiations`), and
tsc's own `getDeclaredTypeOfTypeAlias` also returns the error type when re-entered. The bugs are three places where
tttt is *less lazy* than tsc, plus the diagnostics tsc emits when laziness runs out.

## Why (root causes, reproduced)
- **R18 is not an alias problem, it's an eager mapped type template.** `alias_constraint_cycle.ts`: the typecheck
  walk checks `Pk<keyof M<T> & string>` against Pk's constraint, lowering `M<T>`; `instantiate_mapped_over_list()`
  calls `ty_members()` on M's *generic* mapped type just to read `modifiers_type`; `builder_add_interface_member()`
  (case `INTERFACE_KEY_MAPPED`) lowers the template `P<T[K]>` eagerly -> P's declared type -> P's body ->
  `M<T>` again -> `ty_members()` in flight -> `&empty_members` -> `keyof` = never -> P = `Pk<never>`, not generic.
  tsc's `getTemplateTypeFromMappedType` is lazy (`type.templateType ||= ...`), so M never touches P while M's
  members are being built. (gdb trace in this session; no `ty_declared_type_of()` in-flight hit at all.)
- **R19 is a missing kind of deferred reference.** `type RD<T> = T extends ReadonlyArray<infer I> ?
  ReadonlyArray<RD<I>> : T`: tsc's `isDeferredTypeReferenceNode` defers *generic interface/class references* too
  (`getTypeFromClassOrInterfaceReference`: in an alias body chain (`isResolvedByTypeAlias`) with an argument that
  `mayResolveTypeAlias`, or with defaulted arguments, or when the reference *is* the alias body). tttt only defers
  arrays/tuples, and decides it dynamically (`node_reaches_alias_in_flight()`: "would lowering this reach an alias in
  flight right now") instead of syntactically; conditional branches are deferred by the same dynamic test
  (`lower_branch()`), which also clears `in_alias`, so `ReadonlyArray<RD<I>>` in a branch is lowered eagerly.
- **Alias-body interface references lose the alias name.** `type B<T> = Box<T>` prints `Box<T>` / `Box<string>`;
  tsc's deferred reference carries `aliasSymbol` B. This is where most of the suite gain comes from
  (asyncAliasReturnType*, destructuringParameterDeclaration3*, tsxStatelessFunctionComponentOverload5/6, react SFC
  tests...). Probe `test/probes/plan29/alias_body_ref.ts`.
- **TS2589 lands on the next assignability check** (`report_instantiation_limit()` in check.c turns
  `Checker.instantiation_limit_hit` into an error wherever errors are reported next). tsc reports at `currentNode`
  at the moment of the limit. The same "no diagnostic where tsc has one" holds for the other laziness guards: TS2456
  (alias circularly references itself, `ty_declared_type_of()` TODO), TS4109 (type arguments circularly reference
  themselves, `ty_reference_args()` in flight), TS2615 (mapped property type circular, `prop_type()` in flight gives
  `TY_CIRCULAR`, printed `unknown` where tsc has `any`), TS1062 (awaited type recursion, see stage 2).

## Stages (each leaves the suite runnable; save baselines and commit after each)

### Stage 1: lazy mapped type template (small, ~half a session)
- `MappedInfo.template` becomes private: add `template_node` + `template_ctx` (declared) or `template_from` +
  `template_mapper` (instantiated, set in `instantiate_members()` instead of instantiating), a `LazyState`, and an
  accessor `ty_mapped_template(me, mapped)`; ~19 readers in conditional.c, infer.c, instantiate.c, members.c,
  relate.c, ty_display.c switch to it.
- Re-entry while in flight: **recompute without caching** (don't return an error): tsc has no guard there, the
  instantiation limits end a runaway. Returning TY_ERROR on re-entry lost `recursivelyExpandingUnionNoStackoverflow`
  types; recomputing kept the loss at the TS2615 display issue only.
- Consider the same for `constraint`/`name_type` only if a probe needs it (tsc resolves the constraint eagerly-ish
  via `getConstraintTypeFromMappedType`, so probably not).
- Prototype result alone: `alias_constraint_cycle.ts` and `pick_rec.ts` pass all components, suite +1
  (recursiveConditionalTypes2), type-fest 53 -> 48 (writable-deep 7 -> 2), valibot unchanged.

### Stage 2: deferred generic interface/class references (medium, 1 session)
- `may_resolve_type_alias(node)`: tsc's `mayResolveTypeAlias` over TypeNode (identifier resolving to an alias,
  `typeof`, keyof/readonly operand, union/intersection/indexed/conditional members).
- In `lower()` case `TYPE_IDENTIFIER`, before `lower_identifier()`: for an interface/class with type parameters
  (not a keyword, not `import()`), return `ty_deferred_reference(me, node, ...)` when `alias` is set (the reference is
  the alias body: pass alias + alias_args so it prints `B<string>`), or when `ctx.in_alias` and (argument count !=
  parameter count || some argument `may_resolve_type_alias`).
- `deferred_reference_target()`: `TYPE_IDENTIFIER` -> the declared type. `ty_reference_args()`: identifier nodes
  lower through `lower_type_args()` (defaults included), then instantiate with the deferred mapper.
  `TyDeferredRef.node` comment: array, tuple or identifier.
- `in_alias` must follow tsc's `isResolvedByTypeAlias`: set for the alias body, kept through union, intersection,
  indexed access, conditional (all four parts), keyof/readonly, array, tuple and type arguments; cleared for object
  types, function types, mapped types, template literals, `typeof`. Today it's cleared only for object literals and
  by `lower_branch()`: stop clearing it in `lower_branch()`, clear it for templates.
- Switch array/tuple deferral to the syntactic rule too (`may_resolve_type_alias` on the element(s); empty tuples not
  deferred), then delete `node_reaches_alias_in_flight()` and `_through()` if nothing else needs them
  (`checker.c:699` uses `ty_node_reaches_alias_in_flight()`: check what it guards first). Prototype: neutral on suite
  and corpora, so it's a simplification, not a gain.
- Recursive walkers that used to be stopped by eager error types now see infinite (lazy) structures. Known one:
  `awaited_type()` (call.c) overflowed the stack on `unresolvableSelfReferencingAwaitedUnion`
  (`type SimpleType = string | Promise<SimpleType>`); give it tsc's `awaitedTypeStack` guard (TS1062 where tsc
  reports it). Run `./findcrash.py` after this stage and guard any other walker the same way (a stack of types in
  progress, not a depth counter).
- `instantiate_inner()` turns a deferred reference whose args are already resolved into an ordinary reference (the
  "Resolved by now" shortcut). tsc never does (instantiating a deferred reference makes another deferred one), and
  the shortcut makes laziness depend on who asked first. The prototype kept it and saw no problem
  (`rd_order.ts`); keep it unless a probe shows order dependence, then restrict it to arrays/tuples.

### Stage 3: diagnostics at the right place (small-medium, ~half a session)
- Record where the instantiation count was last reset (`ty_reset_instantiation_count()` callers: statement.c
  `statement_typecheck()`, expr.c `compute()`; give it the node's span) and make `ty_instantiate()` push a pending
  TS2589 for that span (deduplicated) instead of setting a flag; the tail-count limit (`MAX_TAIL_COUNT`), the
  relation overflow (`relate.c`, `st->overflow`) and the cut-short conditional cache replay (`instantiate_conditional`)
  do the same. The statement walk drains pending checker diagnostics into its `ErrorCtx` after each statement.
  Delete `report_instantiation_limit()` and `instantiation_limit_hit`.
- The same pending list carries TS2456 (`ty_declared_type_of()` alias in flight, at the alias name), TS4109
  (`ty_reference_args()` in flight, at the reference node), TS2615 (mapped property type in flight; the property
  type becomes `any`, not `TY_CIRCULAR`). Baselines expecting them: TS2589 9, TS2456 10, TS4109 2, TS2615 2.
  This wins back the two components the prototype lost (`circularTypeArgumentsLocalAndOuterNoCrash1`,
  `recursivelyExpandingUnionNoStackoverflow`). Mind tsc's odd positions (TS4109 there is reported at 5,12, the outer
  function's type parameter: `currentNode`, not the reference).

### Stage 4 (optional, measure first): always-lazy conditional branches
tsc resolves `trueType`/`falseType` on first use, always; tttt only when `node_reaches_alias_in_flight_deep()`. The
prototype (`P29_BRANCH`) gave suite +1 more test but type-fest +2 (writable-deep 2 x TS2345, writable TS2589).
Only do it if those two are understood; otherwise leave `lower_branch()`'s dynamic test and note it in hacks.md.

## What this plan does *not* fix (checked)
- **conditional-pick-deep (6)**: stays after all stages. No in-flight memo and no limit is hit (gdb); the nested
  `ConditionalPickDeep<{string: string}, string>` evaluates to something `ConditionalKeys` drops, and
  `Inner["object"]` prints `Inner` (`test/probes/plan29/cpd_steps.ts`; tsc itself is order-dependent here: it gives
  `sub: any` in that probe). Separate investigation, put it in triage.
- **ts41, async-return-type, invariant-of (TS2589 each)**: `MAX_TAIL_COUNT` reached while resolving type-fest's
  `ArraySlice` for some call expression; reported on the first check of the next file. Stage 3 moves them to the
  real site; the cause (tttt's tail count spanning more than tsc's per-`getConditionalType` loop?) is separate.
- `rd_2589_min.ts` types: `r : RD<J>` where tsc expands the union: a distributed conditional keeps its alias only for
  a *new* alias symbol (tsc's `getConditionalTypeInstantiation` passes `aliasSymbol`, not `type.aliasSymbol`, to
  `mapTypeWithAlias`). That's plan 19's "Also seen" display item (`distributed_union_alias.ts`), not this plan.

## Validation
- Probes: `FAST=1 ./filetest.sh test/probes/plan19/typefest/{alias_constraint_cycle,pick_rec}.ts` pass all
  components; `rd_2589_min.ts` and `test/probes/plan29/rd_order.ts` have no TS2589 (errors pass; types wait for the
  display item above); `test/probes/plan29/alias_body_ref.ts` prints `B<T>`, `BS`, `B<string>` (the `Map` lines
  differ for another reason: no es2015 lib).
- `./fasttest.sh --summary`: expect roughly +20 tests / +30 components after stage 2, no losses after stage 3.
- `./realworld.py`: 0. `--corpus type-fest`: 53 -> ~46 (writable-deep 7 -> 1, readonly-deep's TS2589 gone; the
  DelimiterCase family (camel-case, kebab-case, snake-case, delimiter-case...) must not reappear; conditional-pick-
  deep stays 6). `--corpus valibot` 60 -> ~54, `--corpus react` 1.
- Perf: `valgrind --tool=callgrind ./release/tttt -q --keep-going -p thirdparty/tsc/src/compiler` within 0.5% of
  before; `./bench.sh` for valibot time and RSS (see Risks). Record in `measurements.md`.
- `./findcrash.py` clean after stage 2.

## Risks
- **Memory on generic-heavy code**: valibot +10% RSS in the prototype. `ty_deferred_reference()` keys instances by
  (node, whole mapper) and allocates a `Ty` + `TyObjectState` + `TyDeferredRef` each. tsc keys deferred
  instantiations by the outer type parameters the node mentions (`getObjectTypeInstantiation`); do the same as
  `instantiate_anonymous()` already does for object types (restrict the mapper to what the node mentions), then
  remeasure. If RSS stays up, find out whether it's the deferral or simply more checking now that 6 errors are gone.
- Walkers without tsc's circularity guards (stage 2) can overflow the stack instead of failing a test: run the full
  release suite and `./findcrash.py`, not just `--summary` on a subset.
- Deferred references are distinct `Ty`s from the eager `ty_reference()` of the same arguments: identity checks
  (`source == target` fast paths, union dedupe, `ty_is_array()` style target checks) keep working through
  `reference.target`, but anything comparing reference pointers for equality will now see two types. The prototype
  showed no fallout; watch `displaydiff.py` for union-order changes.
- Prototype toggles and hacks (static depth counter in `awaited_type()`, `getenv`) must not survive into the real
  implementation.
