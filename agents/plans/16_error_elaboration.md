# Plan 16 (stub, needs a planning session): error elaboration (tsc's elaborateError)

## Why
tsc doesn't report a failed assignment of an object/array literal, an arrow function or a JSX element as one
error on the whole expression: `checkTypeRelatedToAndOptionallyElaborate` first calls `elaborateError(node, source,
target)`, which drills into the expression (`elaborateObjectLiteral`, `elaborateArrayLiteral`, `elaborateArrowFunction`,
`elaborateJsxComponents`; through parentheses, `=`/`,`, const assertions) and reports at the innermost property value /
element / returned expression that doesn't fit, with a related TS6500 "The expected type comes from property 'p'
which is declared here on type 'T'" (TS6501/6502 for index signatures and return types). tttt has none of it, so:

```ts
const x: { a: string } = { a: 1 };  // tsc: (1,28) TS2322 Type 'number' is not assignable to type 'string'. + TS6500
                                    // tttt: (1,7) TS2322 Type '{ a: number; }' is not ... / Types of property 'a' ...
```

Estimated size: 128 tsc baselines have a TS6500/6501/6502 related line; `./failstats.py` shows TS2322 missing 825 times
in 346 tests and spurious 754 times in 302, much of it plausibly the same error at another position. Nested excess
property checks (plan 12, `excess.ts` probe line 9, `objectLiteralExcessProperties`) need it too: tsc reports the nested
TS2353 alone, located at the property, because elaboration relates the nested literal on its own.

## What the planning session should decide
- Measure first: how many failing tests would pass with elaboration (probe a handful, `./failstats.py --code TS2322`
  column/line mismatches, "same codes+lines, column differs" is 101 tests).
- Where it hooks in: `check_assignable` (ty/check.c) gets the source *expression* where the caller has one (variable
  initializers, assignments, arguments in call.c, return statements, property assignments). The relation itself stays.
- tsc's details worth copying: `getBestMatchIndexedAccessTypeOrUndefined` for union targets, which properties are
  elaborated (not computed names that aren't literal, not index-signature-only generic targets), the "use the
  expression's own type (checkExpressionForMutableLocation)" retry, related-info spans (TS6500 at the target property's
  declaration, skipped for lib declarations: `hasNoDefaultLib`).
- `TyReportLine.span` (plan 12) already lets a line relocate the error; `Error` needs related info (check how TS2728
  "'x' is declared here" is emitted today and reuse it).
