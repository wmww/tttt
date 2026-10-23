// An erased signature's parameter is a callback unless it was generic in the instantiation erased (tsc's
// isInstantiatedGenericParameter reads the erased signature's target): `m<U>(a: X)` of `Foo<S>` has `a: S`
type S = (p: string) => string;
type T = (p: string | number) => string | number;
interface Foo<X> { m<U>(a: X, b: U): void; m<U>(a: X, b: U, c: U): void }
interface Bar { m<U>(a: T, b: U): void; m<U>(a: T, b: U, c: U): void }
declare let foo: Foo<S>;
let bar: Bar = foo;
interface Foo2<X> { m<U>(a: X, b: U): void }
interface Bar2 { m<U>(a: T, b: U): void }
declare let foo2: Foo2<S>;
let bar2: Bar2 = foo2;
interface Foo3<X> { m(a: X): void; m(a: X, b: 1): void }
interface Bar3 { m(a: T): void; m(a: T, b: 1): void }
declare let foo3: Foo3<S>;
let bar3: Bar3 = foo3;
interface Foo4 { m<U>(a: S, b: U): void; m<U>(a: S, b: U, c: U): void }
declare let foo4: Foo4;
let bar4: Bar = foo4;
