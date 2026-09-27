// @noEmit: true
// @allowJs: true
// @checkJs: true
// @strict: true
// @filename: a.js
class M { constructor() { this.e = 1; } }
/**
 * @this {M}
 * @returns {number}
 */
function errors() { return this.e; }
/** @type {string} */ const s = errors.call(new M());
const o = {
    /** @this {M} */
    g() { return this.e; }
};
/** @this {M} */
const h = function () {
    /** @type {string} */
    const z = this.e;
};
errors();
