// @strict: true
// `typeof D` to `D | T` is closely matched by D, which shares its symbol, so nothing is inferred to T from it (bun case
// 22-C9: tsc's isTypeCloselyMatchedBy, s.symbol === t.symbol)
class D { x = 1 }
class D2 extends D { static s = 1 }
declare function f<T>(a: D | T, b: T): T;
const r = f(D, D2);
declare function g<T>(a: string | T, b: T): T;
const r2 = g(1, "a" as "a" | 2);
