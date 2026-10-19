// @allowJs: true
// @checkJs: true
// @declaration: true
// @emitDeclarationOnly: true
// @strict: true
// A `@property {function(A): B} name` (closure syntax tsgo no longer parses: TS1005 at the `(`) is a property of the
// missing name, '(Missing)' to the checker, nameless in the declaration with the rest of the tag as its description
// (markdown-magic's block-transformer)

// @filename: a.js
/**
 * @typedef {Object} Middleware
 * @property {string} name - Name
 * @property {function(string, number): Promise<string>|string} transform - Transform fn
 */

/** @type {Middleware} */
export const m = { name: '' };
