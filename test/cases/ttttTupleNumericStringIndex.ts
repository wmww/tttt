// @strict: true
// A numeric name (a number literal or a string literal that is one) that no tuple has as an element, every member
// being a tuple: TS2493 / TS2514 for one tuple, TS2339 for a union, never TS7015, and the access reads what the
// tuples have past their leading elements, undefined without a rest element (tsc's getPropertyTypeForIndexType,
// isNumericLiteralName(propName), getTupleElementTypeOutOfStartCount; mutation testing, unionsOfTupleTypes1)
type T1 = [string, number];
type T2 = [boolean] | [string, number];
declare const t1: T1, t2: T2;
let a = t2["2"];
let b = t1["2"];
let c = t2[2];
let d = t1["5"];
let e = t2["1"];
let f = t2["-1"];
let g = t2["1.5"];
let h = t2["01"];
let i = t1["-1"];
type T4 = [boolean] | [string, ...number[]];
declare const t4: T4;
let na1 = t2[1.5];
let na2 = t2[-1];
let na3 = t1[-1];
let na4 = t1[1.5];
let nb1 = t2["1.5"];
let nb2 = t2["-1"];
let nb3 = t1["-1"];
let nb4 = t1["1.5"];
let nc1 = t4["5"];
let nc2 = t4[5];
let nc3 = t4["-1"];
type X = T2["2"];
type Y = T1["2"];
