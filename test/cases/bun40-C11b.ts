// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] }, "files": ["a.ts"] }
// @filename: a.ts
/// <reference path="./h.JS" />
const sentinel: number = "";
// @filename: h.js
var h = 1;
