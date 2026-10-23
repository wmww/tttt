// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowJs": true, "checkJs": true } }
// @filename: a.js
/** @param {number[]} q */
function f() { /** @type {typeof arguments} */ (0); }
/** @type {number} */
const sentinel = "";
