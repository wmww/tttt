// @allowJs: true
// @checkJs: true
// @noEmit: true

// A JavaScript `@typedef` without a name is a parse error of the file in tsgo (the reparser's checkNonIdentifierName,
// among the file's parse diagnostics, not its JSDoc ones), so the checker's grammar errors aren't reported there
// (hasParseDiagnostics): no TS1162. Another JSDoc error (`@type {}`) doesn't count
// @filename: a.js
var a = { x?: 1 };
/**
 * @typedef {string}
 */
var z = 1;
// @filename: b.js
var b = { y?: 1 };
/** @type {} */
0;
