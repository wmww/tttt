// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function gf<X>(): X;
interface I1<T = typeof gf<I1>> { x: T }
const sentinel: number = "";
