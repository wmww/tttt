// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const k: unique symbol;
declare const a: { readonly [k]: unique symbol };
const b: { readonly [k]: unique symbol } = a;
const sentinel: number = "";
