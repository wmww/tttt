// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowJs": true, "checkJs": true } }
// @filename: f.js
export const store = {
  /** @param {number} n */
  format(n) { return String(n); },
  pair: /** @type {[number, number]} */ ([1, 2]),
  reset() {
    this.format = (n) => n.toFixed(2);
    this.pair = [3, 4];
  },
};
export const later = {
  reset() {
    this.format = (n) => String(n);
  },
  /** @param {number} n */
  format(n) { return String(n); },
};
/** @type {number} */
export const sentinel = "";
