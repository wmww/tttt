// @allowJs: true
// @checkJs: true
// @noEmit: true
// @filename: a.js

// tsgo 7.1's checkVariableLikeDeclaration checks every initializer against the declared type, an empty object
// literal of a variable with exports (a JSDoc namespace member, expandos) included: tsc 5's isJSObjectLiteralInitializer
// exemption is gone
const t2 = /** @type {never} */ (1);
const t3 = {};
/** @typedef {boolean} t3.input */
/** @type {t3.input} */
const z = 1;

/** @type {{ a: number }} */
const x = {};
x.a = 1;

/** @type {{ b: number }} */
const y = {};
/** @typedef {string} y.inner */
