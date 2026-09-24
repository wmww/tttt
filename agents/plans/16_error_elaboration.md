# Plan 16: related information, then error elaboration (tsc's elaborateError)

## Why
Two gaps, one depending on the other.

**tttt emits no related information at all.** `Error.sub_errors` with a valid span already render as
`!!! related TSxxxx file:line:col: message` (`format_error_tree`, `src/test/baseline_test_case.c`), but nothing in the
checker creates one. 956 baselines have related lines; **896 failing tests expect some, and 117 of them would pass
errors if we emitted them** (their errors.txt equals ours with the related lines stripped: 51 need only TS2728, 20
TS2208, 16 TS6210, 8 TS6203, 5 TS2793, ...). By tests affected among the failing ones: TS2728 "'x' is declared here"
376, TS6203 "'x' was also declared here" 110, TS2208 "This type parameter might need an `extends X` constraint" 79,
TS6210 "An argument for 'x' was not provided" 62, TS6500 61, TS2750 28, TS1007 25, TS6502 24, TS2793 17, TS6501 15,
TS6212/6213 (did you mean to call / use new) 14. `failstats.py` can't see any of this (it compares summary lines only:
these tests sit in "same codes+positions, message text differs").

**Error elaboration.** tsc doesn't report a failed assignment of an object/array literal or an arrow function as one
error on the whole expression: `checkTypeRelatedToAndOptionallyElaborate` first calls `elaborateError(expr, source,
target)`, which drills into the expression and reports at the innermost property value / element / returned expression
that doesn't fit, with a related TS6500 "The expected type comes from property 'p' which is declared here on type 'T'"
(TS6501 index signature, TS6502 return type of a signature). Probe (`./tscbaseline.sh`):

```ts
const g: () => string = () => 1;   // tsc: (2,31) TS2322 'number' -> 'string' + related TS6502 at `() => string`
let x: { a: string } = { a: 1 };   // tsc: (3,26) TS2322 'number' -> 'string' + related TS6500 a.ts:3:10
h({ a: 1 });                       // tsc: (5,5) TS2322 (not TS2345!) + TS6500 at h's parameter type's `a`
x = f;                             // f a function whose return fits: TS2322 at `f` + related TS6212 "Did you mean to call"
```
tttt reports each at the declaration / argument with a "Types of property 'a' are incompatible" chain. Measured
(`test/probes/plan16/measure.py` after `./failstats.py`, pairing each spurious assignability error of tttt with a missing one of tsc
at a later position of the same statement): **109 failing tests have an errors diff fully explained by relocation**
(85 with nothing left over), 81 more partly. That's an upper bound for the errors component; message text and related
info decide the rest. Nested excess property checks (plan 12: `contextualTyping12`, `objectLiteralExcessProperties`
line 13/33) fall out: tsc relates the nested literal on its own, so the nested TS2353 is reported alone at the property.

Neither changes symbols/types baselines. Corpus counts can rise a little (tsc reports *every* failing property of a
literal, not one error per assignment); positions don't matter there.

## Stage 1: related information
1. **Rendering.** `format_error_tree` prints a related line with `basename_of(current_source)`: use the related span's
   own file (`span.source`), and `--:--` for positions in lib files (tsc's baselines say `lib.es5.d.ts:--:--:`, 70
   such lines). Check `print_error_ctx` (CLI output) prints related info sensibly too (tsc's non-pretty form:
   `  file(line,col): message` indented under the diagnostic, not critical).
   Chain lines are nested sub_errors with null spans (`check_assignable` in `ty/check.c` pushes each as a child of the
   previous); related info goes on the *top-level* Error after the chain so it prints after the whole chain, which is
   tsc's order. Add a named helper (`error_add_related_s/_d`) rather than calling `error_sub_error_s` with a span, so
   the two uses stay readable. A related span that is invalid must not be added (it would render as a chain line).
2. **Relation-level related info.** tsc's `associateRelatedInfo` inside the relation: TS2728 on "Property 'x' is missing
   in type" (at the missing property's first declaration: `Prop.name_span`, follow `Prop.target` for instantiations;
   also TS2739/2740 do *not* get it), TS2208 on a type parameter target (`reportRelationError`: "This type parameter
   might need an `extends {1}` constraint", 2208/2209 variants), TS2798 & co if they show up. The explanation needs a
   channel for these: `ty_explain_unrelated` should return related items alongside `VecTyReportLine` (e.g. a small
   `TyExplanation { lines; related; }`), and every consumer of report lines (`check_assignable`,
   `report_unassignable_with_head`, call.c's overload reporting) attaches them to the Error it creates. Look at how tsc
   drops related info when an outer message replaces the chain before copying blindly.
3. **Checker-level related info**, in order of tests affected: TS2728 for TS2448/2449/2454 use-before-declaration
   (`'x' is declared here`), TS2552/2551 "Did you mean" (`'y' is declared here`), TS2729, TS2420; TS6203/TS6204 on
   duplicate declarations (TS2300/2451/2393...: every other declaration gets its own error with "'x' was also declared
   here" pointing at the first; check tsc's `addDuplicateDeclarationError` for the exact pairing, capped at 5); TS6210
   under TS2554 ("An argument for 'x' was not provided", at the parameter); TS2793 under TS2769; TS2750/TS1007 as the
   grep leads. `./resultdiff.py --gained` after each; stop at diminishing returns, list the rest in triage.md.

Validate: `test/probes/plan16/measure.py` (after `./failstats.py`): the 117 "pass but for related" tests should mostly
be passing; no errors component lost.

## Stage 2: elaboration core
Everything below mirrors `checker.ts` `elaborateError` .. `elaborateObjectLiteral` (tsc 5.8, around line 20656);
read it, it's ~500 lines. JSX (`elaborateJsxComponents`) is out of scope: tttt has no JSX.

1. **Entry point.** `check_assignable` (`ty/check.c`) grows an `Expr const* expr` (null where there is none). On
   failure, before explaining: `elaborate(me, expr, source, target, errs)`; if it reported, return false without the
   whole-expression error. Callers and what tsc passes as the expression:
   - variable initializers (`binding.c` `binding_typecheck`: the initializer; tsc skips elaboration for binding
     patterns? check `checkVariableLikeDeclaration`),
   - assignments (`expr.c` `assignment_target_typecheck`: the right-hand side; compound assignments too, 40285),
   - `return` (`statement.c`) and expression-bodied arrows with a return annotation (find where that's checked:
     `const f = (): string => 1` already reports at `1`),
   - class field initializers (`class_decl.c`), property declarations with initializers in general,
   - call arguments (`call.c` `report_unfit_argument` -> `ty_report_unfit_argument`: `arg->expr`; the elaborated error
     is plain TS2322, the TS2345 head is only used when nothing elaborated),
   - `yield` (`expr.c`), `satisfies` (`ty_check_satisfies`, TS1360 only when nothing elaborated), for-of with an
     expression variable (tsc passes the iterable expression, 44732).
2. **`elaborate_error`**: bail if the target is or contains (intersection) a conditional type; did-you-mean-to-call
   (source has call/construct signatures whose return fits: report the normal whole error at the expression + related
   TS6212/TS6213 at the expression); recurse through parentheses, `as const`, `=` and `,`; dispatch object literal,
   array literal, arrow function (expression body only, no annotated parameters, single call signature on the source).
3. **Elementwise** (`elaborateElementwise`): for each property (object literal: not spreads; methods, accessors and
   shorthands have no inner expression; computed non-literal names use TS2418's message) or element (array literals:
   skip omitted and, for tuple targets, indices the target lacks): target type through
   **`getBestMatchIndexedAccessTypeOrUndefined`** (skip if missing or an indexed access on a type variable), source
   type the literal type's property; if unrelated, recurse into the value expression; if that didn't report, report at
   the property *name* (object) / element: source is the value expression's own type (tsc re-checks it as a mutable
   location with the source property type as context: in tttt `ty_expr_type` of the value plus literal widening where
   the target has no literals, `ty_report_source_cstr` already renders that), target with the missing type removed for
   optional properties. Then related info: TS6501 at the index signature if there's no target property (not in lib
   files: `SourceFile` needs a "lib" bit if it hasn't one), else TS6500 at the target property's (or the target type
   symbol's) first declaration, not in lib files. Every failing property is reported, not just the first.
4. **Array literals whose source isn't a tuple**: tsc re-checks the literal as a tuple (`forceTuple`). Build the tuple
   from the element expressions' types instead of re-checking; if that is wrong somewhere, list it in hacks.md.
5. **Arrow functions**: relate the single source signature's return type to the union of the target's call signature
   returns; recurse into the body expression, else report at the body with related TS6502 at the target symbol's first
   declaration (the function type node) and TS2773 "Did you mean to mark this function as 'async'?" when a Promise of
   the return would fit.
6. **`getBestMatchingType`** (tsc: findMatchingDiscriminantType || findMatchingTypeReferenceOrTypeAliasReference ||
   findBestTypeForObjectLiteral || findBestTypeForInvokable || findMostOverlappyType). Write it once as
   `ty_best_matching_type` and also use it in `related_to_some`'s report path (`relate.c`), which currently only
   elaborates when exactly one union member is an object type. `excess_check_target_of_union` is the discriminant half
   already; share rather than duplicate.

Probe file to start from: `test/probes/plan16/elaboration.ts`, plus `deepElaborationsIntoArrowExpressions`,
`genericCallWithObjectLiteralArgs`, `contextualTyping12`, `objectLiteralExcessProperties`, `errorElaboration`,
`slightlyIndirectedDeepObjectLiteralElaborations`, `invalidVoidValues` (TS6212).

Validate: rerun `test/probes/plan16/measure.py` (relocation: pairs of spurious tttt / missing tsc assignability errors
in one statement) and expect most of the 109 to be gone; `./fasttest.sh --summary` no losses (an elaboration that fires where tsc's
doesn't will show as lost errors components: compare the bail-outs with tsc's); corpora `./realworld.py` (tsc corpus
must stay 0; type-fest/valibot counts may rise slightly, spot-check that new ones are the same errors split per
property); `./bench.sh` shouldn't move (elaboration only runs on failure).

## Stage 3 (if the session allows): overloads and leftovers
- TS2769 "No overload matches this call": tsc elaborates each candidate's error (`getSignatureApplicabilityError`
  with an errorOutputContainer) and carries the related info into the chain. See what `report_no_match` does with it.
- Triage items this plan covers, delete them when done: "Object literal elaboration", "tsc elaborates a lambda
  argument that doesn't fit at its return expression" (genericCallWithGenericSignatureArguments2),
  genericMappedTypeAsClause's `{ bFoo: 'bar' }`; hacks.md "Excess property checks (plan 12...): no elaboration".
- Adjacent but separate (triage, don't do here): TS2561 "Did you mean to write 'foreword'?" for excess properties
  (spelling suggestions), T_1-style type parameter renaming in display.

## Risks
- tsc elaborates with the *declared* source types of the literal's properties and falls back to the expression type.
  tttt's per-expression memo holds the type under the contextual type the literal was checked with; that should be the
  same thing, but inference contexts (generic calls) may leave a different memo. Arguments of generic calls are where
  to look first if messages differ.
- Elaboration must never report where the whole relation would pass: always gate on `!ty_is_related(source prop,
  target prop)` like tsc, and when a property elaborates nothing but the whole failed for another reason (a missing
  property), tsc falls back to the whole-expression error: `reportedError` is only set by a failing property.
