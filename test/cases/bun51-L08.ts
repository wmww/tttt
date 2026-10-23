// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowJs": true, "checkJs": true } }
// @filename: f.js
const k1 = "a", k2 = "b";
export class Q {
  static m() {
    /** @type {(x: number) => void} */
    this[k1] = (x) => {};
    this[k2] = (y) => {};
  }
}
export class R {
  static m() {
    this[k2] = (y) => {};
    /** @type {(x: number) => void} */
    this[k1] = (x) => {};
  }
}
/** @type {number} */
export const sentinel = "";
