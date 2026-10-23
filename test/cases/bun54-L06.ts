// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "noUnusedLocals": true, "allowJs": true, "checkJs": true } }
// @filename: a.ts
import { A1, A2, A3, A4, A5, A6, A7, A8, A9 } from "./p";
/**
 * @typedef {Object} T
 * @property {string} a see {@link A1}
 */
export const s1 = 1;
/** @callback F
 * @param {string} a see {@link A2}
 * @returns {string} see {@link A3}
 */
export const s2 = 1;
/** @param {Object} o see {@link A4}
 * @param {string} o.a see {@link A5}
 * @param {string} b see {@link A6}
 */
export function s12(o: any, b: any) { return [o, b]; }
/**
 * @overload
 * @param {string} a see {@link A7}
 * @returns {string} see {@link A8}
 */
/**
 * @param {string} a see {@link A9}
 */
export function s13(a: any) { return a; }
export const sentinel: number = "";
// @filename: b.js
import { A1, A2, A3, A4, A5, A6, A7, A8, A9 } from "./p";
/**
 * @typedef {Object} T
 * @property {string} a see {@link A1}
 */
/** @callback F
 * @param {string} a see {@link A2}
 * @returns {string} see {@link A3}
 */
/** @param {Object} o see {@link A4}
 * @param {string} o.a see {@link A5}
 * @param {string} b see {@link A6}
 */
export function s12(o, b) { return [o, b]; }
/**
 * @overload
 * @param {string} a see {@link A7}
 * @returns {string} see {@link A8}
 */
/**
 * @param {string} a see {@link A9}
 */
export function s13(a) { return a; }
/** @type {number} */
export const sentinel = "";
// @filename: p.ts
export class A1 {} export class A2 {} export class A3 {} export class A4 {} export class A5 {} export class A6 {} export class A7 {} export class A8 {} export class A9 {}
export class B1 {} export class B2 {} export class B3 {} export class B4 {} export class B5 {} export class B6 {} export class B7 {} export class B8 {} export class B9 {}
