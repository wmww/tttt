type Grow<T, N extends unknown[]> = T extends N['length'] ? N : Grow<T, [...N, 0]>;
declare function f<T>(x: T): Grow<T, []>;
const a1 = f(2000 as const);
const a2 = f(2000 as const);
const a3: string = f(2000 as const);
interface Box<T> { m(): Grow<T, []>; n: { k: Grow<T, []> } }
declare const b: Box<3000>;
const b1 = b.n.k;
const b2: string = b.m();
const b3: string = b.n;
class C<T> { v!: Grow<T, []>; }
declare let c1: C<2000>;
declare let c2: C<1>;
c2 = c1;
c1 = c2;
