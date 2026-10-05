type Deep<T, U> = { [K in keyof T]: { val: Deep<T[K], U[]>; tag: U } };
declare function f<T, U>(x: Deep<T, U>, u: U): T;
interface A { a: A; b: { c: A } }
declare const d: Deep<A, number>;
const r = f(d, 1);
r.a.b.c.a;
const s = f({ x: { val: { y: { val: {}, tag: [1] } }, tag: 1 } }, 1);
