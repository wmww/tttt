// @allowJs: true
// @checkJs: true
// @declaration: true
// @emitDeclarationOnly: true
// A run of three or more backticks opens or closes a fenced code block, in which an `@` starts no tag (tsgo's
// inFencedCodeBlock: the description's, then each tag comment's own); a single backtick protects an `@` up to the end
// of its line in the description too (markdown-magic's "(``` or ~~~)" description hid its tags)

// @filename: a.js
/**
 * Preserves (``` or ~~~).
 * @param {string} content - c
 * @returns {string} r
 */
export function f1(content) { return content; }

/**
 * Unclosed `tick here @param {number} x
 * @param {string} y - c
 */
export function f2(x, y) { }

/**
 * Inline `@param {number} q` mention @param {number} z
 * @param {string} w - c
 */
export function f3(z, w) { }

/**
 * @param {string} a - example:
 * ```js
 * @param {number} b
 * ```
 * @param {boolean} c
 */
export function f4(a, b, c) { }

/**
 * Fence
 * ```
 * @param {number} d
 * ```
 * @param {boolean} e
 */
export function f5(d, e) { }
