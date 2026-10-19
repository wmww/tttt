//// [test/cases/ttttJsDeclarationMapsContinuationLiterals.ts] ////

//// [a.js]
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




//// [a.d.ts]
export type OptionsShape = {
    n?: number;
    'ts-check'?: string;
};
export type ChangeTag = (tag: string, ...tokens: number[]) => void;
export type P = {
    path: string;
    title?: string | undefined;
};
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
export declare function f(p: P, n?: number | undefined): void;
//# sourceMappingURL=a.d.ts.map