// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"],"allowJs":true,"checkJs":true } }
// @filename: a.js
/** @type {{ a: 1; [}} */
const o = 0;
o.a; o.nope;
/** @type {number} */
const sentinel = "";
