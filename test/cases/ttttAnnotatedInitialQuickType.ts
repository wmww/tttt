// @strict: true
// @target: esnext
// @module: esnext
// An annotated variable's initial type in control flow is its initializer's quick type when it has one (tsc's
// getInitialTypeOfVariableDeclaration -> getTypeOfInitializer -> getTypeOfExpression): a call of one non-generic
// signature, a `new`, an awaited one or an assertion, arguments and operands unchecked, so a closure that reads the
// variable through a later function isn't circular (bun 99-V4 getTypeOfInitializer__01/04/05)
declare function pick(v: unknown): number;
const x: string | number = pick(later());
function later() {
  return g();
}
const g = () => x;
const probe: never = g();

class A {
  a = 1;
  constructor(v: unknown) {}
}
class B {
  b = 2;
}
const n: A | B = new A(laterN());
function laterN() {
  return gn();
}
const gn = () => n;

declare function load(v: unknown): Promise<number>;
declare const more: boolean;
const w: string | number = await load(laterW());
function laterW() {
  return gw();
}
const gw = () => w;
const y: string | number = (more ? laterY() : 1) as number;
function laterY() {
  return gy();
}
const gy = () => y;
export {}
