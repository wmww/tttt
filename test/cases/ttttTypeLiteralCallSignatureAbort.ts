// @strict: true
// @noEmit: true
// A type member's `(` commits it to a call signature (tsc's parseSignatureMember): its parameter list reports TS1390 and
// aborts at a reserved word that starts a type member, and the call signature stands (TS7020), then a method
// (mutation testing seed 20261101, contextualParamTypeVsNestedReturnTypeInference1)
declare const x: {
  (function ()
};
type T = { (class: number): void };
