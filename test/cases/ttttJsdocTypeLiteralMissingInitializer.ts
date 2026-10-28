// @allowJs: true
// @checkJs: true
// @strict: true
// @noEmit: true
// @filename: b.js
/** @typedef {{ q: string= }} T1 */
/** @typedef {{ q: string=}} T2 */
/** @typedef {{ q: string=    }} T3 */
/** @type {{ q: string= , r: number }} */
let v4;
/** @typedef {{ q: string= 1 }} T5 */
// @filename: c.ts
type A = { q: string = };
interface B { q: string = }
