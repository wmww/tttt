// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface A { readonly "x-y": unique symbol }
const s1: { (e: A["x-y"]): void } = {};
const sentinel: number = "";
