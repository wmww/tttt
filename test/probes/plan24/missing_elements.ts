declare const o: { p: number };
const { p, r } = o;
function k1({ p, r }: { p: number }) { }
function k2({ p, r } = { p: 1 }) { }
const { p: p3, r: r3 } = { p: 1 };
let [x1, x2] = [1];
function k3([a, b] = [1]) { }
