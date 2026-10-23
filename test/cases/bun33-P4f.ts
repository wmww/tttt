// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowJs": true, "checkJs": true } }
// @filename: a.js
/** @param {number[]} q */
function f() { const { arguments: z } = { arguments: 1 }; }
/** @type {number} */
const sentinel = "";
