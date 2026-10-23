// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es5"] } }
// @filename: a.ts
/// <reference lib="lib.dom.d.ts" />
export const p = document;
export const sentinel: number = "";
