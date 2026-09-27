// @noEmit: true
// @allowJs: true
// @checkJs: true
// @strict: true
// @filename: node.d.ts
interface NodeRequire { (id: string): any; }
declare var require: NodeRequire;
// @filename: a.js
module.exports = class A { constructor() { this.a = 1; } };
// @filename: m.js
/** @template T @param {() => T} f @returns {T} */
const call = (f) => f();
/** @type {number} */ const x = call(() => require("./a"));
/** @type {number} */ const y = (0, require("./a"));
