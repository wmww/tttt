// @strict: true
declare function pipe<A, B>(a: A, ab: (a: A) => B): B
declare function m2<Z>(zero: Z, f: (z: Z) => Z): (elements: string) => Z
const r5 = m2(() => 1, (_b) => () => 2)
declare function take<B>(ab: (a: string) => B): B
const r6 = take(m2(() => 1, (_b) => () => 2))
