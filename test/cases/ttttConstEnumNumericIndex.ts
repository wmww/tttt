// A const enum's object has the reverse mapping's `[x: number]: string` like any enum with a numeric member (tsc's
// resolveAnonymousTypeMembers): `E2["1"]` is a string, not TS2339; by a number it is TS2476 and the error type
const enum E2 { A }
var y0 = E2["1"];
var y1 = E2["A"];
var y2 = E2[1];
declare enum E3 { A }
var y4 = E3["1"];
const enum E4 { A = "a" }
var y5 = E4["1"];
