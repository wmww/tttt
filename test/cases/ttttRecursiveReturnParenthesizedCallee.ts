// @strict: true
// A return of a call to the function itself doesn't count towards its return type only with the bare identifier as
// the callee (tsc's checkAndAggregateReturnExpressionTypes: isIdentifier(expr.expression)): `return (f)();` is circular
// (bun's 32-C6a)
function f() { return (f)(); }
function g() { return g(); }
function h() { return (h()); }
function k() { return 1 + k(); }
