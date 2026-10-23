// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "allowJs": true, "checkJs": true, "types": [], "lib": ["es2022"] } }
// @filename: j.js
/** @type {{ m(): void, n: () => void }} */
const o = { m(a) {}, n(a) {} };
/** @param {{ m(): void }} x */
function take(x) {}
take({ m(a) {} });
/** @type {number} */
const sentinel = "";
