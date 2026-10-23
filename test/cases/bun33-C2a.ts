// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowJs": true, "checkJs": true } }
// @filename: a.js
/** @type {number} */ let x;
() => x;
exports.a = 1;
/** @type {number} */
const sentinel = "";
