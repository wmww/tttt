// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "allowJs": true, "checkJs": true, "module": "commonjs", "types": [], "lib": ["es2022"] } }
// @filename: a.js
var p = r;
var r = [r, exports];
exports.y = exports.z;
exports.z = 1;
/** @type {never} */ var tp = p;
// @filename: b.js
var q = s;
var s = [s, require("./a").y, exports];
exports.k = 1;
/** @type {never} */ var tq = q;
/** @type {number} */
const sentinel = "";
