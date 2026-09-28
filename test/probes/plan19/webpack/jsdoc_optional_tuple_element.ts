// @noEmit: true
// @allowJs: true
// @checkJs: true
// @strict: true
// @filename: a.js
// @ts-check
// tsc: clean. JSDoc `[T?]` is an optional element (parseTupleElementType), `T?` elsewhere is `T | null`
/** @type {[number, number, boolean?][]} */
const values = [];
values.push([1, 2]);
/** @type {[number | boolean?]} */
let u = [null];
/** @type {[(number | boolean)?]} */
let v = [];
/** @type {[boolean[]?]} */
let w = [];
