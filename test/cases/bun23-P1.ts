// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const s: { (x: { readonly k: unique symbol }): void } = {};
const sentinel: number = "";
