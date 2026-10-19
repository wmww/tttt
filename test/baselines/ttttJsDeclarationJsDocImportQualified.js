//// [test/cases/ttttJsDeclarationJsDocImportQualified.ts] ////

//// [core.ts]
export interface Plugin { name: string }
//// [m.ts]
import type { Plugin as P } from './core.js';
export namespace ESLint { export type Plugin = P; }
export namespace NS { export interface I { a: number } }

//// [a.js]
/**
 * @import {
 *   ESLint,
 * } from './m.js';
 * @import { NS } from './m.js'
 */
/** @import * as M from './m.js' */

/**
 * @param {number} [options]
 * @returns {ESLint.Plugin}
 */
export const getPlugin = (options = 1) => {
  return { name: '' };
};

/** @returns {M.NS.I} */
export function h() { return { a: 1 }; }




//// [core.d.ts]
export interface Plugin {
    name: string;
}
//# sourceMappingURL=core.d.ts.map//// [m.d.ts]
import type { Plugin as P } from './core.js';
export declare namespace ESLint {
    type Plugin = P;
}
export declare namespace NS {
    interface I {
        a: number;
    }
}
//# sourceMappingURL=m.d.ts.map//// [a.d.ts]
/**
 * @import {
 *   ESLint,
 * } from './m.js';
 * @import { NS } from './m.js'
 */
/** @import * as M from './m.js' */
import type { ESLint } from './m.js';
import type * as M from './m.js';
/**
 * @param {number} [options]
 * @returns {ESLint.Plugin}
 */
export declare const getPlugin: (options?: number) => ESLint.Plugin;
/** @returns {M.NS.I} */
export declare function h(): M.NS.I;
//# sourceMappingURL=a.d.ts.map