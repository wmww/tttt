// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowJs": true, "checkJs": true } }
// @filename: b.js
/** @param {string[]} rest */
function f1() { return class { arguments = 1 }; }
/** @param {string} n15 */
function f2() { return class { arguments = 1 }; }
/** @param {string[]} rest */
function f3() { arguments: while (1) { break arguments; } }
/** @type {number} */
const sentinel = "";
