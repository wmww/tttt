// @noEmit: true
// @allowJs: true
// @checkJs: true
// @strict: true
// @filename: a.js
/**
 * @overload
 * @returns {number}
 */
/**
 * @overload
 * @param {string} s
 * @returns {string}
 */
/**
 * @param {string=} s
 * @returns {string | number}
 */
function f(s) { return s === undefined ? 1 : s; }
/** @type {number} */ const a1 = f();
/** @type {string} */ const a2 = f("");
/** @type {boolean} */ const a3 = f;

class C {
    /**
     * @overload
     * @returns {number}
     */
    /**
     * @overload
     * @param {string} s
     * @returns {string}
     */
    /**
     * @param {string=} s
     * @returns {string | number}
     */
    m(s) { return s === undefined ? 1 : s; }
}
/** @type {number} */ const b1 = new C().m();
/** @type {string} */ const b2 = new C().m("");

/**
 * @overload
 * @returns {number}
 */
/**
 * @overload
 * @param {string} s
 * @returns {string}
 */
/**
 * @param {string=} s
 * @returns {string | number}
 */
const g = (s) => s === undefined ? 1 : s;
/** @type {number} */ const c1 = g();

/**
 * @template T
 * @overload
 * @param {T} x
 * @returns {T}
 */
/**
 * @param {unknown} x
 * @returns {unknown}
 */
function h(x) { return x; }
/** @type {number} */ const d1 = h(1);
/** @type {string} */ const d2 = h(1);
