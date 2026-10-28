// The JSDoc @type arity count (tsc's isAritySmaller) is one function for both the parse-time and the checker-time test:
// a `this` parameter or a JSDoc `@this` aside, `?`, defaults, rests and JSDoc optional types `{T=}` end the required ones
// @allowJs: true
// @checkJs: true
// @noEmit: true
// @strict: true
// @filename: a.js
/** @type {(a: string) => void} */
function f1(a, b) {}
/** @type {(a: string) => void} */
function f2(a, /** @type {string=} */ b) {}
/**
 * @this {{x: number}}
 * @type {(a: string) => void}
 */
function f3(a) {}
/**
 * @param {string} a
 * @param {string=} b
 * @type {(a: string) => void}
 */
function f4(a, b) {}
/**
 * @this {{x: number}}
 * @param {number} a
 */
function f5(a) {}
/** @type {(a: string) => void} */
const g1 = function (a, b) {};
/** @type {(a: string) => void} */
const g2 = function (a, /** @type {string=} */ b) {};
// @filename: b.js
/** @typedef {(a: string) => void} F */
/** @type {F} */
function f1(a, b) {}
/** @type {F} */
function f2(a, /** @type {string=} */ b) {}
/**
 * @param {string} a
 * @param {string=} b
 * @type {F}
 */
function f4(a, b) {}
/**
 * @this {{x: number}}
 * @type {F}
 */
function f3(a) {}
/** @type {F} */
function f6(a, /** @type {string=} */ b) { a.length; }
/** @type {F} */
function f7(a, b) { a.length; }
