// An instantiation expression followed by a property access is TS1477 through an optional chain too (tsc's
// parsePropertyAccessExpressionRest checks the expression, not the `?.`). Babel's
// type-arguments/instantiation-expression-property-access fixture
declare const a: { c: number; <T>(x: T): T };
type b = string;
a<b>.c;
a<b>?.c;
a<b>?.["c"];
a<b>?.("x");
(a<b>)?.c;
