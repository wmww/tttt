type P<T> = { [K in keyof T]: T[K] };
const f: P<{ key?: string } & { b?: 1 }> = {};
interface A { lang?: string; k: 1 }
interface B { lang?: string; k: 2 }
declare const a: Omit<A, 'k'>;
const b: Omit<A | B, 'k'> = a;
interface C { x?: 1; y: 1 } interface D { x: 1; y?: 1 }
type U = Pick<C | D, 'x' | 'y'>;
type I = P<C & D>;
declare const u: U; declare const i: I;
