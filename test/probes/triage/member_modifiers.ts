class A { private x = 1; protected y = 2; readonly z = 3; #p = 4; static #q = 5; }
class B { private x = 1; protected y = 2; readonly z = 3; }
class D extends A { f() { return this.y + this.x; } }
class E { #p = 1; g(a: A) { return a.#p; } }
class P { private constructor() {} static make() { return new P(); } }
const a = new A();
a.x; a.y; a.z = 4;
let b: B = a;
const p = new P();
const ro: { readonly r: number } = { r: 1 };
ro.r = 2;
const arr: readonly number[] = [1];
arr[0] = 2;
