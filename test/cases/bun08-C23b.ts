// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "commonjs", "allowJs": true, "checkJs": true } }
// @filename: a.js
const { F } = require("./lib");
/** @type {F.Inner} */
let t;
/** @type {number} */ const sentinel = "";
// @filename: lib.js
function F() {}
module.exports = { F };
