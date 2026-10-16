//// [test/cases/ttttJsDeclarationEmitJsDoc.ts] ////

//// [a.js]
/**
 * @typedef {Object} Opts
 * @property {string} a The a
 *   continued here
 *     indented more
 * @property {string} [b=x] - with {@link Opts} default
 *
 * @property {number} c
 *    starts on next line
 * @property {Object} d The d
 * @property {string} d.e The e
 * @property {{ [K in "x"|"y"]: number }} m mapped
 * @prop {number} f Last one */
/** @type {Opts} */
export const o = /** @type {any} */ (null);
/** @type {{ [K in "a"|"b"]: number }} */
export const x = /** @type {any} */ (null);
/**
 * @param {{ [K in "a"]: [a: string, b: number] }} p
 * @returns {{ [K in "a"]: { q: number } }}
 */
export function f(p) { return /** @type {any} */ (p); }
/** @typedef {{ [K in "a"]: number }} M */
/** @typedef {[a: string,
 *   b: number]} T */
/** @typedef {number} NS.Inner */
/**
 * @callback Cb
 * @param {string} [a-b]
 * @returns {void}
 */


//// [a.js]
/**
 * @typedef {Object} Opts
 * @property {string} a The a
 *   continued here
 *     indented more
 * @property {string} [b=x] - with {@link Opts} default
 *
 * @property {number} c
 *    starts on next line
 * @property {Object} d The d
 * @property {string} d.e The e
 * @property {{ [K in "x"|"y"]: number }} m mapped
 * @prop {number} f Last one */
/** @type {Opts} */
export const o = /** @type {any} */ (null);
/** @type {{ [K in "a"|"b"]: number }} */
export const x = /** @type {any} */ (null);
/**
 * @param {{ [K in "a"]: [a: string, b: number] }} p
 * @returns {{ [K in "a"]: { q: number } }}
 */
export function f(p) { return /** @type {any} */ (p); }
/** @typedef {{ [K in "a"]: number }} M */
/** @typedef {[a: string,
 *   b: number]} T */
/** @typedef {number} NS.Inner */
/**
 * @callback Cb
 * @param {string} [a-b]
 * @returns {void}
 */


//// [a.d.ts]
export type Opts = {
    /**
     * The a
     * continued here
     * indented more
     */
    a: string;
    /**
     * - with {@link Opts} default
     */
    b?: string;
    /**
     *    starts on next line
     */
    c: number;
    /**
     * The d
     */
    d: {
        e: string;
    };
    /**
     * mapped
     */
    m: { [K in "x" | "y"]: number; };
    /**
     * Last one
     */
    f: number;
};
/**
 * @typedef {Object} Opts
 * @property {string} a The a
 *   continued here
 *     indented more
 * @property {string} [b=x] - with {@link Opts} default
 *
 * @property {number} c
 *    starts on next line
 * @property {Object} d The d
 * @property {string} d.e The e
 * @property {{ [K in "x"|"y"]: number }} m mapped
 * @prop {number} f Last one */
/** @type {Opts} */
export declare const o: Opts;
/** @type {{ [K in "a"|"b"]: number }} */
export declare const x: { [K in "a" | "b"]: number; };
/**
 * @param {{ [K in "a"]: [a: string, b: number] }} p
 * @returns {{ [K in "a"]: { q: number } }}
 */
export declare function f(p: { [K in "a"]: [a: string, b: number]; }): { [K in "a"]: {
    q: number;
}; };
export type M = {
    [K in "a"]: number;
};
export type T = [
    a: string,
    b: number
];
export declare namespace NS {
    export type Inner = number;
}
export type Cb = (a_b?: string) => void;
/** @typedef {{ [K in "a"]: number }} M */
/** @typedef {[a: string,
 *   b: number]} T */
/** @typedef {number} NS.Inner */
/**
 * @callback Cb
 * @param {string} [a-b]
 * @returns {void}
 */
