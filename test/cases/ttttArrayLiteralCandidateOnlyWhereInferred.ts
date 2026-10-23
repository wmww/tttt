// @strict: true
// An array literal where the parameter infers nothing (`c: unknown`) isn't a literal candidate: `xs`'s number[] stays
// the first candidate and T is number[] (tsc marks literal types ObjectFlags.ArrayLiteral, tttt remembers them)
declare const xs: number[];
declare function f<T>(x: { a: T, b: T, c: unknown }): T;
const r = f({ a: xs, b: ["s"], c: [1] });
const r2 = f({ a: xs, b: ["s"], c: 1 });
const r3 = f({ a: xs, b: ["s"] , c: [] });
