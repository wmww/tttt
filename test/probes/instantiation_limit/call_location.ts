type Grow<T, N extends unknown[]> = T extends N['length'] ? N : Grow<T, [...N, 0]>;
interface Api<T> { get(): Grow<T, []>; x: Grow<T, []>; y: T; }
declare const a: Api<2000>;
const r1 = a.get();
const r2 = a.get();
const x1 = a.x;
const x2 = a.x;
const y1 = a.y;
declare const b: Api<2000>;
const x3 = b.x;
const r3 = b.get();
