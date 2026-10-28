// @allowJs: true
// @checkJs: true
// @strict: true
// @noEmit: true
// @filename: a.js
// A JSDoc type's name isn't checked as a strict-mode identifier (tsc's checkStrictModeIdentifier skips JSDoc nodes):
// `@type {yield}` in a generator is only TS2304
function* f() {
    /** @type {yield} */
    const x = 1;
}
