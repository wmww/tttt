// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowJs": true, "checkJs": true } }
// @filename: b.js
/**
 * @type {(this: { a: number }) => void}
 * @this {{ b: number }}
 */
function both() { this.a; this.b; }
/** @type {number} */
const sentinel = "";
