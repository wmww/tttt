// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type T = { a: "b"; [k]: number };
declare const k: T["a"];
declare let t: T; t.c;
const sentinel: number = "";
