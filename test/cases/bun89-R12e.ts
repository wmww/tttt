// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowJs": true, "checkJs": true } }
// @filename: j.js
class J { constructor() { this.a = /** @type {"b"} */ ("b"); this[jk] = 1; } }
/** @type {J["a"]} */
const jk = "b";
new J().b;
/** @type {number} */
const sentinel = "";
