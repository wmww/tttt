// @allowJs: true
// @checkJs: true
// @noEmit: true
// A JSDoc type expression with nothing before the comment's end is TS1110 at the full start of its end (right after
// the `{`), then '}' expected at the end; the token after the `{` is scanned before tsc's parseJSDocType skips
// leading asterisks, so a line's `*` there is a JSDocAllType (missing_checks.md, cor8's ttttJsdocEmptyType leftovers)
// @filename: z.js
/** @type {
 */
var a = 1;
/** @type { */
var b = 1;
/** @type {
 * number} */
var c = 1;
/** @param {...
 */
function d(a) {}
/** @type {
 *   number } */
var e = 1;
