// @strict: true
type RD<T> = T extends ReadonlyArray<infer I> ? ReadonlyArray<RD<I>> : T;
type J = string | J[];
declare const r: RD<J>;
const x: number = 1;
