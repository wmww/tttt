// `unique symbol` in JSDoc types: an `@type` or `@property` type may be one as a whole (its host decides, as for a
// written annotation: TS1332/TS1331/TS1330), any other tag's, a cast's or a nested one is TS1335 (tsgo reparses the tags
// into the tree and checkGrammarTypeOperatorNode looks at the parent)
// @allowJs: true
// @checkJs: true
// @noEmit: true
// @filename: a.js
/** @param {unique symbol} x */
function f(x) {}
/** @type {unique symbol[]} */
const u = [];
/** @type {unique symbol} */
var v = Symbol(), w = 1;
for (/** @type {unique symbol} */ const k of []) {}
class C {
    /** @type {unique symbol} */
    static p = Symbol();
    /** @type {unique symbol} */
    static readonly = Symbol();
}
/** @type {{ a: unique symbol }} */
const o = { a: Symbol() };
/** @returns {unique symbol} */
function g() { return Symbol(); }
/** @typedef {unique symbol} T */
/**
 * @typedef {Object} O
 * @property {unique symbol} q
 */
/** @type {unique symbol} */
export default Symbol();
/** @type {(x: unique symbol) => void} */
const h = (x) => {};
const z = /** @type {unique symbol} */ (Symbol());
/** @type {unique symbol | number} */
const un = 1;
/** @type {(unique symbol)} */
const par = Symbol();
