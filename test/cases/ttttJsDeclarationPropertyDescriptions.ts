// @allowJs: true
// @declaration: true
// @emitDeclarationOnly: true
// @strict: true
// @target: es2022
// @module: esnext
// A `@typedef`'s `@property` descriptions are JSDoc comments on its members (tsgo's preservePartialJsDoc of the
// property signatures the transformer visits: not in nested literals, function or callback parameters), collected
// like tsgo's parseTagComments: later lines lose their `*` and their indentation up to the description's column, a
// description starting on the next line keeps what's past the tag's column (base-ui's rehypeSlug.mjs)

// @filename: a.js
/**
 * @typedef {Object} T
 * @property {string} a - the a
 *   continued   here
 *      more indented
 * @property {Object} [b] b desc
 * @property {number} b.c - nested desc
 * @property {number} d
 *   next line desc
 * @property {number} e {@link T} linked
 * @property {string} f first
 *
 *   after blank
 * @property {string} [g="x y"] defaulted
 * @property {string} h
 * @property {string} i desc */
/** @typedef {Object} U
 * @property {string} a one line */
/**
 * @typedef {Object[]} V
 * @property {string} a in array
 */
/**
 * @typedef Nested
 * @property {Object} oh
 * @property {number} oh.no
 */
/**
 * @callback Cb
 * @param {Object} e - e desc
 * @param {string} e.k - k desc
 * @returns {void}
 */
/**
 * @param {Object} o - o desc
 * @param {string} o.x - x desc
 */
export function f(o) {}
/**
 * @typedef Options
 *   Configuration (optional).
 * @property {string} [prefix='']
 *   Prefix to add in front of `id`s (default: `''`).
 *     and more
 */
