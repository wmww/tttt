// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface SymbolConstructor { readonly ["m7"]: symbol; readonly "m8": symbol }
let t: boolean = Symbol.m7;
t = Symbol.m8;
const sentinel: number = "";
