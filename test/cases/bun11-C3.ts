// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowJs": true, "checkJs": true } }
// @filename: a.ts
const sentinel: number = "";
// @filename: j.js
/** @type {() => Generator<number, string, boolean>} */
function* f13() { const x = yield "a"; return "s"; }
