// @noEmit: true
// @strict: true
// An accessor pair with computed names is one symbol by the key's type, not its text (tsc's lateBindMember):
// `[a]` and `[b]` with b of `typeof a` pair (the setter's parameter has the getter's type, the property is writable).
// A name of type string isn't late bindable: its accessors stay apart (TS7032 on the setter), and accessors after a
// method of their name conflict with it, each its own symbol
const a = Symbol();
declare const b: typeof a;
class C {
    get [a](): string { return ""; }
    set [b](v) { v.length; }
}
declare const c: C;
c[a] = "x";
const k = "x";
declare const k2: typeof k;
class D {
    get [k](): string { return ""; }
    set [k2](v) { v.length; }
}
declare const d: D;
d[k] = "y";
declare const ks: string;
class E {
    get [ks](): number { return 1; }
    set [ks](v) { }
}
const uu = Symbol();
class F {
    [uu]() {}
    get [uu](): number { return 1; }
    set [uu](v) { }
}
