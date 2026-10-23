// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "allowJs": true, "checkJs": true, "module": "commonjs", "types": [], "lib": ["es2022"] } }
// @filename: a.js
var p = r;
var r = [r, require("./b").y];
exports.k = 1;
/** @type {never} */ var tp = p;
/** @type {number} */
const sentinel = "";
// @filename: b.js
exports.y = exports.z;
exports.z = 1;
var p = r;
var r = [r, exports];
/** @type {never} */ var tp = p;
