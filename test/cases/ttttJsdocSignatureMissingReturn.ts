// TS2355 for a function whose return type a written JSDoc `@type` signature gives is at the whole tag type (tsgo's
// FullSignature), a cast arrow's too; a getter's `@type` and a `@returns` stay at their type
// @allowJs: true
// @checkJs: true
// @noEmit: true
// @strict: true
// @filename: a.js
export default /** @type {(e: unknown) => e is string} */ (e) => {
};
/** @type {(e: unknown) => number} */
export function f(e) {
}
/** @type {(e: unknown) => number} */
export const g = function (e) {
    if (e) return 1;
};
/** @type {(e: unknown) => never} */
export const h = (e) => {
};
/** @returns {number} */
export function k() {}
class C {
    /** @type {number} */
    get x() { }
}
