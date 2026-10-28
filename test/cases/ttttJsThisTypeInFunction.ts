// @allowJs: true
// @checkJs: true
// @noEmit: true
// A JSDoc `this` type in a plain JavaScript function, its signature or its body, and in a prototype method: tsgo 7 has
// no constructor-function classes, so none is a class's `this` (TS2526 each)
// @filename: a.js
/** @param {this} x */
function F(x) {
    /** @type {this} */
    const self = x;
    this.a = self;
}
/** @returns {this} */
F.prototype.m = function () { return this; };
const o = {
    /** @param {this} y */
    n(y) { return y; },
};
