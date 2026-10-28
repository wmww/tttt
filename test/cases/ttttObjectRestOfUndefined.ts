// @strictNullChecks: false,true
// The rest of null or undefined alone is `{}` (tsc's getRestType filters them out to never): an assignment target
// lacking properties is TS2739
let o = undefined;
var { ...m, a } = o;
let na: { b: number; c: string };
let b: number;
({ b, ...na } = o);
declare const u: undefined;
declare const nl: null;
var { ...n } = u;
var { ...n2 } = nl;
let nb: { b: number };
({ ...nb } = u);
function f<T>(x: T | undefined) { const { ...r } = x; return r; }
