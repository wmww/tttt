// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "allowJs": true, "checkJs": true, "types": [], "lib": ["es2022"] } }
// @filename: a.js
const k = "x";
class D1 { static [k] = 1; static m() { this.x = 2; } }
class D2 { static m() { this.x = 2; } static [k] = 1; }
class D3 { constructor() { this.x = 2; } /** @readonly */ [k] = 1; }
class D4 { constructor() { this.x = 2; } /** @private */ [k] = 1; }
class D5 { constructor() { this.x = 2; } [k] = 1; }
/** @type {number} */
const sentinel = "";
