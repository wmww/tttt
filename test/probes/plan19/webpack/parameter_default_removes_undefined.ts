// @strict: true
// @noEmit: true
// @allowJs: true
// @checkJs: true
// tsc: only d.ts line 2 (TS18048). A parameter with a default starts without the undefined of its declared type
// (removeOptionalityFromDeclaredType): whatever typed it (annotation, context, JSDoc `@type` tag), also in closures
// @filename: d.ts
function f(x: number | undefined = 1) { [1].map(() => x + 1); }
function g(x: number | undefined = 1) { x = undefined; [1].map(() => x + 1); }
// @filename: c.js
/**
 * @callback Fn
 * @param {string} data
 * @param {number=} curDepth
 * @returns {number[] | null}
 */
/** @type {Fn} */
const k = function (data, curDepth = 1) {
	return [1].map(() => curDepth + 1);
};
const h = (/** @type {number} */ e) =>
	/** @type {Fn} */
	function k(data, curDepth = 1) {
		if (curDepth > e) return null;
		return [curDepth];
	};
