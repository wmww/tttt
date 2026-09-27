function f<T>(v: T) {
    class C { static s = v; x = v; m(): C { return this; } }
    interface I<U = string> { a: T; u: U; self: I }
    type A<W> = { t: T; w: W };
    let i: I = null!;
    let i2: I<number> = null!;
    let a: A<boolean> = null!;
    return { C, i, i2, a, c: new C() };
}
const r = f(3);
const s = r.C.s;
const x = new r.C().x;
const m = r.c.m();
const ia = r.i.a;
const iself = r.i.self;
const i2 = r.i2;
const aa = r.a;
class G<T> {
    make() { return class { t!: T }; }
}
const gm = new G<string>().make();
const gt = new gm().t;
