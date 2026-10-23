// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type NTuple<T, N extends number, R extends unknown[] = []> = R["length"] extends N ? R : NTuple<T, N, [...R, T]>;
class Base<T extends readonly never[]> { x!: T }
class Derived<N extends number> extends Base<[never, ...NTuple<never, N>]> {}
function f<N extends number>() { return class extends Base<[never, never, ...NTuple<never, N>]> {}; }
const sentinel: number = "";
