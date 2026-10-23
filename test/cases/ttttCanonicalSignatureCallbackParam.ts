// @strict: true
// A generic source compared in the context of the target: tsc's canonical signature and instantiation in context are
// instantiations of the two, whose `cb: T` is the instantiated F, so cb is a callback (compared one way only)
interface Foo<T> { m<U>(cb: T, u: U): void }
interface Bar<T> { m<U>(cb: T, u: U): void }
declare let f: Foo<(x: string) => void>;
declare let g: Bar<(x: string | number) => void>;
f = g;
interface Foo2<T> { m(cb: T): void }
interface Bar2<T> { m(cb: T): void }
declare let f2: Foo2<(x: string) => void>;
declare let g2: Bar2<(x: string | number) => void>;
f2 = g2;
