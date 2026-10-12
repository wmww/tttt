# Plan 61: constraint functions the way tsc has them (getConstraintOfType + getBaseConstraintOfType)

Written 2026-10-12 by fan-out agent B (planning only, nothing landed). Replaces hacks.md's "Constraint functions are
approximations" entry once done. Derisked with a two-line experiment (below): the core of it is small; the work is
in migrating ~60 callers onto two well-defined functions and deleting the ad-hoc guards around them.

## Why
- Real code diverges through it. mikro-orm's tests (+TS2345 x13, +TS2322 x3: `raw()` inside `em.find(E, { books:
  { $in: raw(qb) } })`) reduce to this (tsc: `r()` is `"dflt"` and TS2322 at `$in`; tttt: `readonly {}[] | undefined`,
  no error):
  ```ts
  type OperatorMap<T> = { $in?: readonly T[] };
  declare function r<R = "dflt">(): R;
  type EP<T> = T extends (infer U)[] ? NonNullable<U> : NonNullable<T>;
  type EQ<T> = T extends object ? OperatorMap<T> : number;
  type MU<T> = [T] extends [object] ? T extends string ? T : { [K in keyof T]: T[K] } : T;
  declare function g2<E extends object>(w: { b?: EQ<EP<MU<E>["books"]>> }): void;
  g2({ b: { $in: r() } });
  ```
  tsc's contextual type for `$in` is `readonly {}[] | readonly NonNullable<E["books"]>[]` (an instrumented tsgo
  printed it): the base constraint keeps an object type that still mentions E, so the outer call's silent-never
  mapper makes it non-inferrable. tttt's `base_constraint()` resolves the distributive constraint from the check type's
  *base* constraint (`{}`), losing E.
- The same approximation is behind several HACKs (hacks.md: `conditional_source_related` base-then-immediate, the
  depth-8 cutoff, `primitive_constraint_of`, `expand_mapped`'s unknown `T[string]`) and caller-side `depth > 8` caps.

## How tsgo does it (internal/checker/checker.go)
- `getConstraintOfType(t)` (17434) is the *immediate* constraint, one step: a type parameter's declared constraint
  (`getConstraintOfTypeParameter`, guarded by `hasNonCircularBaseConstraint`), an indexed access's
  `getConstraintFromIndexedAccess` (index constraint first, then object constraint, via `getSimplifiedTypeOrConstraint`),
  a conditional's `getConstraintFromConditionalType` (distributive constraint, else the default one); anything else
  falls back to `getBaseConstraintOfType`.
- `getConstraintOfDistributiveConditionalType` (17671): check type simplified, else `getConstraintOfType(checkType)`
  (immediate!), instantiate the root with it (forConstraint), keep unless never; cached per type
  (`resolvedConstraintOfDistributive`, noConstraintType sentinel). `getDefaultConstraintOfConditionalType`: inferred
  true branch | false branch, an `any` branch elided; cached.
- `getBaseConstraintOfType(t)` (27970) = `getResolvedBaseConstraint(t, nil)` minus the sentinels.
  `getResolvedBaseConstraint(t, stack)` (27981):
  - cached per type in `resolvedBaseConstraint` (also when the answer is `noConstraintType` or `circularConstraintType`);
  - `pushTypeResolution(t, ResolvedBaseConstraint)`: if t is already on the resolution stack (with no entry in between
    that has a type already), every entry from it to the top gets result=false and the push fails -> circular;
  - a recursion-identity stack: always explore 10 levels, up to 50 while the identity isn't on it yet;
  - `computeBaseConstraint(getSimplifiedType(t))`, each kind recursing through `getNextBaseConstraint` (=
    getResolvedBaseConstraint with the stack, sentinels -> nil); conditionals through `getConstraintFromConditionalType`
    with a `conditionalConstraintDepth` cap of 100;
  - pop: a failed resolution is circular (TS2313 for a type parameter, at its constraint declaration).
- `getApparentType` = base constraint or unknown; circular -> unknown.

## What tttt has (relate.c unless noted)
- `base_constraint(ty, depth)` / `base_constraint_uncached`: tsc's computeBaseConstraint shape, but cut off at
  `depth > 8` instead of a circularity stack; memoized (KEY_BASE_CONSTRAINT) at depth 0 only, with an in-flight
  sentinel whose re-entry *recomputes from depth 0*. `composite_base_constraint` likewise.
- `distributive_constraint_as(ty, depth, immediate)`: the base flavour (check type's base constraint, KEY_BASE_DISTRIBUTIVE,
  uncached below depth 0) used by `base_constraint()`; the immediate one (KEY_IMMEDIATE_DISTRIBUTIVE, through
  `simplified_or_constraint`) only by `conditional_source_related()`, after the base one (HACK).
- Immediate-ish helpers that mix both: `simplified_or_constraint` (base for conditional/keyof/template/mapping/
  substitution/tuples), `constraint_of_indexed`, `immediate_constraint` (base in its default case),
  `effective_intersection_constraint` (loops immediate up to 32), `ty_conditional_constraint`, `write_constraint_of_indexed`.
- Separate guards: `apparent_stack[32]` in `apparent_type_inner` (circular apparent -> unknown), `depth < 16` param
  chains in `apparent_type_resolve`, `apparent_type_of_mapped_at` depth > 8, and caller-side `depth > 8` caps in
  expr.c `instantiation_scan_part`, ty/expr.c `context_is_tuple_like` and `apply_type_arguments_part`, check.c
  `is_valid_spread_type`. `for_constraint_root` around the distributive instantiation. Circular type parameter
  constraints: `ty_param_constraint_is_circular` (generic.c TS2313) and members.c's RESOLVING_CONSTRAINT walk.
- A generic resolution stack exists already: `my resolving` with `ty_resolving_push/find/cycle` and kinds
  (checker.c `ty_resolving_cycle`, ty.h RESOLVING_*). That is tsc's pushTypeResolution; stage 1 reuses it.

### Callers (96 call sites, inventory 2026-10-12)
- 39 inside the machinery itself (relate.c 4124-4474: wrappers, memo, the per-kind recursion).
- Relation/assignability 13 (`report_relation_error_now`, `could_access_optional_property`, `write_constraint_of_indexed`
  x2, `ty_conditional_constraint`, `intersection_constraint`, `conditional_source_related` x2, `hoisted_constraints`,
  `structured_related_members` x4).
- Mapped/indexed simplification 6 (`ty_generic_mapped_prop_context`, `simplified_or_constraint`,
  `constraint_of_indexed` x2, keyof and mapped-index cases of `structured_related_members`).
- Apparent type/property access 9 (`apparent_type_of_mapped_at`, `apparent_type_resolve` x2, `has_inferable_index`,
  `immediate_constraint`, iterate.c `call_signatures`, call.c `awaited_type_needed`/`promised_type_of`/`is_thenable`).
- Narrowing/flow/facts 13 (flow.c x7: `type_facts` x2, `non_nullable_constraint_or`, `ty_is_type_derived_from`,
  `narrow_generic_related`, `is_unit_like_type`, `map_base_constraint`; checker.c `ty_nullable_facts`; ty/expr.c
  `is_generic_with_union_constraint`, `base_constraint_or_type`, `ty_substitute_constraints`; expr.c TS18048 key).
- Inference 5 (infer.c `infer_to_tuple`, `infer_to_template`; template.c x3), contextual typing 4 (ty/expr.c
  `context_is_tuple_like`, `is_literal_of_contextual_type`, `is_template_contextual`; expr.c `maybe_kind`).
- Other 15: checker.c `primitive_constraint_of` (HACK, perf) and `remove_constrained_type_variables` (dereferences the
  result without a null check: relies on TYX_CONSTRAINED_TYPE_VARIABLE), ty/expr.c union subtype reduction and
  `apply_type_arguments_part`, check.c spread/`has_empty_object_intersection`, call.c spread args x2, lower.c x3
  (`getNonUndefinedType`, binding rest elements), expr.c `instantiation_scan_part`, relate.c `could_have_singletons`.
- Display has no direct call: `display_mapped` -> `expand_generic_homomorphic` -> `ty_apparent_type` (hacks.md says it
  relies on the cutoff: `Compute<A[K]>`, computedTypesKeyofNoIndexSignatureType).
Nearly every caller outside relate.c wants getBaseConstraintOfType (or getBaseConstraintOrType); the immediate users
are the relation (conditional/indexed sources and targets) and getSimplifiedTypeOrConstraint's users.

## The runaway, found
Switching `base_constraint_uncached`'s conditional case to the immediate distributive constraint
(`distributive_constraint_as(me, ty, depth, true)`) crashes the suite in exactly one test, infiniteConstraints
(stack overflow; every other test runs, distributiveConditionalBaseConstraint newly passes). The cycle (gdb):
```
base_constraint(conditional, 0) -> distributive_constraint_as(immediate) -> simplified_or_constraint(check: indexed)
  -> ty_simplified -> compute_simplified_conditional -> ty_intersection -> reduce_type_variable_intersection
  -> primitive_constraint_of (checker.c) -> ty_base_constraint(..)      <- a fresh depth-0 call
  -> base_constraint -> ... -> composite_base_constraint -> base_constraint(conditional, 0) again
```
The depth-8 cutoff only bounds one call chain's own `depth` argument; any caller that goes out and comes back in
through `ty_base_constraint()` starts at 0 again, and the memo's in-flight sentinel *recomputes* on re-entry instead
of answering "circular". With the base flavour the cycle happened not to close (the base check-type constraint is
already concrete, so no intersection reduction asks again).

## The experiment (not committed)
```diff
-            Ty const* distributive = distributive_constraint(me, ty, depth);
+            Ty const* distributive = distributive_constraint_as(me, ty, depth, true);
...
-    if (cached) return cached == ty ? nullptr : cached == &base_constraint_in_flight ? base_constraint_uncached(me, ty, 0) : cached;
+    if (cached) return cached == ty ? nullptr : cached == &base_constraint_in_flight ? nullptr : cached;
```
(re-entry while in flight = circular = no constraint, tsc's circularConstraintType). Results at ai-main 4f5b6475:
suite 13318 -> 13319 (distributiveConditionalBaseConstraint), 0 lost; infiniteConstraints passes; every corpus
identical (0 spurious except effect's 3, missing webpack 5, drizzle 3, vue 3); instructions flat except react
1.501 -> 1.509 G (+0.5%); the probe above matches tsc's type of `r()` (its TS2322 still missing: see stage 3). The
mikro-orm project itself doesn't move yet: its `$in` contextual type still has the extra `readonly unknown[] |
readonly ExpandScalar<{}>[]` members, from other constraint paths (contextual apparent type: `ty_apparent_type` /
`ty_generic_mapped_prop_context` on the mapped `FilterObject<E>`), which stage 3 covers.
Caveat: the in-flight answer is cached at the top, so the result can depend on which caller came first (tsc has the
same property, but marks every entry of the cycle circular, which the experiment doesn't).

## Stages
1. **Circularity instead of the cutoff** (relate.c only, behaviour-preserving as far as possible).
   - `base_constraint()` pushes `RESOLVING_BASE_CONSTRAINT` on `my resolving` (ty.h kind) for every type it computes,
     at every depth; re-entry -> `ty_resolving_cycle` (marks the cycle) and returns the circular sentinel; on pop a
     circular entry caches the sentinel (KEY_BASE_CONSTRAINT holds three states: constraint, none, circular).
   - Replace `depth > 8` with tsc's recursion-identity stack (10 levels always, up to 50 while the identity isn't on
     the stack; reuse `ty_recursion_identity`/the deep-nesting helpers relate.c has for `is_deeply_nested`), plus
     `conditionalConstraintDepth` (100) for conditionals. Cache at every depth (the depth-0-only rule existed only
     because the cutoff made answers depth-dependent).
   - Make `ty_param_constraint_is_circular` and members.c's RESOLVING_CONSTRAINT walk use the same answer (tsc's
     `hasNonCircularBaseConstraint`), so TS2313 comes from one place.
   - Validate: suite 0 lost (infiniteConstraints, circularConstraint*, recursiveConditional*, the
     `Compute<A[K]>`/computedTypesKeyofNoIndexSignatureType display tests), corpora identical, instrcount
     (react/type-fest/effect are the constraint-heavy ones), `scripts/findcrash.py` clean, threadcheck (the caches are
     per checker; results must not depend on check order across files more than tsc's do).
2. **Immediate distributive constraint everywhere tsc has it.**
   - `base_constraint_uncached`'s conditional case uses `distributive_constraint_as(.., true)` (the experiment's first
     line); delete the base flavour (KEY_BASE_DISTRIBUTIVE) once nothing uses it.
   - `conditional_source_related()`: immediate only (drop the base-first HACK). hacks.md named 6 suite tests that
     immediate-alone lost back then (keyofAndIndexedAccess, styledComponentsInstantiaionLimitNotReached,
     substituteReturnTypeSatisfiesConstraint, instantiateContextualTypes, ...) and react-hook-form TS2589: recheck
     them after stage 1; each one left is a probe for stage 3, not a reason to keep the HACK.
   - Validate: the probe above (filetest), distributiveConditionalBaseConstraint, those 6 tests, react-hook-form in
     the source sweep (`-f '^react-hook-form/'`), corpora, instrcount.
3. **One `ty_constraint_of_type` (getConstraintOfType) and one `ty_base_constraint` (getBaseConstraintOfType).**
   - `ty_constraint_of_type`: type param -> declared constraint if not circular; indexed -> getConstraintFromIndexedAccess
     (`constraint_of_indexed` as it is, with getSimplifiedTypeOrConstraint = simplified else ty_constraint_of_type);
     conditional -> distributive (immediate) else default; else ty_base_constraint. Replaces `simplified_or_constraint`'s
     base fallbacks, `immediate_constraint`, `ty_conditional_constraint`, `effective_intersection_constraint`'s loop.
   - Migrate the relation's immediate users (13 + 6 above) first, then audit the 41 outside relate.c: almost all are
     getBaseConstraintOfType already; `primitive_constraint_of`'s perf HACK and `remove_constrained_type_variables`'s
     unchecked dereference need a look once circular answers exist.
   - `ty_apparent_type`: base constraint, circular -> unknown, then drop `apparent_stack`/`apparent_depth` and the
     `depth < 16` chain loop. Then the contextual-type paths (`ty_generic_mapped_prop_context`,
     getApparentTypeOfContextualType's mapType of apparent types) for the mikro-orm `$in` case.
   - Delete the caller-side `depth > 8` caps one at a time (each with a suite + corpora run).
   - Validate: mikro-orm/tests (`scripts/sourcesweep.py -f '^mikro-orm/tests$'`: the 13 TS2345 + GHx6's 3 TS2322 should
     go), the probe's TS2322, plus the usual oracles. hacks.md loses the "Constraint functions are approximations"
     entry and the `expand_mapped` unknown-`T[string]` one if it goes with it.

## Validation, every stage
`scripts/fasttest.sh --summary` (0 lost), `scripts/realworld.py` on every corpus (identical), `scripts/instrcount.sh`
on react, type-fest, effect, valibot, zod (more than +0.3% needs a reason: tsc does more caching here, so this should
be flat or better once the cutoff's re-computation is gone), then `scripts/sweep.py`, `scripts/expecttypes.py`,
`scripts/sourcesweep.py --sample 4` against a base run, `scripts/findcrash.py` and `scripts/threadcheck.sh`.

## Risks
- Order dependence: a circular answer cached at the top of a cycle is what the first caller saw. tsc has the same
  property; the cure is marking the whole cycle (stage 1), not avoiding the cache.
- Perf: the cutoff accidentally capped some expensive walks (react-hook-form's `T[a][b]...` chains, valibot/react's
  `primitive_constraint_of` HACKs). Measure per stage; the identity stack is tsc's own bound.
- Display relies on the cutoff today (hacks.md: `display_mapped` -> `expand_generic_homomorphic` -> `ty_apparent_type`);
  keep those tests in the stage 1 check list.
