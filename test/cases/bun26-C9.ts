// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type A = string | boolean;
declare const x: A, c: boolean;
let v = c ? [1] : x;
const n: number = v;
const sentinel: number = "";
