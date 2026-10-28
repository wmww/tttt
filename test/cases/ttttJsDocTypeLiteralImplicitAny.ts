// @allowJs: true
// @checkJs: true
// @strict: true
// @noEmit: true
// @filename: a.js
// A JSDoc type literal's member without a type is implicitly any in a checked JavaScript file (TS7008)
/** @type {{ a, b: 1 }} */
const o = { a: 1, b: 1 };
/** @typedef {{ c }} T */
