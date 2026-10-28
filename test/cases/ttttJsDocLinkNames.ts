// @allowJs: true
// @checkJs: true
// @noEmit: true
// The name of a JSDoc `{@link}` is parsed (tsc's parseJSDocLinkName): a member after `#` is an identifier, so a
// reserved word is TS1359; a `.` needs a name after it; a `#` before no name is an invalid character. In JS only
// @filename: a.js
/** Removes it, like {@link Map#delete}. See {@link Promise#catch} and {@link Map#get}. */
export const q1 = 2;
/** {@link Map.} */
export const q2 = 2;
/** {@link Map#} */
export const q3 = 2;
/** @param {number} x {@linkcode Set#delete} {@linkplain Array#with} */
export function q4(x) { return x; }
/** {@link-x Map#delete} {@link Map.prototype.get} `{@link Map#default}` */
export const q5 = 2;
// @filename: b.ts
/** {@link Map#delete} {@link Map.} */
export const t1 = 2;
