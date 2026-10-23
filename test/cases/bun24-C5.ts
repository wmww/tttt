// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const t: { (b: { readonly s: unique symbol }): void; (q: 1): 1 } = {};
const sentinel: number = "";
