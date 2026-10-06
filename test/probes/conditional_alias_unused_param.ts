// A conditional alias body keeps the alias type parameters it never mentions: g returns C<X, number> (tsc), not C<X, U>
type C<T, U> = T extends string ? { a: T } : { b: T };
declare function f<V, W>(v: V, w: W): C<V, W>;
function g<X>(x: X) {
    const r = f(x, 1);
    return r;
}
const s = g<number | boolean>(1);
type D<T> = C<T, 5>;
function h<Y>(y: Y): D<Y> { return null!; }
const t = h(true as boolean | string);
