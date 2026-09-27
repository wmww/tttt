type A = {[x: string]: unknown} & {[x: number]: number; [x: symbol]: boolean};
type N = A[number];
type S = A[string];
type Simplify<T> = {[K in keyof T]: T[K]} & {};
type B = Simplify<A>;
declare const b: B;
const n: number = b[1];
