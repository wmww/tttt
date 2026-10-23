// @filename: tsconfig.json
{ "compilerOptions": { "strict": false, "noEmit": true, "allowJs": true, "checkJs": true, "module": "commonjs", "types": [], "lib": ["es2022"] } }
// @filename: f.js
var v = mk().x;
function mk() { return new C(); }
class C { get x() { return [module, this.x]; } }
v.nope;
module.exports = C;
/** @type {number} */
const sentinel = "";
