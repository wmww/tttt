# Plan 12: fresh/regular literal types, object literal freshness and excess property checks

The literal half of the plan 11 sketch (the higher-order inference half is planned separately). Probes:
`test/probes/plan12/*.ts`, run with `FAST=1 ./filetest.sh test/probes/plan12/X.ts -d` (tsc's side:
`./tscbaseline.sh`). All of them fail today, each for the reason in its header comment.

## Why
tsc has two types per literal value: the *fresh* one a literal expression produces (`"a"`, `1`, `-1`, `true`, the
value `E.x`) and the *regular* one a written type or anything derived from one holds. Widening at mutable
locations only widens fresh literals. tttt has one type per literal and widens by *position*, so every regular literal
that reaches a widening site is widened wrongly: `function f(op: "<" | "=") { return { op }; }` gives
`{ op: string }` (semver.ts, **13 of the corpus's 91 diagnostics**: `createComparator` returns `{ operator, operand }`),
`declare const a: "a"; let x = a` gives `string`, `box(a)` infers `T = string`, `function f(p: "p") { return p }`
returns `string`, and `const e = E.x` of a single-member enum prints `E` where tsc prints `E.x` (hacks.md "Single-member
enums", `baseline_test_case_add_symbol_value` printing enum members by hand). Probes: `semver.ts`,
`regular_sources.ts`, `sites.ts`, `enums.ts`, `enum_messages.ts`.

Object literal freshness is the same idea on object types (`ObjectFlags.FreshLiteral`): an object literal expression
is fresh until it is widened into a declaration, asserted, spread or inferred, and a fresh one related to a target
gets **excess property checks** (TS2353), which tttt doesn't have at all: `var foo: { id: number } = { id: 4, name:
"foo" }` is silent (probe `excess.ts`). Suite: TS2353 missing 172 times in 87 tests, 32 tests' errors diff is TS2353
alone; TS2561 (the "did you mean" variant) 7 in 3 tests. It is stage 4 because it needs the relation and widening
plumbing of stages 1-3, but is otherwise independent.

Not freshness, found on the way: semver.ts's TS2678 (`switch (s: string) { case undefined: }`) is tsc's
`isTypeEqualityComparableTo` (a nullable case type is always fine), probe `case_undefined.ts`, stage 0.
`spreadBooleanRespectsFreshness`: freshness fixes `(string | boolean)[]` -> `(string | false)[]`, but tsc prints
`FooBase[]` because narrowing filters the union's *origin* (`Foo = FooBase | FooArray` minus `FooArray` prints as
`FooBase`); tttt has no union origins. Same in `literalFreshnessPropagationOnNarrowing`. Out of scope (triage).

## tsc's model (checker.ts)
- `getStringLiteralType(v)` etc. intern the regular type; `getFreshTypeOfLiteralType` makes its fresh twin lazily
  (`regularType`/`freshType` pointers, new type id). Booleans are four intrinsics, created fresh false, regular false,
  fresh true, regular true; `boolean` = regular false | regular true. An enum member's declared type is **fresh**,
  the enum union holds the regular ones, and a written `E.x` type reference is regularised (`getTypeReferenceType`).
  A literal *type node* is `getRegularTypeOfLiteralType(checkExpression(literal))`: both twins are made, regular first.
- Producers of fresh: string/number/bigint literal expressions, `-1`/`+1`, `true`/`false`, a no-substitution
  template, constant template expressions, the value of an enum member. Everything else yields regular.
- `getWidenedLiteralType`: fresh literal -> primitive (fresh enum literal -> its enum), unions mapped; regular stays.
  `getBaseTypeOfLiteralType`: widens *regardless* of freshness (comparisons, operators, compound assignment, error
  message generalisation `reportRelationError`, TS2367). The two must be separate functions.
- `getWidenedLiteralLikeTypeForContextualType(t, ctx)` = unless `isLiteralOfContextualType`, widen literals and unique
  symbols; **then regularise**. Used by `checkExpressionForMutableLocation` (object literal properties, array
  elements; const contexts only regularise) and, for unit types only, inferred return/yield types.
- Declarations: `getWidenedTypeForVariableLikeDeclaration` widens (fresh-only) unless const/readonly, and does not
  regularise: `const e = E.x` has the fresh `E.x` (prints `E.x`), `let x = a` keeps regular `"a"`.
- Inference (`getCovariantInference`): primitive constraint -> candidates regularised; top-level and not only in the
  return type -> `getWidenedLiteralType` (fresh-only); else as they are. `checkExpressionWithContextualType` strips
  freshness from a literal that `isLiteralOfContextualType`. Rest arguments: regular under a primitive context, else
  fresh-only widening.
- Regularised explicitly: `typeof x` type queries, `export default` of a const, property names/computed keys, switch
  case types in narrowing, intersection members, `x as const`, discriminant keys (`constituentMap`).
- Relations: `isTypeRelatedTo` / `getNormalizedType` map fresh -> regular (unions member-wise) before anything, so
  caches and identity only ever see regular literals. `getUnionType` keeps fresh members (that's what makes
  `c ? "a" : "b"` widen) but drops a fresh literal whose regular twin is in the set (`removeRedundantLiteralTypes`).
- Narrowing: `getAssignmentReducedType` maps the result back to fresh when the assigned value is a fresh boolean
  (`flag = true` then `let x = flag` is `boolean`, `flag = t` with `t: true` is `true`).
- Baselines: the types writer prints expressions with `getRegularTypeOfExpression` (regular) and declarations with
  the symbol's type (possibly fresh). Error messages see normalised (regular) types, which is why `let q: "z" = e2`
  says `'E'` in tsc too (the hacks.md claim that errors show `E.x` did not reproduce; the `.types` side does).

## Design
- **Representation**: the fresh twin is a separate interned `Ty` (`TY_LITERAL` with `TYF_FRESH_LITERAL` and a
  `literal.regular` pointer), found from the regular one through the intern table (`KEY_FRESH_LITERAL, regular id`),
  so no published `Ty` is ever mutated. API: `ty_fresh_literal(t)`, `ty_regular(t)` (literals, and unions mapped
  member-wise, memoised through the intern table like tsc's `UnionType.regularType`), `ty_is_fresh(t)`. The four
  boolean intrinsics are created in tsc's order in `checker_new()`; `TYF_BOOLEAN` stays the regular pair only (check
  what tsc's `getUnionType`/`formatUnionTypes` do with a fresh true + fresh false union: display collapses by flags,
  `ty_display.c` checks identity today). `ty_literal()` keeps returning the regular type, so every caller that
  doesn't opt in is unchanged. Unique symbols have no twin; computed enum members do (tsc's
  `Freshable` = `Literal | Enum`, `createComputedEnumType` makes both).
- **Ids / union order**: fresh ids are allocated when the twin is first made, like tsc, so a union of fresh
  members sorts by fresh ids. Keep the order faithful by making the twin wherever tsc does: literal expressions,
  and literal type nodes (tsc's `checkExpression(node.literal)` side effect) and enum members when the enum is
  resolved. Baselines print regular unions for expressions, so the visible effect is mostly on declarations.
- **Identity**: pointer equality stays type identity, but a literal compared with `==` may now be fresh. Rule: the
  relation entry points (`ty_is_related`, `check_related`, `is_related_to`'s recursion) normalise first, and every
  place that compares literal types by pointer (flow.c's unit-type and discriminant matching, `union_contains` in
  intersections, switch exhaustiveness, `TY_TRUE`/`TY_FALSE` checks: 11 sites, `ty_display.c` boolean collapse, enum
  display "is the whole enum") either regularises or is shown not to see fresh types. Keys built from type ids
  (indexed access, discriminant caches) regularise the literal first.
- **Memos hold fresh types** (tsc's `checkExpression`). The `.types` traversal prints `ty_regular()` of expression
  memos (`baseline_test_traversal.c` `expr_get_type` sites) and the symbol type at declarations; the enum member
  hack in `baseline_test_case_add_symbol_value` goes (`ty_type_of_symbol` of a member is its fresh literal, which
  `ty_display` already prints as `E.x` because it isn't the enum's own type).

## Widening sites (tttt today -> rule)
- `ty_widened_literal` (relate.c) becomes fresh-only (`getWidenedLiteralType`) incl. enum literals, so
  `widen_enum_literals` (expr.c) folds into it. `ty_base_type_of_literal` (flow.c) stops calling it and widens
  unconditionally. Re-point the callers that mean *base type*: relate.c comparable relation (two sites),
  `report_relation_error` / `ty_report_source_cstr`, `TyDisplay.widen_literals` (probably removable once properties
  are regular-widened at creation; it is marked HACK), src/expr.c:134 (TS2344 message: route through
  `ty_report_source_cstr`), the operator sites in src/expr.c that already use base.
- `widen_literals` / `check_for_mutable_location` (expr.c): fresh-only widening, then `ty_regular`; const context:
  `ty_regular` only. `ty_widened_return_type`: unit types only (already), then regular. Getter/accessor return and
  `ty_object_literal_member_type` go through the same function.
- `ty_widened_for_declaration(literals)`: fresh-only, no regularisation. Callers: binding.c (let/var), class_decl.c
  fields (`!readonly`) and accessors, lower.c parameter defaults (`p = a` must stay `"a"`), expr.c `this`-assigned
  objects and the contextual/initializer check near expr.c:510. lower.c `typeof` (literals=false): regularise.
- infer.c `inference_call_result` (`keep_literals`/`top_level`): primitive constraint -> regular, top-level ->
  fresh-only widening. call.c `rest_args_tuple`: regular under a primitive context, else fresh-only widening. Check
  where tttt's call path corresponds to `checkExpressionWithContextualType` and strip freshness there.
- flow.c: assignment narrowing re-freshens for a fresh boolean assigned value; case types and `===` operands regular.
- `binding_add_names` (hacks.md: exported bindings not widened like `defaultNamedExportWithType1`): export default
  of a const regularises, see if it falls out.

## Stages (suite runnable after each, `./fasttest.sh --summary` no unexplained losses)
0. **Case clause nullable rule** (statement.c `STATEMENT_SWITCH`: skip the check when the case type is
   null/undefined-ish, tsc's `isTypeEqualityComparableTo` with needle and case in tsc's order). Minutes; semver.ts
   TS2678, `literalTypes1`'s two spurious TS2678, `case_undefined.ts`.
1. **Representation, no producers**: twin constructor, `ty_regular`, boolean intrinsics, union redundancy rule,
   relation normalisation, display, the base/widened split with `ty_widened_literal` still unconditional. Nothing is
   fresh yet: the suite must be bit-identical (a good check that the plumbing is inert).
2. **The flip** (atomic by nature): producers make fresh types, `ty_widened_literal` becomes fresh-only, widening
   sites regularise per the list, traversal prints regular expression types, enum hack deleted. Then chase the
   diff: every lost component is either a site from the list not yet ported or a pointer comparison that now sees
   a fresh literal. Expect most of the session here. semver.ts -> 0 at the end of this stage.
3. **Remaining per-site rules**: inference, rest args, contextual stripping, flow boolean re-freshening, `typeof` /
   export default, keys. Some will be needed in stage 2 to avoid losses; fold them in as needed.
4. **Object literal freshness + excess property checks.** `ty_object_literal(literal=true)` types become fresh
   (a flag in the intern key; `ty_regular` gives the regular twin, made through `ty_object_literal` with the props'
   types regularised recursively like `getRegularTypeOfObjectLiteral`). Regular at: declaration widening, inferred
   return types, `as` assertions, spread results, inference candidates, `typeof`. In `is_related_to`, a fresh object
   literal source against a target that is not an intersection member (`IntersectionState.Target`) gets
   `hasExcessProperties`: skip `{}`/`object`/`Object` targets (assignable/comparable), union targets reduced by
   discriminant (`findMatchingDiscriminantType`, tttt has discriminant machinery in flow/expr.c) else primitives
   filtered out, only props declared *in this literal* are checked (not spread ones), known = own property, applicable
   index signature, or any member of a union/intersection target; then the source is regularised for the structural
   check. The error is TS2353 "Object literal may only specify known properties, and 'p' does not exist in type 'T'."
   **at the property name** with the parent head skipped (`reportParentSkippedError`): `TyReportLine` needs a span
   (the prop's `name_span`) that `check_assignable` / argument reporting honour. Also: `excessPropertyCheckWith*`,
   intersection-target `propertiesRelatedTo` re-check for fresh sources (relate.c around tsc 22811). TS2561 needs
   tsc's spelling suggestion (`getSpellingSuggestion`, also TS2551 which tttt lacks): optional, only if cheap. Weak
   type detection (TS2559, 12 tests, hacks.md "no weak type check") sits next to it in tsc's `isRelatedTo`; it is not
   freshness, leave it unless it falls out. Validate on the corpus: tsc's own source is clean, so any TS2353 there is
   a bug in this stage.

## Risks
- **Pointer comparisons seeing fresh literals** are the main risk: silent wrong answers (a discriminant not
  matching, a union not collapsing to `boolean`, an exhaustive switch no longer exhaustive). Stage 1 keeps them
  inert; in stage 2 a debug-only `check(!ty_is_fresh(...))` at relation cache insertion, intern keys of
  unions-with-alias/indexed/discriminant keys and flow's unit comparisons finds them quickly.
- Fresh and regular unions are different `Ty`s: caches keyed by union pointer may do double work but must not
  change answers. `./bench.sh` before/after (one extra intern lookup per literal expression).
- tttt re-checks literals per contextual type (`kind_uses_context`, hacks.md "Expression memo"): fresh/regular
  must not depend on which overload candidate checked last.
- Keep passing: `typeofStripsFreshness`, `exportDefaultStripsFreshness`, `strictNullChecksNoWidening`,
  `freshLiteralTypesInIntersections`, `literalTypesWidenInParameterPosition` (pass today by the positional rule).
- Excess property checks will surface spurious errors wherever tttt's contextual typing or discriminant reduction
  is weak (TS2353 is only right when the target type is right): expect a spurious tail; the corpus is the guard.

## Validation
- All probes in `test/probes/plan12/` match tsc (`sites.ts` and `regular_sources.ts` per line; `excess.ts` for stage 4).
- `./realworld.py`: semver.ts 0 (13 TS2345 + TS2678), no new diagnostic anywhere (91 -> 77 or better).
- `./fasttest.sh --summary`: no losses that aren't explained in the commit message. Tests to watch / expected to
  move (types or errors): `literalTypeWidening` (spurious TS2345), `literalTypes1/2/3`, `literalTypesAndDestructuring`,
  `literalTypesAndTypeAssertions`, `stringLiteralsWithTypeAssertions01`, `booleanLiteralTypes1/2`,
  `numericLiteralTypes1/2/3`, `enumLiteralTypes3`, `stringEnumLiteralTypes3`, `enumLiteralUnionNotWidened` (inference
  of a regular enum union), `enumLiteralsSubtypeReduction`, `widenedTypes1`, `arrayLiteralWidened`,
  `objectLiteralWidened`, `freshLiteralInference`, `genericFunctionHasFreshTypeArgs`, `typePredicateFreshLiteralWidening`;
  stage 4: `./failstats.py --code TS2353` (87 tests), `objectLiteralExcessProperties`, `excessPropertyCheck*`,
  `contextualTyping2/4/9/12/17/20`, `nestedFreshLiteral`, `objectLiteralFreshnessWithSpread`.
- Update `hacks.md` (delete "No fresh/regular literal type pairs", "Single-member enums", the enum member printing
  hack, `ty_union`'s "no fresh/regular literal pairs", relate.c "no excess property checks / freshness") and
  `documentation.md` ("Expressions" paragraph on widening, the literal kind in the type core list).

## Estimate
Stage 0: minutes. Stages 1-3: one long session, possibly two (the diff chase after the flip is the unknown).
Stage 4: one session. Total 2-3 sessions.
