// @allowJs: true
// @checkJs: true
// @declaration: true
// @declarationMap: true
// @emitDeclarationOnly: true
// A JSDoc `@template`'s type parameter is mapped: its name, its constraint where written, its end at the name's end
// (a bracketed one with a default: past the `]`) (rocket's debounce)

// @filename: a.js
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
