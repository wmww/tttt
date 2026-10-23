// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowJs": true, "checkJs": true, "noUnusedLocals": true } }
// @filename: f.js
/** @param {number} z */ function j(z) { const arguments = 1; return z; }
/** @param {number} z */ function j3(z) { var arguments = 1; return z; }
/** @param {number} z */ function j8(z) { const { a: arguments } = { a: 1 }; return z; }
/** @param {number} z */ function j5(z) { class arguments {} return z; }
function k(z) { const arguments = 1; return z; }
j(1); j3(1); j8(1); j5(1); k(1);
/** @type {number} */
const sentinel = "";
export { sentinel };
