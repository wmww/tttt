// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare var K: KCtor;
interface KCtor { readonly k: unique symbol }
let n: number = K.k;
const sentinel: number = "";
