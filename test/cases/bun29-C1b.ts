// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface I { readonly a: unique symbol }
let n: number = null! as I["a"];
const sentinel: number = "";
