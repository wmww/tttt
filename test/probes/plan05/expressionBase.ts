const C = class { a = 1; static s = 2; m() { return this.a; } };
const c = new C();
c.a; C.s;
function mixin<T extends new (...a: any[]) => {}>(B: T) { return class extends B { x = 1; }; }
class Base { b = 1; }
class D extends mixin(Base) { f() { return this.x + this.b; } }
new D().x;
