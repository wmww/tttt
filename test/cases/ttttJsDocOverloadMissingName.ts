// @allowJs: true
// @checkJs: true
// @noEmit: true
// @noImplicitAny: false
// An `@overload` of a function declaration missing its name: tsgo's reparser reports the missing name again, on the
// character before it (checkNonIdentifierName), once however many overloads; not for `export default function ()`
// @filename: a.js
/** @overload */
function () {}
// @filename: b.js
/**
 * @overload
 * @param {string} a
 * @overload
 * @param {number} a
 */
function* /*c*/ () {}
// @filename: c.js
/** @overload */
export default function () {}
