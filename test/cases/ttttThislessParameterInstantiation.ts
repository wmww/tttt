// @strict: true
// @noEmit: true

// tsc's instantiateSymbol leaves a parameter without annotation or initializer alone under a mapper of `this` only
// (isThisless): a contextually typed `(x) => 1` keeps `x: this` through a non-generic class's members, while its
// return type and an annotated `(x: this)` are instantiated; a generic class's mapper has its type parameters too
// (bun case 26-P3a)
declare function id<T>(t: T): T;
class C {
  h = ((x) => 1) satisfies (x: this) => number;
  h1 = ((x) => x) satisfies (x: this) => this;
  h2 = ((x: this) => 1) satisfies (x: this) => number;
  h3 = id<(x: this) => number>((x) => 1);
  h4 = (() => (x: this) => 1) satisfies () => (x: this) => number;
  h5 = ((x, y = this) => y) satisfies (x: this, y?: this) => this;
}
const a = new C().h;
const a1 = new C().h1;
const a2 = new C().h2;
const a3 = new C().h3;
const a4 = new C().h4;
const a5 = new C().h5;
new C().h(new C());

class D extends C {}
const d = new D().h;

class G<T> {
  h = ((x) => 1) satisfies (x: this) => number;
  constructor(public t: T) {}
}
const g = new G("s").h;
g(new G("s"));

interface I { m: (x: this) => void }
declare const i: I;
const im = i.m;
const lit: I = { m: (x) => {} };
