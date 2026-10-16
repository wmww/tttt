//// [test/cases/ttttJsDeclarationReusedImportTypeAlias.ts] ////

//// [package.json]
{ "type": "module" }

//// [types.ts]
export interface S<T> { a: T }
export type SVG = S<string>;

//// [a.js]
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




//// [types.d.ts]
export interface S<T> {
    a: T;
}
export type SVG = S<string>;
//// [a.d.ts]
/**
 * @param {import('./types.js').SVG} el
 * @param {string} s
 */
export declare const f: (el: import('./types.js').SVG, s: string) => void;
/**
 * @param {import('./types.js').SVG} el
 */
export declare const g: (el: import('./types.js').SVG) => void;
/**
 * @param {import('./types.js').S} el
 */
export declare const h: (el: import('./types.js').S) => void;


//// [DtsFileErrors]


a.d.ts(13,30): error TS2314: Generic type 'S<T>' requires 1 type argument(s).


==== package.json (0 errors) ====
    { "type": "module" }
    
==== types.d.ts (0 errors) ====
    export interface S<T> {
        a: T;
    }
    export type SVG = S<string>;
    
==== a.d.ts (1 errors) ====
    /**
     * @param {import('./types.js').SVG} el
     * @param {string} s
     */
    export declare const f: (el: import('./types.js').SVG, s: string) => void;
    /**
     * @param {import('./types.js').SVG} el
     */
    export declare const g: (el: import('./types.js').SVG) => void;
    /**
     * @param {import('./types.js').S} el
     */
    export declare const h: (el: import('./types.js').S) => void;
                                 ~~~~~~~~~~~~~~~~~~~~~~
!!! error TS2314: Generic type 'S<T>' requires 1 type argument(s).
    