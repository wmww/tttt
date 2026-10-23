// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface I { readonly u: unique symbol }
declare let i: I;
let s9: string = i.u;
const sentinel: number = "";
