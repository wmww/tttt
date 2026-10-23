// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface I1 { a: "b"; [k1]: number }
declare const k1: I1["a"];
declare let i1: I1; i1.b;
const sentinel: number = "";
