// @allowJs: true
// @checkJs: true
// @noUnusedLocals: true
// @noUnusedParameters: true
// @noEmit: true
// @filename: a.js
// A JSDoc type tsgo's reparser moves into the host (@type, @param, @returns) keeps its ThisNodeHasError: a missing
// type there (`{}`, `{string|}`) hides the host's and its enclosing declarations' unused diagnostics; an error
// after the type ('}' expected), in @typedef or @template doesn't (known failing: agents/bugs/bun_others.md)
function j01() {
    let a = 1;
    let b = (1 +);
    let c = 2;
}
function j02() {
    /** @type {string|} */
    let d = "";
    let e = 1;
}
function j03() {
    /** @param {number x */
    function inner(x) {}
    let f = 1;
}
/** @type {{ a: }} */
const j04 = { a: 1 };
function j05() {
    /** @typedef {Object} T
     * @property {strin g} p
     */
    let g = 1;
}
function j06(/** @type {number?} */ h, i) {}
class j07 {
    /** @type {} */
    #p = 1;
    #q = 2;
}
function j08() {
    /** @returns {} */
    function k() {}
    let l = 1;
}
function j09() {
    let m = 1; )
    let n = 2;
}
function j10() {
    /** @template T, */
    function t() {}
    let o = 1;
}
function j11(/** @type {} */ p) {}
/** @param {} q */
function j12(q, r) {}
function j13() {
    /** @param {} s */
    function inner2(s) {}
}
function j14() {
    /** @type {string|} */
    let u = "", v = 1;
}
