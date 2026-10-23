// @allowJs: true
// @checkJs: true
// @noEmit: true
// A JSDoc `@type {unique symbol}` on a `let` is TS1332 like the annotation (hacks.md `unique symbol` placement:
// tttt checks it while parsing annotations, and JSDoc types are parsed on demand)
// @filename: a.js
/** @type {unique symbol} */
let s = Symbol();
/** @type {unique symbol} */
const t = Symbol();
