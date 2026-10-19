// @allowJs: true
// @checkJs: true
// @declaration: true
// @declarationMap: true
// @emitDeclarationOnly: true
// JSDoc maps: `T=` ends past its `=` (rocket's `@property {string=} title`); a type literal member with a string
// literal name and a `...rest` parameter first on a continuation line start at the line's `*` (eslint-plugin-jsdoc)

// @filename: a.js
/**
 * @typedef {{
 *   n?: number,
 *   'ts-check'?: string,
 * }} OptionsShape
 */
/**
 * @typedef {(
 *   tag: string,
 *   ...tokens: number[]
 * ) => void} ChangeTag
 */
/**
 * @typedef {object} P
 * @property {string} path
 * @property {string=} title
 */
/** @param {P} p @param {number=} n */
export function f(p, n) {}
