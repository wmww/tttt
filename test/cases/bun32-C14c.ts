// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function gf<T>(): T;
function f(x: string | number) { if (typeof x === "string") { () => x.length; (x = 1, gf)<number>; } }
const sentinel: number = "";
