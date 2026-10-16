//// [test/cases/ttttJsDeclarationMapsContinuationLine.ts] ////

//// [b.ts]
export interface A { a: number } export interface B { b: number } export interface C { c: number }

//// [a.js]
/**
 * @type {import('./b').A & import('./b').B &
 *       import("./b").C}
 */
const config = { a: 1, b: 1, c: 1 };
export default config;
/**
 * @param {string |
 *   number} x
 * @returns {Array<
 *   string>}
 */
export function f(x) { return []; }




//// [b.d.ts]
export interface A {
    a: number;
}
export interface B {
    b: number;
}
export interface C {
    c: number;
}
//# sourceMappingURL=b.d.ts.map//// [a.d.ts]
/**
 * @type {import('./b').A & import('./b').B &
 *       import("./b").C}
 */
declare const config: import('./b').A & import('./b').B & import("./b").C;
export default config;
/**
 * @param {string |
 *   number} x
 * @returns {Array<
 *   string>}
 */
export declare function f(x: string | number): Array<string>;
//# sourceMappingURL=a.d.ts.map