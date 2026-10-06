# Plan 34: error text fidelity (the "right codes, wrong text" bucket)

Written 2026-09-27 by the insights session; stages 1-2 executed the same day (todo entry 3). What's left is a queue
for later "error text" sessions, not a blocker for anything.

## Status
`./errtextdiff.py` (stage 1, after `./failstats.py`) buckets the tests whose errors.txt differs with the same codes at
the same positions by the first differing part of each diagnostic: `header:<code> <mechanism>`, `chain:<tsc line> /
<our line>` (or `missing:`/`extra:`/`deeper:`/`shallower:`), `related:<what> <code>`, `underline:<code> <how>`;
`--coarse` for the parts only, "tests fixed by one kind" ranks them. Type-printing heuristics are shared with
`displaydiff.py` (`displaykinds.py`).

Stage 2 took the bucket from 432 to **302** tests (suite 8958 -> 9090), all by rules, no message special cases:
tsc's incompatible stack (`reportIncompatibleStack`: "The types of 'a.b' are incompatible", "The types returned by
'f(...)'", signature return types 2202-2205), the primitive/wrapper elaboration, no elaboration from a type
parameter's constraint to another type parameter, invariant type arguments explained structurally (the variance
fallback), optional-vs-required after the property types, "has no call/construct signatures" about the apparent
type, TS2719 (two types with the same name), TS2720's suffix, TS2694 fully qualified names, `typeof import("m")` for
namespace imports in messages, property names as written (`["a"]`, `2.0`, `'1'`), related-info spans (parameter
properties, suggestions, type-only imports), and the harness dropping the comments before the first `@filename`.

## Left (2026-09-29, after suite checker bugs 7: 45 tests, from 236)
Suite checker bugs 7 did the non-augmenting subtypes (`getSingleBaseForNonAugmentingSubtype`), almost all
underlines (parser and checker codes), most related info (TS2771, TS1007, TS2773, TS2793, TS2728, TS2208, TS6203,
TS6210, TS6500/TS6502) and ~50 chain tests. Tests fixed by one kind now: `header:TS2322 other` 7 (look one by one),
`outside diagnostics` 6 (lib-file diagnostics under `skipDefaultLibCheck: false`, the harness's pre/post-emit
counts: see triage), chain text 5, TS6500/TS1376/TS1377 related positions, and singles. Leftovers each agent wrote
up are in `triage.md`.
- Elaboration chain (2026-09-29 session C: its 69-test queue is down to ~20). Left there, each needing more than a
  message rule: `The intersection 'A & B' was reduced to 'never'` (tsc's elaborateNeverIntersection needs the reduced
  intersection, tttt makes plain never at construction: intersectionReduction(Strict), intersectionWithConflictingPrivates,
  typeGuardsWithInstanceOf, mixinPrivateAndProtected); a generic source explained through its constraint before an
  intersection target's members (conditionalTypes1: `T` to `NonNullable<T>`); keyof targets whose constraint is an
  intersection (keyofAndIndexedAccessErrors); subclassThisTypeAssignable01 (a relation difference, not text);
  jsxChildrenWrongType (an optional tuple element's missing type prints as `undefined` under exactOptionalPropertyTypes);
  bigintWithLib (tsc repeats one line three times: a relation through the Symbol.toStringTag overloads?).

## Left (2026-10-06, agent D: 20 tests)
`./errtextdiff.py` after `./failstats.py`: 21 tests, one fixed (destructuringUnspreadableIntoRest: getRestType makes
every property's literal in order, the creation order the Omit keys' union goes by). What remains is relation-path or
identity work, no message rules:
- Elaboration paths that take another route through the relation (chain:text/shallower/deeper): indexedAccessRelation,
  keyofAndIndexedAccessErrors (tsc explains `keyof T` through its constraint before splitting the intersection
  target), mappedTypes6 and mappedTypeRelationships (`'P' could be instantiated with a different subtype of constraint
  'string | number | symbol'`: tsc names the constraint's resolved base, tttt the declared `keyof T`; `T[P]` to `{}`
  through NonNullable), complicatedIndexedAccessKeyofReliesOnKeyofNeverUpperBound, conditionalTypes1,
  recursiveConditionalTypes (tsc stops at the deferred conditionals), bigintWithLib (tsc repeats one line three
  times), jsxExcessPropsAndAssignability (tsc adds the `ComposedComponentProps["myProp"] & string` step).
- invariantGenericErrorElaboration: tsc relates `Constraint<Runtype<any>>` to `Constraint<Num>` by its type arguments
  (variance) and reports `Runtype<any>` to `Num` directly; tttt falls back to the structure (`underlying`). Compare the
  measured variance of Constraint's A (`TTTT_DUMP_VARIANCES=1`) with tsc's first.
- tsxUnionElementType3/4 (related:text): tsc's `P & { children?: ReactNode }` in React.Component is instantiated per
  class instantiation because P has two declarations (interface + class Component<P, S>): isTypeParameterPossiblyReferenced
  says yes for a type parameter whose symbol has more than one declaration, so `{ children?: ReactNode }` appears once
  per union member in the intersection. tttt's free-parameter collection (collect_free_params) only counts mentions, and
  a type node doesn't know its outer type parameters; the lib's many-times-declared `Array<T>` makes this a perf
  question too.
- tsxStatelessFunctionComponentsWithTypeArguments4 (`{ b: number; a: unknown }` vs tttt's `b: unknown`: inference
  for the overload in the message), genericCallWithGenericSignatureArguments3 (tsc keeps `(n: Object) => 1` as an
  inferred return: the inner arrow's literal isn't widened under the outer one's type-parameter return context),
  propertyAssignmentUseParentType2 (a JS expando function annotated through its variable: tsc's expression type is
  the merged symbol's, `{ (): boolean; nuo: 1000 }`, tttt prints the arrow's own `(): true`).
- `outside diagnostics` (3) and `count` (1): see the earlier note (lib diagnostics, emit counts).

## Validation
Each fix: `./fasttest.sh -s` with no lost components (`./resultdiff.py --lost`), the corpora unchanged (a message
change can crash, see the `ty_prop_written_name` mapped-property bug the suite didn't catch but type-fest did), and
the bucket count from `./failstats.py` into `measurements.md`.
