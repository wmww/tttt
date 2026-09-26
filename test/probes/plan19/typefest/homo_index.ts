// @strict: true
type Simplify<T> = {[K in keyof T]: T[K]};
type D = {[x: string]: string | undefined; a: string};
declare const s: Simplify<D>;
const r: {a: string} = s;
const r2: {a: string} = {} as Simplify<{[x: string]: string; a: string}>;
