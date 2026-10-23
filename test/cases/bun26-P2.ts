// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type Inf<T> = T extends string ? Inf<`a${T}`> : never;
declare function f<A, B>(x: A): void;
function g<T>() { return f<T, Inf<T>>; }
g<string>();
const sentinel: number = "";
