//// [test/cases/ttttJsDocPropertyMissingName.ts] ////

//// [a.js]
/**
 * @typedef {Object} Middleware
 * @property {string} name - Name
 * @property {function(string, number): Promise<string>|string} transform - Transform fn
 */

/** @type {Middleware} */
export const m = { name: '' };




//// [a.d.ts]
/**
 * @typedef {Object} Middleware
 * @property {string} name - Name
 * @property {function(string, number): Promise<string>|string} transform - Transform fn
 */
export type Middleware = {
    /**
     * - Name
     */
    name: string;
    /**
     * (string, number): Promise<string>|string} transform - Transform fn
     */
    : Function;
};
/** @type {Middleware} */
export declare const m: Middleware;
