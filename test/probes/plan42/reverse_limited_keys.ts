declare function f<T>(x: { [K in keyof T & ("a" | 1)]: Box<T[K]> }): T;
type Box<V> = { v: V };
declare const sym: unique symbol;
const r = f({ a: { v: "s" }, 1: { v: 2 }, b: { v: true } });
declare function g<T>(x: { [K in keyof T & (typeof sym | "x")]: Box<T[K]> }): T;
const r2 = g({ [sym]: { v: 1 }, x: { v: "x" }, y: { v: 0 } });
