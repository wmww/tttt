function f<T>(v: T) {
    interface I { x: T }
    class C<U> { t = v; u!: U; self(): C<U> { return this; } }
    class D extends C<string> { d = v; }
    interface J extends I { y: T[] }
    return { i: null! as I, c: new C<number>(), d: new D(), j: null! as J, C, D };
}
let a = f(1);
let b = f("s");
a.i = b.i;
a.c = b.c;
const dt = a.d.t;
const du = a.d.u;
const jy = a.j.y;
const jx = a.j.x;
const nc = new a.C<boolean>();
const nd = new b.D().self();
if (a.c instanceof a.C) { a.c; }
function g<T>() {
    class L { next?: L; val!: T }
    const l = new L();
    return l.next!.next!.val;
}
const gv = g<number>();
class Outer<T> {
    m<U>(u: U) {
        class In { t!: T; u = u; }
        return new In();
    }
    static s<V>() { return class { v!: V }; }
}
const oi = new Outer<string>().m(1);
const ot = oi.t;
const os = new (Outer.s<boolean>())().v;
