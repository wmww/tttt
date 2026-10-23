// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowJs": true, "checkJs": true } }
// @filename: a.js
/** @type {number[]} */
const x1 = [/** @type {string} */ ("")];
/** @type {number[]} */
const y2 = [/** @type {string} */ (/** @type {any} */ (""))];
/** @type {number} */
const sentinel = "";
