// A spread element's missing operand is reported where it is missing (tsc's parseSpreadElement), which is how
// `(a, ...)` in an argument list recovers: the parenthesized expression ends at `...`, a spread of nothing follows
declare function f(...args: any[]): void;
declare const a: any;
f(...);
f(1, ...);
f((a, ...) => 1);
