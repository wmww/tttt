// @strict: true
// @module: commonjs
// @noEmit: true

// Errors tsc's checker reports that tttt's parser finds (a non-string `require()` specifier's TS1141, a parameter
// initializer's `yield`/`await`, TS2523/TS2524) are semantic: they don't make the file one with parse errors, so the
// file's other semantic errors stay, and they stay next to a real parse error (bun's 03-C16, 06-C5)
// @filename: a.ts
import e = require(foo);
export {};
const s1: number = "";
// @filename: b.ts
function* g5() { const f = (a = yield "x") => { }; }
function* g6() { const f = (a = yield) => { }; }
const s2: number = "";
// @filename: c.ts
function * foo(a = yield => yield) {
}
