//// [test/cases/ttttJsDeclarationPropertyDescriptions.ts] ////

//// [a.js]
/**
 * @typedef {Object} T
 * @property {string} a - the a
 *   continued   here
 *      more indented
 * @property {Object} [b] b desc
 * @property {number} b.c - nested desc
 * @property {number} d
 *   next line desc
 * @property {number} e {@link T} linked
 * @property {string} f first
 *
 *   after blank
 * @property {string} [g="x y"] defaulted
 * @property {string} h
 * @property {string} i desc */
/** @typedef {Object} U
 * @property {string} a one line */
/**
 * @typedef {Object[]} V
 * @property {string} a in array
 */
/**
 * @typedef Nested
 * @property {Object} oh
 * @property {number} oh.no
 */
/**
 * @callback Cb
 * @param {Object} e - e desc
 * @param {string} e.k - k desc
 * @returns {void}
 */
/**
 * @param {Object} o - o desc
 * @param {string} o.x - x desc
 */
export function f(o) {}
/**
 * @typedef Options
 *   Configuration (optional).
 * @property {string} [prefix='']
 *   Prefix to add in front of `id`s (default: `''`).
 *     and more
 */




//// [a.d.ts]
export type T = {
    /**
     * - the a
     * continued   here
     * more indented
     */
    a: string;
    /**
     * b desc
     */
    b?: {
        c: number;
    };
    /**
     *   next line desc
     */
    d: number;
    /**
     * {@link T} linked
     */
    e: number;
    /**
     * first
     *
     * after blank
     */
    f: string;
    /**
     * defaulted
     */
    g?: string;
    h: string;
    /**
     * desc
     */
    i: string;
};
export type U = {
    /**
     * one line
     */
    a: string;
};
export type V = {
    /**
     * in array
     */
    a: string;
}[];
export type Nested = {
    oh: {
        no: number;
    };
};
export type Cb = (e: {
    k: string;
}) => void;
/**
 * @typedef {Object} T
 * @property {string} a - the a
 *   continued   here
 *      more indented
 * @property {Object} [b] b desc
 * @property {number} b.c - nested desc
 * @property {number} d
 *   next line desc
 * @property {number} e {@link T} linked
 * @property {string} f first
 *
 *   after blank
 * @property {string} [g="x y"] defaulted
 * @property {string} h
 * @property {string} i desc */
/** @typedef {Object} U
 * @property {string} a one line */
/**
 * @typedef {Object[]} V
 * @property {string} a in array
 */
/**
 * @typedef Nested
 * @property {Object} oh
 * @property {number} oh.no
 */
/**
 * @callback Cb
 * @param {Object} e - e desc
 * @param {string} e.k - k desc
 * @returns {void}
 */
/**
 * @param {Object} o - o desc
 * @param {string} o.x - x desc
 */
export declare function f(o: {
    x: string;
}): void;
export type Options = {
    /**
     * Prefix to add in front of `id`s (default: `''`).
     * and more
     */
    prefix?: string;
};
/**
 * @typedef Options
 *   Configuration (optional).
 * @property {string} [prefix='']
 *   Prefix to add in front of `id`s (default: `''`).
 *     and more
 */
