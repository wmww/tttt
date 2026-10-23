// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "allowJs": true, "checkJs": true, "types": [], "lib": ["es2022"] } }
// @filename: a.js
const k = "foo";
class C5 { constructor() { this.foo = 1; } [k]() { return ""; } }
/** @type {number} */
const sentinel = "";
