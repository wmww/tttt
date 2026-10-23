// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowJs": true, "checkJs": true } }
// @filename: a.js
/** @type {unknown} */
const v = null;
/** @param {unknown} x @returns {(asserts x is string)} */
function p1(x) {}
function t1() { p1(v); v.length; }
class K {
  /** @returns {(asserts this is L)} */
  n() {}
}
class L extends K { l = 1 }
/** @type {K} */
const k = new K();
function t2() { k.n(); k.l; }
/** @param {unknown} x @returns {(asserts x is string)} */
const p3 = x => {};
/** @type {number} */
const sentinel = "";
