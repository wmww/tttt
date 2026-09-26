// @strict: true
type Simplify<T> = {[K in keyof T]: T[K]};
declare const a: Simplify<{[x: string]: string; a: string}>;
declare const b: Simplify<{[x: string]: string} & {a: string}>;
type X = {[x: string]: string};
type Y = {a: "a"};
declare const c: Simplify<X & Y>;
