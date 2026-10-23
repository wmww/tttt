// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "allowJs": true, "checkJs": true, "useDefineForClassFields": false, "types": [], "lib": ["es2022"] } }
// @filename: a.js
/** @param {number} a @param {number} extra */
function g3(a) { return class { arguments = 1; constructor(arguments) {} }; }
/** @param {number} a @param {number} extra */
function g1(a) { return class { arguments = 1; }; }
/** @type {number} */
const sentinel = "";
