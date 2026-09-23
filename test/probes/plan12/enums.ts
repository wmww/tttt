// @strict: true
enum E { x }
enum F { a, b }
let e1 = E.x;
const e2 = E.x;
let f1 = F.a;
const f2 = F.a;
declare const fa: F.a;
let f3 = fa;
let s: string = E.x;
let s2: string = F.b;
let arr = [F.a, fa];
