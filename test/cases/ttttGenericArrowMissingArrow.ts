// A parameter list after type parameters followed by `{` is an arrow function missing its `=>` (tsc's
// parseParenthesizedArrowFunctionExpression takes `{` even when the lookahead was unsure), not a type assertion.
// In a class body the `<` ends the member list (TS1068) and the rest is such an arrow function
class D { <T>() {} }
class E { <T>(x: T) { return x; } y = 1; }
<T>() {};
var a = <T>(x: T) {};
var f = async <T>(x: T) {};
var g = <T,>(x) : number {};
declare var foo: any;
<any>(foo)
{ foo(); }
var h = <string>(foo) + 1;
