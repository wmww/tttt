// @noEmit: true
// @allowJs: true
// @checkJs: true
// @strict: true
// @target: es2017
// @filename: a.js
const CIRCULAR = Symbol("circular target");
/** @returns {{ a: number } | CIRCULAR | undefined} */
function f() { return CIRCULAR; }
const r = f();
if (r === CIRCULAR) {} else if (r) { r.a; }
const r2 = f();
if (r2 !== CIRCULAR && r2) { r2.a; }
