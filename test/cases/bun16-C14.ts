// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function f<T extends string>(x: { p: `a${T}` }): T;
const r1: never = f({ p: "ab" });
declare function f4<T extends string>(x: { p: `a${T}` | number }): T;
const r4: never = f4({ p: "ab" });
const sentinel: number = "";
