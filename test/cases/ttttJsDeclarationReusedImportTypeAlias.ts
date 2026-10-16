// @allowJs: true
// @declaration: true
// @emitDeclarationOnly: true
// @strict: true
// @target: es2022
// @module: nodenext
// A JSDoc import type naming an alias of a generic type's instantiation is reused as written (its quotes kept): only
// a type reference node naming the reference's own target is checked for missing type arguments (tsc's
// existingTypeNodeIsNotReferenceOrIsReferenceWithCompatibleTypeArgumentCount; mermaid's setupGraphViewbox.js)

// @filename: package.json
{ "type": "module" }

// @filename: types.ts
export interface S<T> { a: T }
export type SVG = S<string>;

// @filename: a.js
/**
 * @param {import('./types.js').SVG} el
 * @param {string} s
 */
export const f = function (el, s) {};
/**
 * @param {import('./types.js').SVG} el
 */
export const g = (el) => {};
/**
 * @param {import('./types.js').S} el
 */
export const h = (el) => {};
