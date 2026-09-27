// @checkJs: true
// @allowJs: true
// @noEmit: true
// @module: commonjs
// @strict: true
// @filename: hook.d.ts
export class SyncHook<T extends any[]> { constructor(names: string[]); tap(name: string, fn: (...args: T) => void): void; }
// @filename: a.js
const { SyncHook } = require("./hook");
class Compiler {
	constructor() {
		this.hooks = Object.freeze({
			/** @type {SyncHook<[number, string]>} */
			compilation: new SyncHook(["a", "b"]),
		});
		/** @type {number} */
		this.n = 1;
	}
}
module.exports = Compiler;
// @filename: b.js
/** @typedef {import("./a")} Compiler */
/** @typedef {{ x: number }} Obj */
/**
 * @param {Compiler} compiler c
 * @param {Obj} o o
 */
function f(compiler, o) {
	compiler.hooks.compilation.tap("x", (c, s) => { c.toFixed(); s.length; });
	o.x;
}
