// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const k = "p";
interface I { [k]: number }
interface I { readonly p: number }
const sentinel: number = "";
