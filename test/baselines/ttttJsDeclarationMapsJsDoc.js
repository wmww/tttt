//// [test/cases/ttttJsDeclarationMapsJsDoc.ts] ////

//// [b.ts]
export interface X { a: number }

//// [a.js]
/**
 * @param {Object} opts - Options for building configurations.
 * @param {string[]} opts.entry - The entry array.
 * @returns {import('./b').X}
 */
export function modernConfig(opts) {
  return { a: opts.entry.length };
}

/**
 * @param {Object} o
 * @param {string} o.x
 * @param {Object} o.y
 * @param {number} o.y.z */
export function f(o) { return o; }

/**
 * @param {Object[]} arr - list
 * @param {string} arr[].n
 */
export function h(arr) {}

/**
 * @param {Object} [o]
 * @param {string} [o.x="a"]
 */
export function k(o) {}

/**
 * @typedef {Object} A
 * @property {number} [b]
 */
/**
 * @typedef {Object} B
 * @property {number} [b]
 * @see x
 */
/**
 * @typedef {Object} C
 * @property {number} b
 */
/**
 * @typedef {Object} D
 * @property {number} [b=1]
 * @property {number} c */
/**
 * @typedef {string} S
 */
/** @typedef {number} N */
/**
 * @callback Cb
 * @param {Object} e
 * @param {string} e.k
 * @returns {void}
 */
/**
 * @callback Cc
 * @param {string} [e]
 */

/**
 * @param {*} a
 * @returns {*}
 */
export const g = (a) => a;




//// [b.d.ts]
export interface X {
    a: number;
}
//# sourceMappingURL=b.d.ts.map//// [a.d.ts]
/**
 * @param {Object} opts - Options for building configurations.
 * @param {string[]} opts.entry - The entry array.
 * @returns {import('./b').X}
 */
export declare function modernConfig(opts: {
    entry: string[];
}): import('./b').X;
/**
 * @param {Object} o
 * @param {string} o.x
 * @param {Object} o.y
 * @param {number} o.y.z */
export declare function f(o: {
    x: string;
    y: {
        z: number;
    };
}): {
    x: string;
    y: {
        z: number;
    };
};
/**
 * @param {Object[]} arr - list
 * @param {string} arr[].n
 */
export declare function h(arr: {
    n: string;
}[]): void;
/**
 * @param {Object} [o]
 * @param {string} [o.x="a"]
 */
export declare function k(o?: {
    x?: string;
}): void;
export type A = {
    b?: number;
};
export type B = {
    b?: number;
};
export type C = {
    b: number;
};
export type D = {
    b?: number;
    c: number;
};
export type S = string;
export type N = number;
export type Cb = (e: {
    k: string;
}) => void;
export type Cc = (e?: string) => any;
/**
 * @typedef {Object} A
 * @property {number} [b]
 */
/**
 * @typedef {Object} B
 * @property {number} [b]
 * @see x
 */
/**
 * @typedef {Object} C
 * @property {number} b
 */
/**
 * @typedef {Object} D
 * @property {number} [b=1]
 * @property {number} c */
/**
 * @typedef {string} S
 */
/** @typedef {number} N */
/**
 * @callback Cb
 * @param {Object} e
 * @param {string} e.k
 * @returns {void}
 */
/**
 * @callback Cc
 * @param {string} [e]
 */
/**
 * @param {*} a
 * @returns {*}
 */
export declare const g: (a: any) => any;
//# sourceMappingURL=a.d.ts.map