// @strict: true
type RD<T> = T extends ReadonlyArray<infer I> ? ReadonlyArray<RD<I>> : T;
type J = string | J[];
declare function f<I>(x: ReadonlyArray<RD<I>>): I;
declare const r0: RD<string[][]>;
const n = r0[0][0];
declare const r: RD<J>;
const x: number = 1;
