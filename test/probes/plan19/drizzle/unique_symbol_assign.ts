// @target: es2015
const S = Symbol.for("x");
const o = { a: S as typeof S, b: "x" as "x", c: <"y">"y", e: S satisfies typeof S, f: [S as typeof S] };
const c: number = o;
let x = S as typeof S;
const d: number = x;
