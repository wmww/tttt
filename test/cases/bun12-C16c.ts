// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare var Keys: K;
interface K { readonly a: unique symbol }
let s: string = Keys.a;
const sentinel: number = "";
