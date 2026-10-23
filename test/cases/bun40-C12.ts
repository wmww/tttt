// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
/// <reference lib="lib.dom.d.ts" />
const d: Document = 1;
const sentinel: number = "";
