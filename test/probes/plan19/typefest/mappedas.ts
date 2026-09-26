// @strict: true
type M<T> = { [K in keyof T as K]: T[K] };
declare const a: M<string>;
const s: string = a;
declare const b: M<{ x: 1 } | { y: 2 }>;
declare const c: M<number[]>;
