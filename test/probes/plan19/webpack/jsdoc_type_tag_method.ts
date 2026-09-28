// @strict: true
// @noEmit: true
// @allowJs: true
// @checkJs: true
// tsc: clean. A class method's (or function declaration's) `@type {Alias}` naming a function type is its signature
// (getSignaturesOfSymbol, getSignatureOfTypeTag) and types its parameters (getParameterTypeOfTypeTag), also when the
// method's type is first needed from the constructor (`path` was implicitly any when its own evaluation applied the tag)
// @filename: b.js
/**
 * @template T, R
 * @typedef {(item: T, callback: (r: R) => void) => void} Processor
 */
class A {
	constructor() {
		const p = this._read.bind(this);
	}
	/**
	 * @type {Processor<string, number>}
	 */
	_read(path, callback) {
		path.length;
	}
}
/**
 * @typedef {<T>(m : T) => T} IFn
 */
/** @type {IFn} */
function inJs(l) {
    return l;
}
inJs(1);
