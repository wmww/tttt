// @allowJs: true
// @declaration: true
// @declarationMap: true
// @emitDeclarationOnly: true
// @strict: true
// @target: es2022
// @module: esnext
// JSDoc nodes in declaration maps: a type literal of nested `@param`/`@property` tags starts at its first tag's `@`
// and ends where its last tag does (at the next tag's `@`, the comment's `*/`, or right after a bracketed name with
// nothing after it), a bracketed tag is its member's question token, a `@typedef` ends with its literal or at its
// name, a `@callback`'s signature spans its tags, and `*` is an `any` there

// @filename: b.ts
export interface X { a: number }

// @filename: a.js
/**
 * @param {Object} opts - Options for building configurations.
 * @param {string[]} opts.entry - The entry array.
 * @returns {import('./b').X}
 */
export function modernConfig(opts) {
  return { a: opts.entry.length };
}

/**
 * @param {Object} o
 * @param {string} o.x
 * @param {Object} o.y
 * @param {number} o.y.z */
export function f(o) { return o; }

/**
 * @param {Object[]} arr - list
 * @param {string} arr[].n
 */
export function h(arr) {}

/**
 * @param {Object} [o]
 * @param {string} [o.x="a"]
 */
export function k(o) {}

/**
 * @typedef {Object} A
 * @property {number} [b]
 */
/**
 * @typedef {Object} B
 * @property {number} [b]
 * @see x
 */
/**
 * @typedef {Object} C
 * @property {number} b
 */
/**
 * @typedef {Object} D
 * @property {number} [b=1]
 * @property {number} c */
/**
 * @typedef {string} S
 */
/** @typedef {number} N */
/**
 * @callback Cb
 * @param {Object} e
 * @param {string} e.k
 * @returns {void}
 */
/**
 * @callback Cc
 * @param {string} [e]
 */

/**
 * @param {*} a
 * @returns {*}
 */
export const g = (a) => a;
