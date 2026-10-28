// Past the end of a tuple is undefined (tsc's getTupleElementTypeOutOfStartCount): an array literal's extra elements
// against `[string, number] | undefined` elaborate to 'undefined', and an element access of a possibly undefined
// tuple past its end is undefined after TS18048
// @strict: true
declare let a: [string, number] | undefined;
var e1 = a![2];
declare let b: [string, number];
var e2 = b[2];
declare let c: { t?: [string, number] };
var e3 = c.t![2];
c.t = ["foo", 5, false, true];
let x: [string, number] | undefined = ["foo", 5, false];
declare let o: { t?: [string, number] };
var e4 = o.t[2];
o.t[3] = { a: "string" };
