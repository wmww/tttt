// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowJs": true, "checkJs": true, "module": "commonjs" } }
// @filename: a.js
var o = /** @type {any} */ (null);
with (o) { var r1 = require("./nope1"); }
r1;
with (o) { var { r2 } = require("./nope2"); }
r2;
with (o) { var r3 = require("./nope3"); }
/** @type {number} */
var sentinel = "";
