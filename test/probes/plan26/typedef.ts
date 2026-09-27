// @noEmit: true
// @allowJs: true
// @checkJs: true
// @strict: true
// @filename: a.js
/** @typedef {string | number} Id */
/**
 * @typedef {Object} Opts
 * @property {string} name the name
 * @property {number=} size
 * @property {Id} [id]
 * @property {Object} nested
 * @property {boolean} nested.flag
 */
/**
 * @callback Cb
 * @param {string} a
 * @param {number} [b]
 * @returns {boolean}
 */
/**
 * @template T
 * @typedef {{ value: T }} Box
 */

/** @param {Opts} o @param {Cb} cb @param {Box<Id>} box */
function f(o, cb, box) {
    o.name;
    o.nested.flag;
    cb("x");
    box.value;
    /** @type {Id} */
    const x = 1;
    return x;
}
/** @type {Missing} */
let m;
module.exports = f;
// @filename: b.ts
import type { Opts } from "./a";
const o: Opts = { name: "a", nested: { flag: true } };
