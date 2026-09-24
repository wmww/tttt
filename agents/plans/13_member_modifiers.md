# Plan 13: member modifiers (accessibility, readonly writes, private names)

## Status
Stages 1-4 done (2026-09-23, see `documentation.md` "Member modifiers" and "Private names"). Left: stage 5 (nominal
relations), then the leftovers below. Probe: `test/probes/triage/member_modifiers.ts`: what still differs from tsc
there is exactly stage 5 (the TS2322 elaboration "Types have separate declarations of a private property").

## What exists now
- `Prop.flags`: `PROP_PRIVATE`/`PROP_PROTECTED` (an accessor's are the getter's, `PROP_SET_*` the setter's for
  writes), `PROP_STATIC`, `PROP_ACCESSOR`, `PROP_FIELD`. Mapped types and spreads drop them (they copy only
  optional/readonly). `Prop.parent` is the declaring class; the root declaration is `prop->target` followed to the end.
- Resolve walk: `PropertyExpr.enclosing_class` + `ClassDecl.outer_class` (the class chain around an access),
  `in_constructor_of`, `this_parameter`/`this_function`; `CallExpr.enclosing_class` for `new`.
- `ty_check_member_modifiers()`/`ty_is_readonly_assignment()`/`ty_constructor_accessible()` in `ty/check.c`,
  `ty_union_non_public_conflict()` in `ty/access.c` (a union whose members disagree on a private/protected property
  has no such property: TS2339).
- `super` is typed for `super.x`/`super[x]` (`super_type()` in ty/expr.c): base instance with `this` as
  this-argument, base constructor in static members. Where tsc says `super` is illegal (computed names, member
  decorators, constructor parameters, `super()` outside a constructor: `Scope.no_super`) it stays `any`.

## Stage 4 leftovers
Private names in object literals, interfaces, parameters and enums don't parse (tsc parses them and reports TS18016 /
TS1451: `./failstats.py --syntax -f '^privateName.*'`). TS18015 in relations (reportUnmatchedProperty's private-name
case, needs stage 5's place), TS2806, TS2804/TS18010/TS2300 duplicates (privateNameDuplicateField and friends).

## Stage 5: relations
A private/protected target property is only satisfied by the *same declaration* (tsc `propertiesRelatedTo`: "Types
have separate declarations of a private property", "Property 'x' is private in type 'A' but not in type 'B'", TS2415
elaborations in class heritage). Intersection reduction to `never` for conflicting privates (lazily, like the existing
`has_conflicting_discriminants`) and its TS2339 message: mixinAccessModifiers, mixinPrivateAndProtected,
intersectionWithConflictingPrivates are the tests (they report spurious TS2341/TS2445 now, on members tsc reduces
away). Use the root-declaration identity `ty_union_non_public_conflict()` already uses.
Risk: nominal privates make some currently-passing assignments fail. Check `./resultdiff.py --lost` and the corpora
(tsc's own `transformers/` uses private members heavily).

## Leftovers (not in any stage, small)
- Destructuring: `const { priv } = a` and `({ priv } = a)` check accessibility in tsc (checkPropertyAccessibility from
  binding elements and object literal destructuring): privateProtectedMembersAreNotAccessibleDestructuring,
  destructuringAssignment_private.
- JSDoc `@private`/`@protected`/`@readonly` in JS files (jsdocAccessibilityTags, jsdocReadonly).
- TS2704 (`delete` of a readonly property), TS2729 (use before initialization: its own mini plan, 70 in 31 tests).
- `this.x` in a constructor of a class merged with an interface that declares `readonly x` should error (tsc checks
  that the declaration's parent is the class); we exempt any property whose parent symbol is the class.
- `globalThis.globalThis = x` is TS2540 in tsc; `typeof globalThis` is skipped entirely.
