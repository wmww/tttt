// @allowJs: true
// @checkJs: true
// @noEmit: true
// A JSDoc `@type {unique symbol}` where tsgo's reparser makes no annotation of it (an object literal property, an
// expando, a `this.x =`, a CommonJS export: nothing checks it) or makes it a getter's return type (TS1335)
// @filename: a.js
const o = {
    /** @type {unique symbol} */
    a: Symbol(),
    /** @type {unique symbol} */
    get b() { return Symbol(); },
};
function f() {}
/** @type {unique symbol} */
f.x = Symbol();
class C {
    constructor() {
        /** @type {unique symbol} */
        this.z = Symbol();
    }
    /** @type {unique symbol} */
    static get g() { return Symbol(); }
}
/** @type {unique symbol} */
module.exports.w = Symbol();
