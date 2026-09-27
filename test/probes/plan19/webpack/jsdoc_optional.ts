// @noEmit: true
// @allowJs: true
// @checkJs: true
// @strict: true
// @filename: a.js
/**
 * @typedef {object} K
 * @property {(size: number) => string=} formatSize
 * @property {function(number=): string=} g
 * @property {string|number=} u
 */
/** @param {K} k */
function f(k) {
  if (k.formatSize) { /** @type {string} */ const s = k.formatSize(1); }
  /** @type {null} */ const a = k.g;
  /** @type {null} */ const b = k.u;
}
