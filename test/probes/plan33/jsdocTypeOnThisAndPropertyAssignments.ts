// @checkJs: true
// @allowJs: true
// @noEmit: true
// @module: commonjs
// @strict: true
// @target: es2017
// @filename: hook.d.ts
export class Hook<T extends any[]> { constructor(names?: string[]); tap(name: string, fn: (...args: T) => void): void; }
// @filename: a.js
const { Hook } = require("./hook");
class C {
	constructor() {
		/** @type {Set<number>} */
		this.set = new Set();
		/** @type {Hook<[number]>} */
		this.hook = new Hook(["a"]);
		this.hooks = Object.freeze({
			/** @type {Hook<[string]>} */
			h: new Hook(["a"]),
		});
		/** @type {number[] | Record<string, number>} */
		this.u = [];
	}
	m() {
		for (const x of this.set) x.toFixed();
		this.hook.tap("a", (n) => n.toFixed());
		this.hooks.h.tap("a", (s) => s.length);
		/** @type {Hook<[string]>} */
		const local = new Hook();
		local.tap("a", (s) => s.length);
	}
}
module.exports = C;
