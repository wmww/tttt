// @allowJs: true
// @checkJs: true
// @declaration: true
// @declarationMap: true
// @emitDeclarationOnly: true
// @module: esnext
// A qualified JSDoc name whose left is an `@import` alias is reused as written (eslint-plugin-jsdoc's
// `@returns {ESLint.Plugin}`; a bare one names what the alias resolves to), and an `@import` declaration's range is its
// tag's: to the comment's end past a line break, else to its last token

// @filename: core.ts
export interface Plugin { name: string }
// @filename: m.ts
import type { Plugin as P } from './core.js';
export namespace ESLint { export type Plugin = P; }
export namespace NS { export interface I { a: number } }

// @filename: a.js
/**
 * @import {
 *   ESLint,
 * } from './m.js';
 * @import { NS } from './m.js'
 */
/** @import * as M from './m.js' */

/**
 * @param {number} [options]
 * @returns {ESLint.Plugin}
 */
export const getPlugin = (options = 1) => {
  return { name: '' };
};

/** @returns {M.NS.I} */
export function h() { return { a: 1 }; }
