// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const k = "kk";
function F() {}
F[k] = { a: F.kk };
const sentinel: number = "";
