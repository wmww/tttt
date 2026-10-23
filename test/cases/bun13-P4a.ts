// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const xs: number[];
declare function f<T>(x: { a: T, b: T, c: unknown }): T;
f({ a: xs, b: ["s"], c: [1] });
const sentinel: number = "";
