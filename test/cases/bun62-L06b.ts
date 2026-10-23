// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "allowJs": true, "checkJs": true, "types": [], "lib": ["es2022"] } }
// @filename: a.js
const k = "x";
const o = { [k]: 1, m() { this.x = 2; } };
/** @type {boolean} */ const a = o;
const o2 = { [k]: 1, n: 1, get g() { this.x = 2; return 1; } };
/** @type {boolean} */ const d = o2;
/** @type {{ m(): void, x: string, n: string }} */ const e = o2;
/** @type {number} */
const sentinel = "";
