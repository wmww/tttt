// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const s1: { <T extends { readonly k: unique symbol }>(x: T): void } = {};
const sentinel: number = "";
