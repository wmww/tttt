//// [test/cases/ttttJsDeclarationMapsTemplateTags.ts] ////

//// [a.js]
/**
 * @template {(...args: any) => any} F
 * @param {F} fn
 */
export function g(fn) { }
/**
 * @template T, U
 * @template [V=string]
 * @param {T} a
 * @param {U} b
 * @param {V} c
 */
export function f(a, b, c) { }
/**
 * @template {number} K
 */
export class C {
    /** @param {K} k */
    m(k) {}
}
/**
 * @template X
 * @typedef {{ x: X }} Box
 */
/** @param {Box<number>} b */
export function g2(b) {}




//// [a.d.ts]
/**
 * @template {(...args: any) => any} F
 * @param {F} fn
 */
export declare function g<F extends (...args: any) => any>(fn: F): void;
/**
 * @template T, U
 * @template [V=string]
 * @param {T} a
 * @param {U} b
 * @param {V} c
 */
export declare function f<T, U, V = string>(a: T, b: U, c: V): void;
/**
 * @template {number} K
 */
export declare class C<K extends number> {
    /** @param {K} k */
    m(k: K): void;
}
export type Box<X> = {
    x: X;
};
/**
 * @template X
 * @typedef {{ x: X }} Box
 */
/** @param {Box<number>} b */
export declare function g2(b: Box<number>): void;
//# sourceMappingURL=a.d.ts.map