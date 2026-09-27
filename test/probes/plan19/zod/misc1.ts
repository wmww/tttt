// @strict: true
// @target: es2020
export const INVALID: unique symbol = Symbol.for("x");
const F2: unique symbol = Symbol.for("y");
declare const dv: number | (() => number);
const r1 = typeof dv === "function" ? (dv as Function)() : dv;
const factory = new Function("a", "return a");
const r2 = factory(1);
declare const f3: Function;
f3();
