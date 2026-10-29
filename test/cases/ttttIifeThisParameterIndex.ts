// @strict: true
// An immediately invoked function's parameter takes the argument at its position among the parameters as written, a
// `this` parameter counted (bun case 25-P2: tsc's getContextuallyTypedParameterType, func.parameters.indexOf)
(function f(this: any, a) { const n: never = f; })(1);
(function g(this: any, a, b) { return [a, b]; })(1, "x");
(function h(this: any, ...r) { return r; })(1, 2);
