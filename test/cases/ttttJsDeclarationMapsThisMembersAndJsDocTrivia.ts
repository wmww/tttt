// @allowJs: true
// @checkJs: true
// @declaration: true
// @declarationMap: true
// @emitDeclarationOnly: true
// @strict: true
// @target: es2022
// @module: esnext
// JavaScript declaration maps (rocket, eslint-plugin-jsdoc): a `this.x = v` member's name is mapped (the synthesized
// property isn't); a JSDoc `[name]` gives an initialized parameter its tag as `?` too; a JSDoc type literal's member
// ends at its last token, not past the next line's `*`, and one that doesn't start with its name starts at its line's
// `*`; a JSDoc signature's last parameter ends at its last token

// @filename: a.js
export class MainMenu {
  constructor() {
    /** @type {string} */
    // @ts-ignore
    this.pageTree = undefined;
    this.currentPath = '';
    this["a-b"] = 1;
  }
}

/**
 * @param {string} a
 * @param {Object} [options]
 * @param {string} [options.title]
 * @param {number} [options.menu] render
 */
export function document(a, { title, menu } = {}) {}

/**
 * @param {number} [b]
 */
export function h(b = 1) {}

/**
 * @typedef {{
 *   value: string
 * }} ValueObject
 */

/**
 * @typedef {{
 *   m(): void,
 *   (): void,
 *   readonly x: number,
 *   new (): string,
 *   props: {
 *     [key: string]: ValueObject|null,
 *   },
 *   exported?: boolean
 * }} T
 */

/**
 * @param {T} x
 * @returns {ValueObject}
 */
export function f(x) { return { value: "" }; }

/** @type {(a: string, b: number
 * ) => void} */
export const g = (a, b) => {};
