// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export default function f() {}
export default interface I {}
let v: I;
const sentinel: number = "";
