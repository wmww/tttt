// @noEmit: true
// @allowJs: true
// @checkJs: true
// @strict: true
// @filename: a.js
/** @type {object} */
var obj;
/** @type {?number} */
var nullable;
/** @type {number=} */
var opt;
/** @type {Array.<string>} */
var arr;
/**
 * @param {string} a
 * @param {number=} b
 * @param {...boolean} rest
 * @returns {string}
 */
function f(a, b, ...rest) { return a; }
f("x");
var c = /** @type {string} */ (obj);
