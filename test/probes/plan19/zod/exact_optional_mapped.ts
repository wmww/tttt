// @strict: true
// @exactOptionalPropertyTypes: true
type Partialish<T> = { [K in keyof T]?: T[K] };
type P = Partialish<{ name: string }>;
declare const p: P;
const a: { [x: string]: string } = p;
type Q = { name?: string };
declare const q: Q;
const b: { [x: string]: string } = q;
type Flat<T> = { [K in keyof T]: T[K] };
type R = Flat<{ name?: string } & { a: string }>;
declare const r: R;
const c: { [x: string]: string } = r;
