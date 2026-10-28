// @strict: true
// tsc checks the operand of `+` and `delete` even though the result type doesn't need it: an equality on a
// loop's partial flow types reports, a self-reference is circular (bun checks-of-a-value-assigned-on-a-loop-back-edge)
declare const more: boolean;
function d24() { let x: string | number = "a"; while (more) { x = +(x === 5); } }
var a = +a;
const b = { get v() { return +b.v; } };
const o: { x?: number } = {};
function d25() { let x: string | number = "a"; while (more) { x = delete o[(x === 5) ? "x" : "x"] ? 1 : 2; } }
var c = { y: delete c.y };
