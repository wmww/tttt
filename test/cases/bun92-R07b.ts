// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"],"allowJs":true,"checkJs":true } }
// @filename: a.js
/**
 * @param {{ a: 1; static
 * {}; b: 2 }} o
 */
function f(o) { o.a; o.b; o.nope; }
/** @type {number} */
const sentinel = "";
