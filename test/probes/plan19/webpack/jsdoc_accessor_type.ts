// @allowJs: true
// @checkJs: true
// @strict: true
// @noEmit: true
// @target: es2017
// (tttt lacks TS7032 entirely: a missing check, not this probe's subject)
// tsc: only the setter's TS7032/TS7006. An accessor's `@type` is the property's (getAnnotatedAccessorType): a getter's return type (a setter's is
// ignored: TS7032, TS7006), not the object literal item's declared type; an annotated getter isn't widened (`typeof S` stays unique). webpack
// serialization.js
// @filename: t.js
const S = Symbol("S");
/** @typedef {typeof S} S_TYPE */
const o = {
	/** @type {S_TYPE} */
	get s() {
		return S;
	},
	/** @type {number} */
	get n() { return 1; }
};
/** @type {typeof S} */
const x = o.s;
class C {
	/** @type {S_TYPE} */
	get s() { return S; }
}
const p = {
	/** @type {number} */
	set v(x) { x.toFixed(); }
};
