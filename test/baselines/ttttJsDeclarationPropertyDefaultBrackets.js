//// [test/cases/ttttJsDeclarationPropertyDefaultBrackets.ts] ////

//// [a.js]
/**
 * @typedef {Object} Options
 * @property {string[]} [exclude=[]] - Array of glob patterns to exclude from the tree.
 * @property {number} [depth=[1][0]] - Depth.
 */

/** @param {Options} o */
function tree(o) {}
module.exports = { tree };




//// [a.d.ts]
/**
 * @typedef {Object} Options
 * @property {string[]} [exclude=[]] - Array of glob patterns to exclude from the tree.
 * @property {number} [depth=[1][0]] - Depth.
 */
declare const _exports: {
    tree: typeof tree;
};
export = _exports;
export type Options = {
    /**
     * - Array of glob patterns to exclude from the tree.
     */
    exclude?: string[];
    /**
     * - Depth.
     */
    depth?: number;
};
/** @param {Options} o */
declare function tree(o: Options): void;
