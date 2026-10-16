// @declaration: true
// @allowJs: true
// @checkJs: true
// @strict: true
// @outDir: ./out
// JavaScript declaration emit of JSDoc (plan 64 stage 6): a @typedef's @property descriptions become member comments
// (the continuation lines' margin: the description's column, or the tag's where it starts on a later line), a JSDoc
// namespace's members keep `export`; annotations go through the node builder (mapped types and tuples on one line),
// a type alias's type through the transformer
// @filename: a.js
/**
 * @typedef {Object} Opts
 * @property {string} a The a
 *   continued here
 *     indented more
 * @property {string} [b=x] - with {@link Opts} default
 *
 * @property {number} c
 *    starts on next line
 * @property {Object} d The d
 * @property {string} d.e The e
 * @property {{ [K in "x"|"y"]: number }} m mapped
 * @prop {number} f Last one */
/** @type {Opts} */
export const o = /** @type {any} */ (null);
/** @type {{ [K in "a"|"b"]: number }} */
export const x = /** @type {any} */ (null);
/**
 * @param {{ [K in "a"]: [a: string, b: number] }} p
 * @returns {{ [K in "a"]: { q: number } }}
 */
export function f(p) { return /** @type {any} */ (p); }
/** @typedef {{ [K in "a"]: number }} M */
/** @typedef {[a: string,
 *   b: number]} T */
/** @typedef {number} NS.Inner */
/**
 * @callback Cb
 * @param {string} [a-b]
 * @returns {void}
 */
