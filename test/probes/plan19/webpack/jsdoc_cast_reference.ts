// @strict: true
// @noEmit: true
// @allowJs: true
// @checkJs: true
// tsc: clean. A JSDoc cast `/** @type {W} */ (error)` is a ParenthesizedExpression to tsc: `(cast).chunk` is a
// narrowable reference matching `error.chunk` (isNarrowableReference, isMatchingReference)
// @filename: e.js
/** @typedef {{ chunk?: { name: string } }} W */
/** @param {string | W | Error} error */
function f(error) {
	if (typeof error !== "string") {
		if (/** @type {W} */ (error).chunk) {
			const chunk = /** @type {W} */ (error).chunk;
			chunk.name;
		}
	}
}
