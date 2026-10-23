// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "allowJs": true, "checkJs": true, "types": [], "lib": ["es2022"] } }
// @filename: a.js
/** @param {never} x */
function nv(x) {}
class A1 { constructor() { this.a = null; this.a = "s"; } a; }
class A2 { a; constructor() { this.a = null; this.a = "s"; } }
class A3 { constructor() { this.a = 1; nv(this.a); this.a = "s"; nv(this.a); } a; m() { nv(this.a); } }
/** @type {number} */
const sentinel = "";
