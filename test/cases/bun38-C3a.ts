// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es5"] } }
// @filename: a.ts
/// <reference lib="es2022.full" />
export const p = document;
export const sentinel: number = "";
