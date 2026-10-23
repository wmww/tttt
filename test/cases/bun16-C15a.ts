// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowJs": true, "checkJs": true } }
// @filename: b.js
const x = [1];
/** @param {string[]} rest */
function f1() { return x[arguments.length]; }
/** @param {string} n2 */
function f2() { return x[arguments[0]]; }
/** @param {string[]} rest */
function f3(a = arguments[0]) { return a; }
/** @param {string[]} rest */
function f4() { return { [arguments.length]: 1 }; }
/** @type {number} */
const sentinel = "";
