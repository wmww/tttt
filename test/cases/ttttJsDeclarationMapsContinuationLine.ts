// @allowJs: true
// @declaration: true
// @declarationMap: true
// @emitDeclarationOnly: true
// @strict: true
// @target: es2022
// @module: esnext
// A JSDoc type starting on a continuation line of its comment is mapped from that line's `*` (tsgo maps
// skipTrivia(pos) and the `*` isn't trivia; create-t3-app's prettier.config.mjs)

// @filename: b.ts
export interface A { a: number } export interface B { b: number } export interface C { c: number }

// @filename: a.js
/**
 * @type {import('./b').A & import('./b').B &
 *       import("./b").C}
 */
const config = { a: 1, b: 1, c: 1 };
export default config;
/**
 * @param {string |
 *   number} x
 * @returns {Array<
 *   string>}
 */
export function f(x) { return []; }
