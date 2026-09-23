// @strict: true
// Regular literal types (from annotations) never widen; fresh ones (from literal expressions) do
declare const a: "a";
declare const one: 1;
declare const t: true;
const c: "c" = "c";
let x1 = a;
let x2 = one;
let x3 = t;
let x4 = c;
let x5 = "fresh";
let x6 = true;
let arr = [a, one, t];
let obj = { a, one, t, lit: "lit", n: 2 };
function ret1(p: "p") { return p; }
function ret2() { return "q"; }
function ret3(p: "p" | "q") { return p; }
class K { f = a; g = "g"; readonly h = "h"; }
const fromConst = "k";
let viaConst = fromConst;
let cond = Math.random() ? a : "b";
let arr2 = [cond];
