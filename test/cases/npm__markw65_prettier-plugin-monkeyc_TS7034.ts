// origin: npm @markw65/prettier-plugin-monkeyc@1.0.69 https://github.com/markw65/prettier-plugin-monkeyc tsconfig.bench.json seed 20261031
// flip: no
// @strict: true
// src/printer.ts:74: `let rest, ...; ({ print, ..., ...rest } = estree)`: an auto-typed `let` assigned by an object rest
// element of a destructuring assignment. tsgo takes rest's type from that assignment; tttt reports TS7034 and TS7005
// (the array rest form below works).
declare const src: { a: number; b: string; c: boolean };
function f() {
  let rest, b;
  ({ b, ...rest } = src);
  return [rest, b];
}
function g() {
  let first, others;
  [first, ...others] = [1, 2, 3];
  return [first, others];
}
