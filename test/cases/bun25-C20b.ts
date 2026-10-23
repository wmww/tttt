// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowJs": true, "checkJs": true } }
// @filename: a.js
const [x] = require("./m");
/** @type {number} */ const sentinel = "";
// @filename: m.js
module.exports = [1];
