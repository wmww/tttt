// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowJs": true, "checkJs": true } }
// @filename: a.js
/** @type {a.} */
var x = 1;
// @filename: b.ts
const sentinel: number = "";
