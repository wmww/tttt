// A literal's written member with a widening null (`{ p: null, ...o }`) makes the spread result ContainsWideningType
// even once o's p replaced it (tsc's checkObjectLiteral passes its members' flags to getSpreadType): no property
// reports TS7018, so the declaration is TS7005/TS7006 with the whole type
// @strictNullChecks: false
// @noImplicitAny: true
declare const o: { p: string; s: string };
declare const u: { p: string } | { q: number };
const a1 = { p: null, ...o };
const a2 = { ...o, p: null };
const a3 = { p: null, s: undefined, ...o };
const a4 = { p: null, ...u };
const a5 = { x: [null], ...o };
const a6 = { x: { y: null }, ...o, x2: 1 };
function f(x = { p: null, ...o }) { return x; }
let a7 = { p: null, ...o, s: "s" };
