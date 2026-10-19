//// [test/cases/ttttJsDocFencedCodeBlocks.ts] ////

//// [a.js]
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




//// [a.d.ts]
/**
 * Preserves (``` or ~~~).
 * @param {string} content - c
 * @returns {string} r
 */
export declare function f1(content: any): any;
/**
 * Unclosed `tick here @param {number} x
 * @param {string} y - c
 */
export declare function f2(x: any, y: string): void;
/**
 * Inline `@param {number} q` mention @param {number} z
 * @param {string} w - c
 */
export declare function f3(z: number, w: string): void;
/**
 * @param {string} a - example:
 * ```js
 * @param {number} b
 * ```
 * @param {boolean} c
 */
export declare function f4(a: string, b: any, c: boolean): void;
/**
 * Fence
 * ```
 * @param {number} d
 * ```
 * @param {boolean} e
 */
export declare function f5(d: any, e: boolean): void;
