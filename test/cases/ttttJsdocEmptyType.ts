// @allowJs: true
// @checkJs: true
// @noEmit: true
// @strict: true
// A JSDoc tag whose braces hold no type (`@type {}`, TS1110) has tsc's missing type, an error any: w and the
// parameters are any (no TS2322, no TS7006), `@satisfies {}` is any, a `@template {}` constraint any
// @filename: b.js
/** @type {} */
var w = 1;
w = "s";
/** @param {} p */
function f(p) { return p; }
/** @returns {} */
function g() { return 1; }
/** @typedef {} T */
/** @type {T} */
var t = 1;
/** @typedef {Object} O
 * @property {} x
 */
/** @type {O} */
var o = { x: 1 };
/** @template {} U
 * @param {U} u */
function h(u) { return u; }
const s = /** @satisfies { } */ ({ a: 1 });
/** @param {} [q] */
function k(q) { return q; }
/** @param {...} r */
function m(r) { return r; }
/** @type {?} */
var n1 = 1;
