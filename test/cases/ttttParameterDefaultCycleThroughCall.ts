// @strict: true
// Whether a parameter's default can be undefined is resolved under an entry of its own (tsc's
// parameterInitializerContainsUndefined, TypeSystemPropertyNameInitializerIsUndefined): a default reaching the parameter
// through a function whose return type is being inferred is circular there, TS2502 at the parameter and TS7024 at the
// arrow. (Types: the arrow's `n` prints `number | undefined`, tsc's `number`: tsc checks the body again later and asks
// afresh, tttt keeps the expression's type from inside the cycle)
function f(n: number | undefined = [1].map(() => n)[0]) {}
declare function g(a: any): number;
function f1(x: number | undefined = g(x)) { return x; }
function f2(y: number | undefined = 1) { return y; }
