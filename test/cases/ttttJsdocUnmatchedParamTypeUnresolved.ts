// @allowJs: true
// @checkJs: true
// @noEmit: true

// tsgo never resolves the type of a `@param` tag that names no parameter (checkUnmatchedJSDocParameters), except
// the last one's when the body uses `arguments` (TS8029): no TS2304 for the others, nor with a JSDoc @type
// @filename: a.js
/**
 * @param {number} a
 * @param {Nope} b
 * @param {Array<Nope2>} c
 */
function f6(a) { return 42 }
function g(...args) {}
/**
 * @param {Nope3} x
 */
function h() { return arguments; }
/**
 * @type {(a: 1) => number}
 * @template T
 * @template U
 * @param {T} a
 * @param {U} b
 * @param {V} c
 */
function f5b(a) { return 42 }
/**
 * @template T
 * @param {T} a
 * @param {T} b
 */
function f6b(a) { return 42 }
