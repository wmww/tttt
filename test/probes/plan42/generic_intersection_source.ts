type Box<V> = { v: V };
declare function f<T>(x: { [K in keyof T]: Box<T[K]> }): T;
function g<U>(u: U & { a: Box<number> }) {
    const r = f(u);
    return r;
}
