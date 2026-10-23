// A heritage element is a left-hand side expression, which is no arrow function (tsc's
// parseLeftHandSideExpressionOrHigher): `implements x => void` is `x` then TS1005 ',' expected. Babel's
// arrow-function/invalid-class-implements fixture
class Q implements x => void {}
class R extends x => 1 {}
