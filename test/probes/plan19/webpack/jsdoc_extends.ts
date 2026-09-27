// @noEmit: true
// @allowJs: true
// @checkJs: true
// @strict: true
// @filename: a.js
/** @template T */
class Base {
    /** @param {T} x */
    constructor(x) { this.x = x; }
}
/** @extends {Base<string>} */
class D extends Base {
    constructor() { super(""); }
}
/** @type {number} */ const n = new D().x;
/**
 * @template U
 * @extends {Base<U[]>}
 */
class E extends Base {
    /** @param {U} u */
    constructor(u) { super([u]); }
}
/** @type {number} */ const m = new E(1).x;
/** @extends {Base<number>} */
module.exports = class F extends Base {};
