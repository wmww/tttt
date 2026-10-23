// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "allowJs": true, "checkJs": true, "types": [], "lib": ["es2022"] } }
// @filename: a.js
/** @overload */
function () {}
// @filename: b.js
/**
 * @overload
 * @param {string} a
 * @overload
 * @param {number} a
 */
function   () {}
