// @allowJs: true
// @checkJs: true
// @strict: true
// @noEmit: true
// @target: es2017
// tsc: TS2565 at A.prototype.m only (tttt lacks that check: the flow of a property declared by an expression
// statement starting unassigned). The JSDoc before a host may be followed by other comments (getJSDocCommentRanges):
// `// eslint-disable-next-line` between `@this` and `const fn = function` (webpack deprecation.js)
// @filename: v.js
/**
 * @this {Set<number>} a Set
 * @returns {number | undefined} the value
 */
// eslint-disable-next-line func-style
const fn = function () {
	for (const item of this) return item;
};
/** @type {number} */
class A {}
/** @type {(x?: number) => void} */
A.prototype.m;
function F() {}
/** @type {(x?: number) => void} */
F.prototype.m;
new F().m(1);
