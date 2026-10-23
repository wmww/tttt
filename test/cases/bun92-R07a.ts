// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"],"allowJs":true,"checkJs":true } }
// @filename: a.js
/**
 * @type {{ a: 1; static
 * {}; b: 2 }}
 */
const o = 0;
o.a; o.b; o.nope;
/** @type {number} */
const sentinel = "";
