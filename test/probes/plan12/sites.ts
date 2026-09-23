// @strict: true
// @target: es2015
// Every widening site with a regular literal source: tsc keeps the literal type at all of them
declare const a: "a";
declare const t: true;
declare function box<T>(x: T): { v: T };
let b1 = box(a);
let b2 = box("fresh");
function withDefault(p = a) { return p; }
const o = { m() { return a; } };
const [d1 = a] = [] as string[];
function* gen() { yield a; }
let flag = true;
flag = t;
let narrowed = flag;
