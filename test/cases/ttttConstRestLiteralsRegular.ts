// @strict: true
// @noEmit: true
// The arguments of a rest parameter typed by a const type parameter are inferred as regular literals: tsc's
// getSpreadArgumentType takes getRegularTypeOfLiteralType of each under inConstContext, so an unconstrained inference
// from `f(...).o` keeps "a" | "b" rather than widening fresh ones to string (sphi/osis's enumAdapter tests)
declare function f<const X extends string[]>(...cases: X): { o: { [I in keyof X]: X[I] }[number] };
declare function id<T>(x: T): { v: T };
const r1 = f('a', 'b').o;
const m1 = id(r1);
const z1: number = m1;
declare function g<const X extends string[]>(...cases: X): { o: X[number] };
const z2: number = id(g('a', 'b').o);
const z3: number = id(g('a', 'b'));
