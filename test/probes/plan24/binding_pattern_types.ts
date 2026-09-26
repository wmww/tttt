// @strict: true
function f({ x, y = 0 } = { x: 0 }) { return x + y; }
f({ x: 1, y: 1 });
let [a = 1, c] = [undefined, "s"];
const g = ({ q = 1 }) => q;
const h = ([m, n = "s"]) => m;
declare const o: { a: number; b: string; c: boolean };
const { a: aa, ...rest } = o;
function k({ p, r } = { p: 1 }) { return p; }
