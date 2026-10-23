// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowJs": true, "checkJs": true } }
// @filename: a.js
/** @param {number[]} q */
function f(a = arguments) {}
/** @type {number} */
const sentinel = "";
