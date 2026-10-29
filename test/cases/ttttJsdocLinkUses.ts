// JSDoc {@link} names are uses where tsc's checkSourceElement visits the JSDoc's host (checkJSDocComments): main text
// and top-level tag comments only, leading comments after a line break (parameters and declarations: trailing ones too)
// @noUnusedLocals: true
// @noUnusedParameters: true
// @filename: m.ts
export class A {} export class B {} export class C {} export class D {} export class E {} export class F {}
export class G {} export class H {} export class I {} export class J {} export class K {} export class L {}
export class M {} export class N {} export class O {} export class P {} export class Q {} export class R {}
export class S {} export class T {} export class U {} export class V {} export class W {} export class X {}
// @filename: a.ts
import { A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X } from "./m";
/** main {@link A} */
export function f1() {}
/**
 * @param x {@link B} desc
 * @returns {@link C} rest
 * @throws {@link D} after {@link E}
 * @see F
 * @see {@link G}
 * @deprecated use {@link H}
 */
export function f2(x: number) { return x; }
/**
 * @typedef {object} T1
 * @property {number} p {@link I}
 */
/**
 * @callback CB
 * @param {number} q {@link J}
 */
export class Cl {
    /** {@link K} */
    m() {}
    /** {@link L} */
    p = 1;
}
export function f3(/** {@link M} */ a: number) { return a; }
export const o = { /** {@link N} */ k: 1 };
export enum En {
    /** {@link O} */
    a,
}
export interface In {
    /** {@link P} */
    m: number;
}
export function f4() {
    /** {@link Q} */
    const z = 1;
    return z;
}
/** {@linkcode R} and {@linkplain S} and {@link   T#foo} and {@link U.prototype} */
export type Ty = number;
/**
 * @overload
 * @param {string} s {@link V}
 */
export function f5(s: string) { return s; }
/** @param {object} o desc
 * @param o.a {@link W}
 */
export function f6(o: { a: number }) { return o; }
/** text @link X inline */
export const v1 = 1;
