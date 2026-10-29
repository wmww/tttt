// More hosts of JSDoc {@link} uses (see ttttJsdocLinkUses): case clauses, namespaces, arrow and function type
// parameters, type literal members, backticks, fences, nested @param, decorators, type parameters, declarations
// @noUnusedLocals: true
// @noUnusedParameters: true
// @filename: m.ts
export class A {} export class B {} export class C {} export class D {} export class E {} export class F {}
export class G {} export class H {} export class I {} export class J {} export class K {} export class L {}
export class M {} export class N {} export class O {} export class P {} export class Q {} export class R {}
export class S {} export class T {} export class U {} export class V {} export class W {} export class X {}
// @filename: a.ts
import { A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X } from "./m";
export function f1(x: number) {
    switch (x) {
        case 1:
            /** {@link A} */
            return 1;
    }
    return 0;
}
export namespace NS {
    /** {@link B} */
    export const b = 1;
}
export const arrow = (/** {@link C} */ p: number) => p;
export type TL = {
    /** {@link D} */
    m: number;
};
export class Cl { /** {@link E} */ m = 1; }
export type FT = (/** {@link F} */ q: number) => void;
export const ol = { m(/** {@link G} */ r: number) { return r; } };
/** `{@link H}` and ```
 * {@link I}
 * ``` {@link J} */
export const v1 = 1;
/** x @see {@link K} */
export const v2 = 1;
/**
 * @param {Object} o
 * @param {number} o.a {@link L}
 * @returns nothing {@link M}
 */
export function f2(o: { a: number }) { return o; }
/** @typedef {number} T2
 * @property {number} z {@link N}
 */
export class Dec {
    m(@dd /** {@link O} */ s: number) { return s; }
}
declare function dd(...a: any[]): any;
export function f3<
    /** {@link P} */
    Q1>() {}
/** {@link Q.foo} {@link R#bar} {@link   S} */
export const v3 = 1;
export const v4 = 1, /** {@link T} */ v5 = 2;
for (const /** {@link U} */ z of [1]) { z; }
label: /** {@link V} */ for (;;) { break label; }
