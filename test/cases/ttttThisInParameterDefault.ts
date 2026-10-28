// @strict: true
// `this` in a parameter's initializer (or a default in its pattern) of a function whose `this` it would be isn't that
// function's `this` (bun 99-V4 checkObjectLiteralMethod__01, checkPropertyAssignment__03...): tsc's tryGetThisTypeAt
// skips the function for isInParameterInitializerBeforeContainingFunction unless it has a `this` parameter, and an
// object literal method's contextual `this` never applies there: any, TS2683. Through an arrow function it does
export {}
const o = {
  k: 1,
  m(x = this.k) { return x; },
  n: function (y = this.k) { return y; },
  p({ a = this.k }: { a?: number }) { return a; },
  q(this: { k: number }, z = this.k) { return z; },
  r(w = () => this.k) { return w; },
  s(v = (() => this.k)()) { return v; },
};
const probeM: never = o.m();
const probeP: never = o.p({});
function f(a = this) { return a; }
function g(this: { s: string }, b = this.s) { return b; }
class C {
  k = 1;
  m(x = this.k) { return x; }
  static s = 1;
  static sm(x = this.s) { return x; }
}
const h = function (c = this) { return c; };
const ctx: { k: number; m(x?: number): number } = { k: 1, m(x = this.k) { return x; } };
