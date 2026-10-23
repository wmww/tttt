// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type NTuple<T, N extends number, R extends unknown[] = []> = R["length"] extends N ? R : NTuple<T, N, [...R, T]>;
declare function fn<T extends readonly never[]>(): T;
function f<N extends number>() { return fn<[never, ...NTuple<never, N>]>; }
type Q<N extends number> = typeof fn<[never, never, ...NTuple<never, N>]>;
const sentinel: number = "";
