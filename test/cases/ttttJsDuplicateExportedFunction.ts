// @allowJs: true
// @checkJs: true
// @noEmit: true
// Exports of an ES module JavaScript file are checked for redeclarations like a TypeScript file's: TS2323 besides TS2393
// (tsc's checkExternalModuleExports)

// @filename: a.js
export function g(a) { }
export function g(b) {}
export var v = 1;
export var v = 2;
/** @typedef {number} T */
export const T = 1;
export function f() {}
f.x = 1;
