// @strict: true
type Simplify<T> = {[K in keyof T]: T[K]} & {};
type D = {[x: string]: number; a: 1};
declare const s: Simplify<D>;
const v = s.a;
