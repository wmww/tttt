# Plan 13: member modifiers (accessibility, readonly writes, private names)

## Why
`private`/`protected`/`readonly`/`#x` are parsed and then mostly ignored by the checker. That's wrong code accepted
on every class-heavy program, and a clean, well-delimited category in the suite (2026-09-22, `./failstats.py`):
missing TS2540 154 in 39 tests, TS2341 117 in 36, TS2445 106 in 22, TS2729 70 in 31 (use before init, related),
TS2446 31 in 5, TS18013 30 in 18, TS2542 26 in 11, TS18028 18 in 6, TS2673/TS2674 14 in 8, plus the
"Types have separate declarations of a private property" relation rule (nominal privates) and intersection reduction
with conflicting privates (intersectionWithConflictingPrivates). The corpus has none of these errors (it's clean
under tsc), so this is suite work, but it also removes a class of false "assignable" answers from relations.
Probe: `test/probes/triage/member_modifiers.ts` (every rule above in 13 lines, tttt reports one wrong TS2322).

## What exists
- `Prop.flags` has `PROP_READONLY` (set for readonly fields/signatures, getter-only accessors, const contexts,
  mapped `readonly` modifiers: `members.c`), `Prop.parent` is the declaring symbol. The parser keeps
  `ACCESS_MODIFIER_PRIVATE/PROTECTED` on class items (`class_decl.h`) and `modifier_list.h` on the rest.
- Property access checking is `check_access()` in `ty/check.c`; assignments to property targets are checked in
  `ty/expr.c` (find where a property access is an assignment target: `x.a = `, `x.a++`, `x.a += `, destructuring
  targets, `for (x.a of ...)`).
- `#x` is a plain name (atom `#x`): two classes' `#x` are the same property. `class_heritage_typecheck` skips
  classes that redeclare a base's `#x` (HACK in `hacks.md`).

## Design
1. **Flags**: `PROP_PRIVATE`, `PROP_PROTECTED` on `Prop` (from class members incl. parameter properties and
   accessors); keep through instantiation, mapped types drop them like tsc (homomorphic mapped types keep only
   optional/readonly), intersections/unions combine like tsc's `getCheckFlags` (a union property is private if any
   constituent is: check tsc's createUnionOrIntersectionProperty).
2. **Access check** (tsc `checkPropertyAccessibility`): given the declaring class of the prop and the enclosing class
   of the access site (walk up the AST from the access; the binder knows containing classes), report TS2341/TS2445/
   TS2446 (protected through an instance of a non-derived class)/TS2673/TS2674 (`new` on a private/protected
   constructor; result is still the instance type in tsc, check what `typesWithPrivateConstructor` wants). `super.x`
   rules (TS2340) only if cheap. `this` inside the class body and static members follow the same rule.
3. **Readonly writes** (tsc `checkReferenceExpression`/`isAssignmentToReadonlyEntity`): TS2540 for a readonly prop as
   an assignment target, except in the declaring class's constructor for its own `this.x` (and property initializers);
   TS2542 for a readonly index signature (`readonly number[]`, `ReadonlyArray`); TS2588 for `const` already exists?
   (check). Namespace-imported/const enum members and `import * as ns` exports are readonly too (TS2540 on
   `ns.x = 1`): see what the suite asks for.
4. **Private names**: give `#x` a per-class identity (tsc's escaped name `__#<classid>@#x`): the binder can mangle
   the atom with the declaring class's symbol id so lookup from another class simply fails, then TS18013 when a
   same-spelled `#x` exists on the type (tsc's lookup for a better message), TS18028 below ES2015 (target-gated,
   `compiler_options()->target`). Remove the `class_heritage_typecheck` HACK.
5. **Relations**: a private/protected target property is only satisfied by the *same declaration* (tsc
   `propertiesRelatedTo`: "Types have separate declarations of a private property", "Property 'x' is private in type
   'A' but not in type 'B'", TS2415 elaborations in class heritage). Intersection reduction to `never` for conflicting
   privates (lazily, like the existing `has_conflicting_discriminants`) and its TS2339 message if cheap.

Stages (each keeps the suite runnable): flags + access check (1, 2); readonly (3); private names (4); relations (5).
Order by payoff: 3 and 2 are the bulk. Expect 2 sessions; if the "enclosing class" query or private name mangling
turns out to need binder surgery, stop and write that up.

## Risks
- Relations: nominal privates make some currently-passing assignments fail. That's correct, but check spurious TS2322s
  with `./resultdiff.py --lost` and the corpus (`./realworld.py` must stay at its count: tsc's own code uses private
  members heavily in `transformers/`).
- Readonly flags already drive narrowing (`flow.c` constant references): don't change what sets them without
  rerunning the corpus.
- `readonly` on inferred members: `readonly z = 3` has type `3` (literal kept, the probe shows tsc reports TS2540 there,
  we report TS2322 `4` to `3`): the readonly check must come before the assignability check for the same target.

## Validation
- `test/probes/triage/member_modifiers.ts` passes (`FAST=1 ./filetest.sh ... -d`).
- `./failstats.py`: the codes above mostly gone from "missing" (list the leftovers in `triage.md`), none new in
  "spurious"; `./fasttest.sh --summary` no unexplained losses; `./realworld.py` unchanged.
- Named tests: typesWithPrivateConstructor, typesWithProtectedConstructor, intersectionWithConflictingPrivates,
  privateNamesUnique*, readonlyMembers, protectedClassPropertyAccessibleWithinSubclass*, privateInstanceMemberAccessibility.
