# Triage
Uninvestigated items: dump here what you don't want to deal with now. Triage sessions drain this into `bugs/*.md`,
plans, or fixes.

Last drained 2026-09-27 (triage session, todo entry 10). Everything that was here went to:
- `bugs/missing_checks.md`: diagnostics tttt lacks or reports under another code (the queue for "suite checker bugs"
  sessions), including JSX checks and the single-code buckets.
- `bugs/type_core.md`: inference, relations/reduction, circularity, classes, narrowing and flow.
- `bugs/display.md`: alias display rules, structure, specifiers, underlines, the symbols baseline.
- `bugs/harness.md`: harness, CLI, tools, dead code. `bugs/perf_leads.md`: what to profile/try next.
- Plan 27 "Leftovers collected by triage" (parser), plan 26 "Leftovers collected by triage" (JS).
Items whose tests pass now were dropped (requiresScopeChange, computed-name index signatures, invalidNewTarget,
interfaceThatIndirectlyInheritsFromItself, reverseMappedUnionInference, the `W["_i"] | undefined` display...).


## From plan 30 stage 3 (2026-09-27)
- inferTypesWithExtends1: `T extends (infer U extends number ? 1 : 0) ? 1 : 0` (tsc's speculative lookahead for
  `infer T extends U ?`: parsed as a conditional in parentheses) gets TS1005s; X12/X13 (`infer U extends number) ?`)
  are the other half of the rule. Parser (plan 27).
- The scope of `infer X extends C`: C is resolved in the scope of the whole extends type (`type_lookup_names`,
  TYPE_INFERRED), so another `infer` of the same clause isn't visible (tsc: TS2304, inferTypesWithExtends2 passes);
  check what tsc does for an `infer` declared in an *outer* conditional's extends type.

## From suite checker bugs 3 (2026-09-27)
- An object literal with a numeric-string key prints unquoted: `{ "0": 0 }` is `{ 0: number; }` here, tsc
  `{ "0": number; }` (probe: `var m: MyMap<number> = { "0": 0 }` types baseline). Display (bugs/display.md).
- inferTypes1 logs `scope.c: Declaration B acquired multiple times` (the only log_error left besides the lib-file
  errors of modularizeLibrary_ErrorFromUsingES6ArrayWithOnlyES6ArrayLib).
- tsc's non-widening `undefined` (`{}["hi"]` under noImplicitAny is `undefined`, and `var x = {}["hi"]` stays
  `undefined`; tttt widens it to any): noImplicitAnyIndexing(Suppressed) fail types on it only. Related to hacks.md
  `widen_nullable`.

## From plan 27 stage 2 (2026-09-27)
- Variance annotations are parsed and dropped (`parse_generic_decl_item()`): no TS2636/TS2637 (the check that a
  declared `in`/`out` agrees with the measured variance), no TS1273/TS1274/TS1030/TS1029 on type parameter
  modifiers, and the relation doesn't use declared variances. A feature, not a parser fix: varianceAnnotations and
  friends. Worth a plan (check `./failstats.py --code TS2636`).
- The sub-agents reported for their clusters (details in plan 27 stage 2): classExtendingPrimitive wants `Did you mean
  'undefined'?` from scope_suggest(); ambientWithStatements gets a spurious TS1105 for `break` in a `declare module`;
  constDeclarations-invalidContexts has a TS1156 underline one character short; classStaticBlock6/7 fail on
  `arguments`/`return` checks; castOfYield misses TS2318 (IterableIterator); privateNameBadDeclaration treats
  `A.prototype = {}` as an expando in a .ts file; TS1312 (`{ x = 1 }` outside a pattern) is missing;
  optionalPropertiesSyntax: `ty_check_duplicate_index_signatures` skips files with parse errors, tsc doesn't.

## From the type-fest session (2026-09-27, R20/jsonify)
- exactOptionalPropertyTypes: `'a' in x` should narrow `x.a` to non-undefined when the property's type contains
  tsc's missingType (narrowTypeByBinaryExpression's containsMissingType case): `x: {a: number} | {a?: never}` gives
  `number` in the true branch under tsc, `number | undefined` under tttt. In tttt's model "contains missing" is an
  optional property (exact on) whose stored type lacks undefined.
- Padded properties (`a?: never` from widening) get symbol declarations only from the sibling that has the property;
  tsc lists both (`Symbol(a, Decl(..., 3, 18), Decl(..., 2, 18))`), probe `plan19/typefest/exact_missing_padding.ts`.
