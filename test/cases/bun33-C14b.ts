// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowJs": true, "checkJs": true } }
// @filename: j.js
exports.a = 1;
declare module "zz" { }
/** @type {number} */
const sentinel = "";
