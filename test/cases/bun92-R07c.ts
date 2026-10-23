// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"],"allowJs":true,"checkJs":true } }
// @filename: a.js
/**
 * @typedef {{
 *   "a": 1,
 *   "a": 2
 * }} T
 */
/** @type {number} */
const sentinel = "";
