// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "commonjs", "allowJs": true, "checkJs": true } }
// @filename: a.ts
const sentinel: number = "";
// @filename: ii.js
const { J } = require("./jj");
class I extends J {}
module.exports.I = I;
// @filename: jj.js
const { I } = require("./ii");
class J extends I {}
module.exports.J = J;
