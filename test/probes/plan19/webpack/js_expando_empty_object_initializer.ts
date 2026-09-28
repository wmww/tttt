// @allowJs: true
// @checkJs: true
// @strict: true
// @noEmit: true
// @filename: a.js
/** @typedef {{ start: number, end: number }} L */
function f() {
    /** @type {L} */
    const loc = {};
    loc.start = 1;
    /** @type {L} */
    let m = {};
    if (Math.random()) m.end = 2;
    /** @type {L} */
    const n = {};
    return [loc, m, n];
}
