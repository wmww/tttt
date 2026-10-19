// @allowJs: true
// @checkJs: true
// @declaration: true
// @emitDeclarationOnly: true
// @module: commonjs
// A `@property` with a bracketed default holding brackets of its own, `[exclude=[]]`: its description starts after the
// outer `]` (markdown-magic's fileTree)

// @filename: a.js
/**
 * @typedef {Object} Options
 * @property {string[]} [exclude=[]] - Array of glob patterns to exclude from the tree.
 * @property {number} [depth=[1][0]] - Depth.
 */

/** @param {Options} o */
function tree(o) {}
module.exports = { tree };
